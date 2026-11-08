// Distributed under the terms of the GNU General Public License v2
//
// nickel-accountsd: the Linux side of NickelOS local accounts.
//
// A NickelOS account is a real Linux user. The root filesystem (and with it
// /etc/passwd, /etc/group, /etc/shadow) is read-only and replaced by every OS
// update, so local users are kept in /var/lib/nickel/accounts (on the
// encrypted stateful volume) and merged into copies of the system files:
//
//   nickel-accountsd --merge   (boot, nickel-accounts-setup job)
//   nickel-accountsd --remerge (nickel-accounts-setup --gentoo: in place)
//     writes /run/nickel/accounts/{passwd,group,shadow} from the image's
//     files (saved there as *.base) plus the local users; the job then
//     bind-mounts them over /etc.
//   nickel-accountsd           (daemon, nickel-accountsd job)
//     owns org.nickelos.Accounts on the system bus and rewrites the merged
//     files in place after every change, so /etc reflects it at once.
//
// Each user gets a group of their own (gid = uid), membership in every group
// chronos belongs to (Chrome runs as the user; those groups grant it the
// devices and services chronos has), camera (webcams for Linux apps:
// WirePlumber opens them for the Camera portal; ChromeOS gives the group only
// to its camera service), and wheel if they are an administrator (sudo,
// /etc/sudoers.d/90-nickel-wheel).
//
// D-Bus interface org.nickelos.Accounts at /org/nickelos/Accounts:
//   CreateUser(s username, s display_name, s password, b admin) -> (u uid)
//   DeleteUser(s username)
//   ListUsers() -> (a(subs) users: name, uid, admin, display name)
//   SetPassword(s username, s old_password, s new_password)
//   SetAdmin(s password, s username, b admin)
//   RemoveUser(s password, s username)
// CreateUser and DeleteUser are only accepted from root, or from chronos
// while no user session is running (the login screen's Chrome); otherwise an
// app running as chronos could create itself an administrator. ListUsers is
// open to all (as /etc/passwd is). SetPassword is accepted from root, and
// from the user it changes with their current password: any app runs as that
// user, and an administrator's password is the key to sudo. SetAdmin and
// RemoveUser (Settings > Accounts, in a session) are accepted from an
// administrator with their own password; the last administrator stays one,
// and nobody removes themselves. Errors are named
// org.nickelos.Accounts.Error.{WrongPassword,NotAllowed,LastAdmin,NoSuchUser,
// Failed}; a wrong password is answered after a second, so guessing is slow.

#include <crypt.h>
#include <dirent.h>
#include <fcntl.h>
#include <gio/gio.h>
#include <glib.h>
#include <sys/stat.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fstream>
#include <map>
#include <optional>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace {

constexpr char kDbDir[] = "/var/lib/nickel/accounts";
constexpr char kUsersFile[] = "/var/lib/nickel/accounts/users";
constexpr char kShadowFile[] = "/var/lib/nickel/accounts/shadow";
constexpr char kRunDir[] = "/run/nickel/accounts";

constexpr uid_t kFirstUid = 5000;
constexpr uid_t kLastUid = 5999;
constexpr uid_t kChronosUid = 1000;
constexpr char kChronos[] = "chronos";
constexpr char kWheel[] = "wheel";
constexpr char kCamera[] = "camera";
// A user's home is /home/<name>: during their session nickel-session-setup
// mounts their My files there (the Files app's root, in their encrypted
// vault), so Files, the terminal and every app see one home. Signed out, it
// is an empty directory.
constexpr char kHomes[] = "/home";
constexpr char kShell[] = "/bin/bash";

constexpr char kServiceName[] = "org.nickelos.Accounts";
constexpr char kObjectPath[] = "/org/nickelos/Accounts";
constexpr char kIntrospection[] =
    "<node>"
    "  <interface name='org.nickelos.Accounts'>"
    "    <method name='CreateUser'>"
    "      <arg type='s' name='username' direction='in'/>"
    "      <arg type='s' name='display_name' direction='in'/>"
    "      <arg type='s' name='password' direction='in'/>"
    "      <arg type='b' name='admin' direction='in'/>"
    "      <arg type='u' name='uid' direction='out'/>"
    "    </method>"
    "    <method name='DeleteUser'>"
    "      <arg type='s' name='username' direction='in'/>"
    "    </method>"
    "    <method name='ListUsers'>"
    "      <arg type='a(subs)' name='users' direction='out'/>"
    "    </method>"
    "    <method name='SetPassword'>"
    "      <arg type='s' name='username' direction='in'/>"
    "      <arg type='s' name='old_password' direction='in'/>"
    "      <arg type='s' name='new_password' direction='in'/>"
    "    </method>"
    "    <method name='SetAdmin'>"
    "      <arg type='s' name='password' direction='in'/>"
    "      <arg type='s' name='username' direction='in'/>"
    "      <arg type='b' name='admin' direction='in'/>"
    "    </method>"
    "    <method name='RemoveUser'>"
    "      <arg type='s' name='password' direction='in'/>"
    "      <arg type='s' name='username' direction='in'/>"
    "    </method>"
    "  </interface>"
    "</node>";

#define NICKEL_INFO(...) syslog(LOG_INFO, __VA_ARGS__)
#define NICKEL_WARN(...) syslog(LOG_WARNING, __VA_ARGS__)

// A local account as stored in kUsersFile: name:uid:admin:display name.
struct User {
  std::string name;
  uid_t uid = 0;
  bool admin = false;
  std::string display_name;
};

std::vector<std::string> ReadLines(const std::string& path) {
  std::vector<std::string> lines;
  std::ifstream in(path);
  std::string line;
  while (std::getline(in, line)) {
    if (!line.empty()) {
      lines.push_back(line);
    }
  }
  return lines;
}

std::vector<std::string> Split(const std::string& s, char sep, int max) {
  std::vector<std::string> parts;
  size_t start = 0;
  while (static_cast<int>(parts.size()) < max - 1) {
    size_t pos = s.find(sep, start);
    if (pos == std::string::npos) {
      break;
    }
    parts.push_back(s.substr(start, pos - start));
    start = pos + 1;
  }
  parts.push_back(s.substr(start));
  return parts;
}

std::vector<User> LoadUsers() {
  std::vector<User> users;
  for (const auto& line : ReadLines(kUsersFile)) {
    auto f = Split(line, ':', 4);
    if (f.size() != 4) {
      continue;
    }
    users.push_back({f[0], static_cast<uid_t>(std::stoul(f[1])), f[2] == "1",
                     f[3]});
  }
  return users;
}

std::map<std::string, std::string> LoadShadow() {
  std::map<std::string, std::string> hashes;
  for (const auto& line : ReadLines(kShadowFile)) {
    auto f = Split(line, ':', 2);
    if (f.size() == 2) {
      hashes[f[0]] = f[1];
    }
  }
  return hashes;
}

// Writes |content| to |path| atomically (temp file + rename), mode |mode|.
bool WriteAtomically(const std::string& path,
                     const std::string& content,
                     mode_t mode) {
  std::string tmp = path + ".new";
  int fd = open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, mode);
  if (fd < 0) {
    return false;
  }
  bool ok = write(fd, content.data(), content.size()) ==
                static_cast<ssize_t>(content.size()) &&
            fsync(fd) == 0;
  close(fd);
  ok = ok && chmod(tmp.c_str(), mode) == 0 &&
       rename(tmp.c_str(), path.c_str()) == 0;
  if (!ok) {
    unlink(tmp.c_str());
  }
  return ok;
}

// Rewrites |path| in place: it may be bind-mounted over /etc, so it must
// keep its inode.
bool WriteInPlace(const std::string& path, const std::string& content) {
  int fd = open(path.c_str(), O_WRONLY | O_TRUNC | O_CLOEXEC);
  if (fd < 0) {
    return false;
  }
  bool ok = write(fd, content.data(), content.size()) ==
                static_cast<ssize_t>(content.size()) &&
            fsync(fd) == 0;
  close(fd);
  return ok;
}

bool SaveDb(const std::vector<User>& users,
            const std::map<std::string, std::string>& hashes) {
  std::ostringstream u, s;
  for (const auto& user : users) {
    u << user.name << ':' << user.uid << ':' << (user.admin ? 1 : 0) << ':'
      << user.display_name << '\n';
    auto it = hashes.find(user.name);
    s << user.name << ':' << (it != hashes.end() ? it->second : "!") << '\n';
  }
  g_mkdir_with_parents(kDbDir, 0700);
  return WriteAtomically(kUsersFile, u.str(), 0600) &&
         WriteAtomically(kShadowFile, s.str(), 0600);
}

// Builds the merged passwd/group/shadow and writes them to kRunDir. The
// image's own files are read from kRunDir/*.base.
bool Merge(bool create) {
  const std::string run = kRunDir;
  auto users = LoadUsers();
  auto hashes = LoadShadow();
  long days = time(nullptr) / 86400;

  std::ostringstream passwd, group, shadow;
  for (const auto& line : ReadLines(run + "/passwd.base")) {
    passwd << line << '\n';
  }
  for (const auto& user : users) {
    passwd << user.name << ":x:" << user.uid << ':' << user.uid << ':'
           << user.display_name << ':' << kHomes << '/' << user.name << ':'
           << kShell << '\n';
  }

  for (const auto& line : ReadLines(run + "/group.base")) {
    auto f = Split(line, ':', 4);
    if (f.size() != 4) {
      group << line << '\n';
      continue;
    }
    std::vector<std::string> members;
    if (!f[3].empty()) {
      members = Split(f[3], ',', 1 << 20);
    }
    bool chronos_group = f[0] == kChronos ||
                         std::find(members.begin(), members.end(), kChronos) !=
                             members.end();
    for (const auto& user : users) {
      if (chronos_group || f[0] == kCamera || (f[0] == kWheel && user.admin)) {
        members.push_back(user.name);
      }
    }
    group << f[0] << ':' << f[1] << ':' << f[2] << ':';
    for (size_t i = 0; i < members.size(); ++i) {
      group << (i ? "," : "") << members[i];
    }
    group << '\n';
  }
  for (const auto& user : users) {
    group << user.name << ":!:" << user.uid << ":\n";
  }

  for (const auto& line : ReadLines(run + "/shadow.base")) {
    shadow << line << '\n';
  }
  for (const auto& user : users) {
    auto it = hashes.find(user.name);
    shadow << user.name << ':' << (it != hashes.end() ? it->second : "!")
           << ':' << days << ":0:99999:7:::\n";
  }

  if (create) {
    return WriteAtomically(run + "/passwd", passwd.str(), 0644) &&
           WriteAtomically(run + "/group", group.str(), 0644) &&
           WriteAtomically(run + "/shadow", shadow.str(), 0600);
  }
  return WriteInPlace(run + "/passwd", passwd.str()) &&
         WriteInPlace(run + "/group", group.str()) &&
         WriteInPlace(run + "/shadow", shadow.str());
}

std::set<std::string> BaseNames(const std::string& file, uid_t* max_id) {
  std::set<std::string> names;
  for (const auto& line : ReadLines(std::string(kRunDir) + "/" + file)) {
    auto f = Split(line, ':', 4);
    names.insert(f[0]);
    if (max_id && f.size() >= 3) {
      *max_id = std::max<uid_t>(*max_id, std::stoul(f[2]));
    }
  }
  return names;
}

std::optional<std::string> HashPassword(const std::string& password) {
  char setting[CRYPT_GENSALT_OUTPUT_SIZE];
  if (!crypt_gensalt_rn("$y$", 0, nullptr, 0, setting, sizeof(setting))) {
    return std::nullopt;
  }
  struct crypt_data data = {};
  const char* hash = crypt_rn(password.c_str(), setting, &data, sizeof(data));
  if (!hash || hash[0] == '*') {
    return std::nullopt;
  }
  return std::string(hash);
}

// --- D-Bus ----------------------------------------------------------------

GDBusConnection* g_bus = nullptr;

std::optional<uid_t> CallerUid(const char* sender) {
  GVariant* reply = g_dbus_connection_call_sync(
      g_bus, "org.freedesktop.DBus", "/org/freedesktop/DBus",
      "org.freedesktop.DBus", "GetConnectionUnixUser",
      g_variant_new("(s)", sender), G_VARIANT_TYPE("(u)"),
      G_DBUS_CALL_FLAGS_NONE, 5000, nullptr, nullptr);
  if (!reply) {
    return std::nullopt;
  }
  guint32 uid;
  g_variant_get(reply, "(u)", &uid);
  g_variant_unref(reply);
  return uid;
}

// "stopped" at the login screen and during setup, "started" in a session.
std::string SessionState() {
  GVariant* reply = g_dbus_connection_call_sync(
      g_bus, "org.chromium.SessionManager", "/org/chromium/SessionManager",
      "org.chromium.SessionManagerInterface", "RetrieveSessionState", nullptr,
      G_VARIANT_TYPE("(s)"), G_DBUS_CALL_FLAGS_NONE, 5000, nullptr, nullptr);
  if (!reply) {
    return "unknown";
  }
  const gchar* state;
  g_variant_get(reply, "(&s)", &state);
  std::string result = state;
  g_variant_unref(reply);
  return result;
}

bool AllowedToManage(const char* sender) {
  auto uid = CallerUid(sender);
  if (!uid) {
    return false;
  }
  return *uid == 0 || (*uid == kChronosUid && SessionState() == "stopped");
}

// The system bus caches who is in which group, and D-Bus policies grant
// Chrome's access to group chronos (a local account's session runs Chrome as
// the account's user): make it look again after accounts change.
void ReloadBusConfig() {
  g_dbus_connection_call(g_bus, "org.freedesktop.DBus", "/org/freedesktop/DBus",
                         "org.freedesktop.DBus", "ReloadConfig", nullptr,
                         nullptr, G_DBUS_CALL_FLAGS_NONE, 5000, nullptr,
                         nullptr, nullptr);
}

void ReturnError(GDBusMethodInvocation* invocation,
                 GDBusError code,
                 const char* message) {
  NICKEL_WARN("%s", message);
  g_dbus_method_invocation_return_error_literal(invocation, G_DBUS_ERROR, code,
                                                message);
}

// /home/<name> must be free for the user's home: absent, or the empty,
// root-owned directory a removed user of that name left. Others there belong
// to the system: /home/user and /home/root hold every vault's mounts,
// /home/chronos Chrome's data.
bool HomeIsFree(const std::string& name) {
  const std::string home = std::string(kHomes) + "/" + name;
  struct stat st;
  if (lstat(home.c_str(), &st) != 0) {
    return errno == ENOENT;
  }
  if (!S_ISDIR(st.st_mode) || st.st_uid != 0) {
    return false;
  }
  DIR* dir = opendir(home.c_str());
  if (!dir) {
    return false;
  }
  bool empty = true;
  while (struct dirent* entry = readdir(dir)) {
    if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
      empty = false;
      break;
    }
  }
  closedir(dir);
  return empty;
}

void HandleCreateUser(GDBusMethodInvocation* invocation, GVariant* params) {
  const gchar *name, *display, *password;
  gboolean admin;
  g_variant_get(params, "(&s&s&sb)", &name, &display, &password, &admin);
  if (!AllowedToManage(g_dbus_method_invocation_get_sender(invocation))) {
    return ReturnError(invocation, G_DBUS_ERROR_ACCESS_DENIED,
                       "CreateUser is only allowed from the login screen");
  }
  static const std::regex kName("^[a-z_][a-z0-9_-]{0,31}$");
  std::string display_name = display;
  std::replace(display_name.begin(), display_name.end(), ':', ' ');
  std::replace(display_name.begin(), display_name.end(), '\n', ' ');
  if (!std::regex_match(name, kName)) {
    return ReturnError(invocation, G_DBUS_ERROR_INVALID_ARGS,
                       "invalid username");
  }
  if (strlen(password) == 0) {
    return ReturnError(invocation, G_DBUS_ERROR_INVALID_ARGS,
                       "empty password");
  }
  uid_t max_id = 0;
  auto taken = BaseNames("passwd.base", nullptr);
  auto taken_groups = BaseNames("group.base", nullptr);
  auto users = LoadUsers();
  for (const auto& user : users) {
    taken.insert(user.name);
    max_id = std::max(max_id, user.uid);
  }
  if (taken.count(name) || taken_groups.count(name) || !HomeIsFree(name)) {
    return ReturnError(invocation, G_DBUS_ERROR_INVALID_ARGS,
                       "username is taken");
  }
  uid_t uid = std::max(kFirstUid, max_id + 1);
  if (uid > kLastUid) {
    return ReturnError(invocation, G_DBUS_ERROR_LIMITS_EXCEEDED,
                       "no free user ids");
  }
  auto hash = HashPassword(password);
  if (!hash) {
    return ReturnError(invocation, G_DBUS_ERROR_FAILED,
                       "cannot hash password");
  }
  auto hashes = LoadShadow();
  users.push_back({name, uid, admin != FALSE, display_name});
  hashes[name] = *hash;
  if (!SaveDb(users, hashes) || !Merge(false)) {
    return ReturnError(invocation, G_DBUS_ERROR_FAILED,
                       "cannot write the user database");
  }
  NICKEL_INFO("created user %s (uid %u%s)", name, uid,
              admin ? ", administrator" : "");
  ReloadBusConfig();
  g_dbus_method_invocation_return_value(invocation, g_variant_new("(u)", uid));
}

void HandleDeleteUser(GDBusMethodInvocation* invocation, GVariant* params) {
  const gchar* name;
  g_variant_get(params, "(&s)", &name);
  if (!AllowedToManage(g_dbus_method_invocation_get_sender(invocation))) {
    return ReturnError(invocation, G_DBUS_ERROR_ACCESS_DENIED,
                       "DeleteUser is only allowed from the login screen");
  }
  auto users = LoadUsers();
  auto it = std::find_if(users.begin(), users.end(),
                         [&](const User& u) { return u.name == name; });
  if (it == users.end()) {
    return ReturnError(invocation, G_DBUS_ERROR_INVALID_ARGS, "no such user");
  }
  users.erase(it);
  auto hashes = LoadShadow();
  hashes.erase(name);
  if (!SaveDb(users, hashes) || !Merge(false)) {
    return ReturnError(invocation, G_DBUS_ERROR_FAILED,
                       "cannot write the user database");
  }
  NICKEL_INFO("deleted user %s", name);
  ReloadBusConfig();
  g_dbus_method_invocation_return_value(invocation, nullptr);
}

constexpr char kErrorPrefix[] = "org.nickelos.Accounts.Error.";

void ReturnNamedError(GDBusMethodInvocation* invocation,
                      const char* name,
                      const std::string& message) {
  NICKEL_WARN("%s", message.c_str());
  g_dbus_method_invocation_return_dbus_error(
      invocation, (std::string(kErrorPrefix) + name).c_str(),
      message.c_str());
}

// Whether |password| is |name|'s, checked against the stored hash. A wrong
// one costs a second (the daemon is single-threaded: guessing stays slow).
bool PasswordMatches(const std::string& name, const std::string& password) {
  auto hashes = LoadShadow();
  auto it = hashes.find(name);
  bool ok = false;
  if (it != hashes.end() && !it->second.empty() && it->second[0] == '$') {
    struct crypt_data data = {};
    const char* hash =
        crypt_rn(password.c_str(), it->second.c_str(), &data, sizeof(data));
    ok = hash && hash[0] != '*' && it->second == hash;
  }
  if (!ok) {
    g_usleep(G_USEC_PER_SEC);
  }
  return ok;
}

std::vector<User>::iterator FindUser(std::vector<User>& users,
                                     const std::string& name) {
  return std::find_if(users.begin(), users.end(),
                      [&](const User& u) { return u.name == name; });
}

// The local account calling, if it is an administrator and |password| is
// theirs; otherwise answers |invocation| with the error and returns nullopt.
std::optional<User> AdminCaller(GDBusMethodInvocation* invocation,
                                const std::vector<User>& users,
                                const std::string& password) {
  auto uid = CallerUid(g_dbus_method_invocation_get_sender(invocation));
  auto it = std::find_if(users.begin(), users.end(), [&](const User& u) {
    return uid && u.uid == *uid;
  });
  if (it == users.end() || !it->admin) {
    ReturnNamedError(invocation, "NotAllowed",
                     "only an administrator can change other accounts");
    return std::nullopt;
  }
  if (!PasswordMatches(it->name, password)) {
    ReturnNamedError(invocation, "WrongPassword",
                     "wrong password for " + it->name);
    return std::nullopt;
  }
  return *it;
}

void HandleListUsers(GDBusMethodInvocation* invocation) {
  GVariantBuilder b;
  g_variant_builder_init(&b, G_VARIANT_TYPE("a(subs)"));
  for (const auto& user : LoadUsers()) {
    g_variant_builder_add(&b, "(subs)", user.name.c_str(), user.uid,
                          user.admin, user.display_name.c_str());
  }
  g_dbus_method_invocation_return_value(invocation,
                                        g_variant_new("(a(subs))", &b));
}

void HandleSetPassword(GDBusMethodInvocation* invocation, GVariant* params) {
  const gchar *name, *old_password, *password;
  g_variant_get(params, "(&s&s&s)", &name, &old_password, &password);
  auto users = LoadUsers();
  auto it = FindUser(users, name);
  if (it == users.end()) {
    return ReturnNamedError(invocation, "NoSuchUser", "no such user");
  }
  auto caller = CallerUid(g_dbus_method_invocation_get_sender(invocation));
  if (!caller || (*caller != 0 && *caller != it->uid)) {
    return ReturnNamedError(invocation, "NotAllowed",
                            "only the user can change their password");
  }
  if (*caller != 0 && !PasswordMatches(name, old_password)) {
    return ReturnNamedError(invocation, "WrongPassword",
                            std::string("wrong password for ") + name);
  }
  auto hash = HashPassword(password);
  if (strlen(password) == 0 || !hash) {
    return ReturnNamedError(invocation, "Failed", "invalid password");
  }
  auto hashes = LoadShadow();
  hashes[name] = *hash;
  if (!SaveDb(users, hashes) || !Merge(false)) {
    return ReturnNamedError(invocation, "Failed",
                            "cannot write the user database");
  }
  NICKEL_INFO("changed the password of %s", name);
  g_dbus_method_invocation_return_value(invocation, nullptr);
}

void HandleSetAdmin(GDBusMethodInvocation* invocation, GVariant* params) {
  const gchar *password, *name;
  gboolean admin;
  g_variant_get(params, "(&s&sb)", &password, &name, &admin);
  auto users = LoadUsers();
  auto caller = AdminCaller(invocation, users, password);
  if (!caller) {
    return;
  }
  auto it = FindUser(users, name);
  if (it == users.end()) {
    return ReturnNamedError(invocation, "NoSuchUser", "no such user");
  }
  if (!admin && it->admin &&
      std::count_if(users.begin(), users.end(),
                    [](const User& u) { return u.admin; }) == 1) {
    return ReturnNamedError(invocation, "LastAdmin",
                            "the device needs an administrator");
  }
  it->admin = admin != FALSE;
  if (!SaveDb(users, LoadShadow()) || !Merge(false)) {
    return ReturnNamedError(invocation, "Failed",
                            "cannot write the user database");
  }
  NICKEL_INFO("%s made %s %s", caller->name.c_str(), name,
              admin ? "an administrator" : "a standard user");
  ReloadBusConfig();
  g_dbus_method_invocation_return_value(invocation, nullptr);
}

void HandleRemoveUser(GDBusMethodInvocation* invocation, GVariant* params) {
  const gchar *password, *name;
  g_variant_get(params, "(&s&s)", &password, &name);
  auto users = LoadUsers();
  auto caller = AdminCaller(invocation, users, password);
  if (!caller) {
    return;
  }
  auto it = FindUser(users, name);
  if (it == users.end()) {
    return ReturnNamedError(invocation, "NoSuchUser", "no such user");
  }
  if (it->uid == caller->uid) {
    return ReturnNamedError(invocation, "NotAllowed",
                            "an account cannot remove itself");
  }
  users.erase(it);
  auto hashes = LoadShadow();
  hashes.erase(name);
  if (!SaveDb(users, hashes) || !Merge(false)) {
    return ReturnNamedError(invocation, "Failed",
                            "cannot write the user database");
  }
  NICKEL_INFO("%s removed user %s", caller->name.c_str(), name);
  ReloadBusConfig();
  g_dbus_method_invocation_return_value(invocation, nullptr);
}

void OnMethodCall(GDBusConnection*,
                  const gchar*,
                  const gchar*,
                  const gchar*,
                  const gchar* method,
                  GVariant* params,
                  GDBusMethodInvocation* invocation,
                  gpointer) {
  if (g_strcmp0(method, "CreateUser") == 0) {
    return HandleCreateUser(invocation, params);
  }
  if (g_strcmp0(method, "DeleteUser") == 0) {
    return HandleDeleteUser(invocation, params);
  }
  if (g_strcmp0(method, "ListUsers") == 0) {
    return HandleListUsers(invocation);
  }
  if (g_strcmp0(method, "SetPassword") == 0) {
    return HandleSetPassword(invocation, params);
  }
  if (g_strcmp0(method, "SetAdmin") == 0) {
    return HandleSetAdmin(invocation, params);
  }
  if (g_strcmp0(method, "RemoveUser") == 0) {
    return HandleRemoveUser(invocation, params);
  }
  g_dbus_method_invocation_return_error(invocation, G_DBUS_ERROR,
                                        G_DBUS_ERROR_UNKNOWN_METHOD,
                                        "unknown method %s", method);
}

void OnNameLost(GDBusConnection*, const gchar* name, gpointer loop) {
  NICKEL_WARN("lost or could not own %s", name);
  g_main_loop_quit(static_cast<GMainLoop*>(loop));
}

}  // namespace

int main(int argc, char** argv) {
  openlog("nickel-accountsd", LOG_PID, LOG_AUTHPRIV);
  if (argc == 2 && g_strcmp0(argv[1], "--merge") == 0) {
    if (!Merge(true)) {
      NICKEL_WARN("cannot write the merged account files");
      return 1;
    }
    return 0;
  }
  // The same again while they are bind-mounted over /etc's (in place, so
  // the mounts keep showing them): after nickel-accounts-setup --gentoo
  // changed the *.base files (a Gentoo package added a system account).
  if (argc == 2 && g_strcmp0(argv[1], "--remerge") == 0) {
    if (!Merge(false)) {
      NICKEL_WARN("cannot rewrite the merged account files");
      return 1;
    }
    return 0;
  }

  GError* error = nullptr;
  g_bus = g_bus_get_sync(G_BUS_TYPE_SYSTEM, nullptr, &error);
  if (!g_bus) {
    NICKEL_WARN("no system bus: %s", error->message);
    return 1;
  }
  GMainLoop* loop = g_main_loop_new(nullptr, FALSE);
  GDBusNodeInfo* node = g_dbus_node_info_new_for_xml(kIntrospection, nullptr);
  static const GDBusInterfaceVTable kVTable = {OnMethodCall, nullptr, nullptr};
  if (!g_dbus_connection_register_object(g_bus, kObjectPath,
                                         node->interfaces[0], &kVTable,
                                         nullptr, nullptr, &error)) {
    NICKEL_WARN("cannot export %s: %s", kObjectPath, error->message);
    return 1;
  }
  g_bus_own_name_on_connection(g_bus, kServiceName,
                               G_BUS_NAME_OWNER_FLAGS_NONE, nullptr,
                               OnNameLost, loop, nullptr);
  g_main_loop_run(loop);
  return 0;
}
