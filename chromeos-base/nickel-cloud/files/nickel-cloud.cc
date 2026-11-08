// Distributed under the terms of the GNU General Public License v2
//
// nickel-cloud: cloud storage in Files, through rclone (rclone.org): Google
// Drive, OneDrive, Dropbox, Proton Drive, S3, WebDAV/Nextcloud and the rest
// of rclone's providers. Runs in the user's session as the user
// (nickel-session-exec, upstart nickel-cloud.conf).
//
// rclone is not in the image (86 MB, wanted by some users only): the first
// account the user adds downloads the release pinned here from
// downloads.rclone.org, checked against its SHA-256, into
// ~/.local/share/nickel-cloud/. Accounts are rclone remotes in the user's
// rclone.conf (~/.config/rclone/rclone.conf: rclone on the command line sees
// them too), named as the user names them.
//
// One "rclone rcd" serves everything, on $XDG_RUNTIME_DIR/nickel-cloud/rc.sock
// (the user's own directory: no other user reaches it, so no password). It
// mounts each account at $XDG_RUNTIME_DIR/nickel-cloud/drives/<name> with a
// full VFS cache (files open as local ones do; changes upload in the
// background; OneDrive's Personal Vault, which no app outside Microsoft's
// can open, left out), and runs rclone's configuration, whose questions Settings
// shows; a sign-in opens the browser (rclone runs xdg-open, ChromeOS's
// browser) and waits for it. The mounts are outside the home on purpose:
// the home is a bind mount of the vault apart from the one Chrome reads, and
// a mount under one is not seen under the other. Files shows each mounted
// account as a volume of its own (Chrome patch 0050). rclone runs only while
// there are accounts.
//
// D-Bus, on the session bus: org.nickelos.Cloud at /org/nickelos/Cloud
//   GetState() -> s            JSON {"rclone": "missing" | "downloading" |
//                              "ready" | "failed", "error", "adding",
//                              "accounts": [{"name", "type", "mounted",
//                              "mountPath", "error"}]}
//   Add(s name, s type, s parameters) -> s
//   Continue(s name, s state, s result) -> s
//                              rclone's config/create and config/update,
//                              non-interactive (parameters: a JSON object of
//                              rclone options; passwords are obscured here).
//                              JSON {"done": b, "state", "option": rclone's
//                              next question, "error"}. A sign-in through the
//                              browser answers when the browser returns.
//   Cancel(s name)             ends an Add in progress; the half-made account
//                              goes
//   Remove(s name)             unmounts the account, deletes it and its cache;
//                              waits for changes still uploading, and fails
//                              if they don't finish in kUploadWaitSeconds
//   Changed()                  signal: GetState again

#include <gio/gio.h>
#include <gio/gunixsocketaddress.h>
#include <glib-unix.h>
#include <json-glib/json-glib.h>
#include <signal.h>
#include <sys/stat.h>
#include <unistd.h>

#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr char kRcloneVersion[] = "v1.75.2";
constexpr char kRcloneSha256[] =
    "349ac8fba6ff65d6247043f1750cdcb518ec5d500ef91463a10d37c0ccdf3702";
constexpr char kRcloneUrl[] =
    "https://downloads.rclone.org/v1.75.2/rclone-v1.75.2-linux-amd64.zip";
constexpr char kRcloneZipMember[] = "rclone-v1.75.2-linux-amd64/rclone";

constexpr char kBusName[] = "org.nickelos.Cloud";
constexpr char kObjectPath[] = "/org/nickelos/Cloud";
constexpr char kInterface[] = "org.nickelos.Cloud";
constexpr char kIntrospection[] =
    "<node>"
    "  <interface name='org.nickelos.Cloud'>"
    "    <method name='GetState'>"
    "      <arg type='s' name='state' direction='out'/>"
    "    </method>"
    "    <method name='Add'>"
    "      <arg type='s' name='name' direction='in'/>"
    "      <arg type='s' name='type' direction='in'/>"
    "      <arg type='s' name='parameters' direction='in'/>"
    "      <arg type='s' name='result' direction='out'/>"
    "    </method>"
    "    <method name='Continue'>"
    "      <arg type='s' name='name' direction='in'/>"
    "      <arg type='s' name='state' direction='in'/>"
    "      <arg type='s' name='answer' direction='in'/>"
    "      <arg type='s' name='result' direction='out'/>"
    "    </method>"
    "    <method name='Cancel'>"
    "      <arg type='s' name='name' direction='in'/>"
    "    </method>"
    "    <method name='Remove'>"
    "      <arg type='s' name='name' direction='in'/>"
    "    </method>"
    "    <signal name='Changed'/>"
    "  </interface>"
    "</node>";

// A failed mount is tried again this often (no network at sign-in, a server
// down for a while).
constexpr guint kRemountSeconds = 120;
// How long Remove waits for an account's changes to finish uploading (the
// client waits 60 s for Remove).
constexpr int kUploadWaitSeconds = 30;

enum class Rclone { kMissing, kDownloading, kReady, kFailed };

struct Account {
  std::string name;
  std::string type;
};

using Done = std::function<void()>;
using RcDone = std::function<void(JsonNode* body, const std::string& error)>;

struct State {
  GDBusConnection* session = nullptr;
  Rclone rclone = Rclone::kMissing;
  std::string rclone_error;
  std::string data_dir;     // ~/.local/share/nickel-cloud
  std::string rclone_path;  // data_dir/rclone-<version>/rclone
  std::string config_path;  // ~/.config/rclone/rclone.conf
  std::string runtime_dir;  // $XDG_RUNTIME_DIR/nickel-cloud
  std::string socket_path;
  std::string drives_dir;
  GSubprocess* rcd = nullptr;
  bool rcd_ready = false;
  bool rcd_starting = false;
  std::vector<Done> waiting;  // for rclone and rcd
  std::vector<Account> accounts;
  std::set<std::string> mounted;
  std::map<std::string, std::string> mount_errors;
  std::string adding;
  GCancellable* add_cancel = nullptr;
  std::string discarding;  // cancelled while being added, not deleted yet
  bool quitting = false;
  GMainLoop* loop = nullptr;
};

State g;

void Log(const std::string& message) {
  g_message("%s", message.c_str());
}

std::string MountPath(const std::string& name) {
  return g.drives_dir + "/" + name;
}

void EmitChanged() {
  if (g.session) {
    g_dbus_connection_emit_signal(g.session, nullptr, kObjectPath, kInterface,
                                  "Changed", nullptr, nullptr);
  }
}

std::string ToJson(JsonNode* node) {
  g_autoptr(JsonGenerator) generator = json_generator_new();
  json_generator_set_root(generator, node);
  g_autofree gchar* text = json_generator_to_data(generator, nullptr);
  return text;
}

// rclone's names: no colon or slash, no space at either end.
bool ValidName(const std::string& name) {
  return !name.empty() && name.size() <= 100 &&
         name.find_first_of(":/\\\n\r\t") == std::string::npos &&
         name.front() != ' ' && name.back() != ' ' && name.front() != '-';
}

// The remotes in rclone.conf, without rclone (it may not be here yet).
std::vector<std::string> ConfiguredNames() {
  std::vector<std::string> names;
  g_autoptr(GKeyFile) file = g_key_file_new();
  if (!g_key_file_load_from_file(file, g.config_path.c_str(), G_KEY_FILE_NONE,
                                 nullptr)) {
    return names;
  }
  gsize count = 0;
  g_auto(GStrv) groups = g_key_file_get_groups(file, &count);
  for (gsize i = 0; i < count; ++i) {
    names.emplace_back(groups[i]);
  }
  return names;
}

// ---------------------------------------------------------------------------
// rclone's remote control: HTTP/1.0 (no chunked answers) over its socket.

struct RcCall {
  std::string request;
  std::string response;
  RcDone done;
  GCancellable* cancellable = nullptr;
  GSocketConnection* connection = nullptr;
};

void RcFinish(RcCall* call, const std::string& error) {
  std::unique_ptr<RcCall> owned(call);
  if (call->connection) {
    g_object_unref(call->connection);
  }
  if (call->cancellable) {
    g_object_unref(call->cancellable);
  }
  if (!error.empty()) {
    call->done(nullptr, error);
    return;
  }
  const std::string& response = call->response;
  const size_t end = response.find("\r\n\r\n");
  int status = 0;
  if (response.compare(0, 5, "HTTP/") == 0) {
    const size_t space = response.find(' ');
    if (space != std::string::npos) {
      status = atoi(response.c_str() + space + 1);
    }
  }
  if (end == std::string::npos || status == 0) {
    call->done(nullptr, "rclone gave no answer");
    return;
  }
  g_autoptr(JsonParser) parser = json_parser_new();
  const std::string body = response.substr(end + 4);
  if (!json_parser_load_from_data(parser, body.c_str(), body.size(), nullptr)) {
    call->done(nullptr, "rclone's answer is not JSON");
    return;
  }
  JsonNode* root = json_parser_get_root(parser);
  if (status != 200) {
    std::string message = "rclone: error " + std::to_string(status);
    if (JSON_NODE_HOLDS_OBJECT(root)) {
      const char* text = json_object_get_string_member_with_default(
          json_node_get_object(root), "error", nullptr);
      if (text) {
        message = text;
      }
    }
    call->done(nullptr, message);
    return;
  }
  call->done(root, "");
}

void OnRcRead(GObject* source, GAsyncResult* result, gpointer data) {
  auto* call = static_cast<RcCall*>(data);
  g_autoptr(GError) error = nullptr;
  g_autoptr(GBytes) bytes = g_input_stream_read_bytes_finish(
      G_INPUT_STREAM(source), result, &error);
  if (!bytes) {
    RcFinish(call, error->message);
    return;
  }
  gsize size = 0;
  const char* chunk = static_cast<const char*>(g_bytes_get_data(bytes, &size));
  if (size == 0) {
    RcFinish(call, "");
    return;
  }
  call->response.append(chunk, size);
  g_input_stream_read_bytes_async(
      g_io_stream_get_input_stream(G_IO_STREAM(call->connection)), 65536,
      G_PRIORITY_DEFAULT, call->cancellable, OnRcRead, call);
}

void OnRcWritten(GObject* source, GAsyncResult* result, gpointer data) {
  auto* call = static_cast<RcCall*>(data);
  g_autoptr(GError) error = nullptr;
  if (!g_output_stream_write_all_finish(G_OUTPUT_STREAM(source), result,
                                        nullptr, &error)) {
    RcFinish(call, error->message);
    return;
  }
  g_input_stream_read_bytes_async(
      g_io_stream_get_input_stream(G_IO_STREAM(call->connection)), 65536,
      G_PRIORITY_DEFAULT, call->cancellable, OnRcRead, call);
}

void OnRcConnected(GObject* source, GAsyncResult* result, gpointer data) {
  auto* call = static_cast<RcCall*>(data);
  g_autoptr(GError) error = nullptr;
  call->connection = g_socket_client_connect_finish(G_SOCKET_CLIENT(source),
                                                    result, &error);
  g_object_unref(source);
  if (!call->connection) {
    RcFinish(call, error->message);
    return;
  }
  g_output_stream_write_all_async(
      g_io_stream_get_output_stream(G_IO_STREAM(call->connection)),
      call->request.data(), call->request.size(), G_PRIORITY_DEFAULT,
      call->cancellable, OnRcWritten, call);
}

// Calls rclone's remote control METHOD (e.g. "config/create") with a JSON
// object of parameters, which this takes.
void Rc(const std::string& method,
        JsonObject* parameters,
        GCancellable* cancellable,
        RcDone done) {
  g_autoptr(JsonNode) node = json_node_new(JSON_NODE_OBJECT);
  json_node_take_object(node, parameters);
  const std::string body = ToJson(node);
  auto* call = new RcCall;
  call->request = "POST /" + method +
                  " HTTP/1.0\r\nHost: localhost\r\n"
                  "Content-Type: application/json\r\nContent-Length: " +
                  std::to_string(body.size()) + "\r\n\r\n" + body;
  call->done = std::move(done);
  call->cancellable =
      cancellable ? G_CANCELLABLE(g_object_ref(cancellable)) : nullptr;
  GSocketClient* client = g_socket_client_new();
  g_autoptr(GSocketAddress) address =
      g_unix_socket_address_new(g.socket_path.c_str());
  g_socket_client_connect_async(client, G_SOCKET_CONNECTABLE(address),
                                call->cancellable, OnRcConnected, call);
}

// ---------------------------------------------------------------------------
// Accounts and their mounts.

void Refresh(Done done);

void MountOne(const std::string& name, Done done) {
  const std::string path = MountPath(name);
  if (g_mkdir_with_parents(path.c_str(), 0700) != 0) {
    g.mount_errors[name] = "cannot make " + path;
    done();
    return;
  }
  JsonObject* parameters = json_object_new();
  json_object_set_string_member(parameters, "fs", (name + ":").c_str());
  json_object_set_string_member(parameters, "mountPoint", path.c_str());
  JsonObject* vfs = json_object_new();
  json_object_set_string_member(vfs, "CacheMode", "full");
  json_object_set_object_member(parameters, "vfsOpt", vfs);
  // OneDrive's Personal Vault, in every personal OneDrive's top folder, is
  // for Microsoft's own apps only: listing it fails ("ObjectHandle is
  // Invalid", rclone issue 8736), so it is left out of the mount. Its name
  // follows the account's language. A filter given with mount/mount stays
  // with that mount.
  for (const Account& account : g.accounts) {
    if (account.name == name && account.type == "onedrive") {
      JsonArray* exclude = json_array_new();
      json_array_add_string_element(exclude, "/Personal Vault/**");
      json_array_add_string_element(exclude, "/Cofre Pessoal/**");
      JsonObject* filter = json_object_new();
      json_object_set_array_member(filter, "ExcludeRule", exclude);
      json_object_set_object_member(parameters, "_filter", filter);
    }
  }
  Rc("mount/mount", parameters, nullptr,
     [name, done](JsonNode*, const std::string& error) {
       if (error.empty()) {
         g.mounted.insert(name);
         g.mount_errors.erase(name);
         Log("mounted " + name);
       } else {
         g.mount_errors[name] = error;
         Log("cannot mount " + name + ": " + error);
       }
       done();
     });
}

// Mounts every account not mounted yet, one after another.
void MountAll(Done done) {
  auto pending = std::make_shared<std::vector<std::string>>();
  for (const Account& account : g.accounts) {
    if (!g.mounted.count(account.name) && account.name != g.adding &&
        account.name != g.discarding) {
      pending->push_back(account.name);
    }
  }
  auto next = std::make_shared<std::function<void()>>();
  *next = [pending, next, done]() {
    if (pending->empty()) {
      EmitChanged();
      done();
      return;
    }
    const std::string name = pending->back();
    pending->pop_back();
    MountOne(name, [next]() { (*next)(); });
  };
  (*next)();
}

// Reads the accounts and the mounts from rclone.
void Refresh(Done done) {
  Rc("config/dump", json_object_new(), nullptr,
     [done](JsonNode* body, const std::string& error) {
       if (!error.empty() || !body || !JSON_NODE_HOLDS_OBJECT(body)) {
         done();
         return;
       }
       g.accounts.clear();
       JsonObject* remotes = json_node_get_object(body);
       g_autoptr(GList) names = json_object_get_members(remotes);
       for (GList* l = names; l; l = l->next) {
         const char* name = static_cast<const char*>(l->data);
         JsonNode* remote = json_object_get_member(remotes, name);
         std::string type;
         if (JSON_NODE_HOLDS_OBJECT(remote)) {
           type = json_object_get_string_member_with_default(
               json_node_get_object(remote), "type", "");
         }
         g.accounts.push_back({name, type});
       }
       Rc("mount/listmounts", json_object_new(), nullptr,
          [done](JsonNode* mounts, const std::string& error) {
            g.mounted.clear();
            if (error.empty() && mounts && JSON_NODE_HOLDS_OBJECT(mounts)) {
              JsonArray* list = json_object_get_array_member(
                  json_node_get_object(mounts), "mountPoints");
              const guint count = list ? json_array_get_length(list) : 0;
              for (guint i = 0; i < count; ++i) {
                JsonObject* mount = json_array_get_object_element(list, i);
                std::string fs =
                    json_object_get_string_member_with_default(mount, "Fs", "");
                if (!fs.empty() && fs.back() == ':') {
                  fs.pop_back();
                }
                g.mounted.insert(fs);
              }
            }
            EmitChanged();
            done();
          });
     });
}

// ---------------------------------------------------------------------------
// rclone itself: download, and its remote control daemon.

void RunWaiting() {
  std::vector<Done> waiting = std::move(g.waiting);
  g.waiting.clear();
  for (Done& done : waiting) {
    done();
  }
}

void StartRcd();

// Mounts left under drives/ by an rclone that ended without unmounting
// (killed, crashed): dead ("Transport endpoint is not connected") until
// unmounted, and a new mount would stack on them.
void UnmountLeftovers() {
  g_autofree gchar* mountinfo = nullptr;
  if (!g_file_get_contents("/proc/self/mountinfo", &mountinfo, nullptr,
                           nullptr)) {
    return;
  }
  const std::string prefix = g.drives_dir + "/";
  g_auto(GStrv) lines = g_strsplit(mountinfo, "\n", -1);
  for (gchar** line = lines; *line; ++line) {
    g_auto(GStrv) fields = g_strsplit(*line, " ", 6);
    if (g_strv_length(fields) < 5) {
      continue;
    }
    // Field 5 is the mount point, with spaces as \040.
    g_autofree gchar* target = g_strcompress(fields[4]);
    if (!g_str_has_prefix(target, prefix.c_str())) {
      continue;
    }
    GSubprocess* umount = g_subprocess_new(
        G_SUBPROCESS_FLAGS_STDERR_SILENCE, nullptr, "fusermount3", "-u", "-z",
        target, nullptr);
    if (umount) {
      g_subprocess_wait(umount, nullptr, nullptr);
      g_object_unref(umount);
    }
    Log(std::string("unmounted a leftover mount at ") + target);
  }
}

void OnRcdExited(GObject* source, GAsyncResult* result, gpointer) {
  g_subprocess_wait_finish(G_SUBPROCESS(source), result, nullptr);
  if (G_SUBPROCESS(source) != g.rcd) {
    g_object_unref(source);
    return;
  }
  g_clear_object(&g.rcd);
  g.rcd_ready = false;
  g.mounted.clear();
  if (g.quitting) {
    g_main_loop_quit(g.loop);
    return;
  }
  Log("rclone stopped; starting it again");
  EmitChanged();
  if (!ConfiguredNames().empty()) {
    g_timeout_add_seconds(
        5,
        [](gpointer) -> gboolean {
          StartRcd();
          return G_SOURCE_REMOVE;
        },
        nullptr);
  }
}

// Waits for the socket to answer, then mounts the accounts.
void WaitForRcd(int tries) {
  Rc("rc/noop", json_object_new(), nullptr,
     [tries](JsonNode*, const std::string& error) {
       if (!error.empty()) {
         if (tries > 50 || !g.rcd) {
           Log("rclone's remote control did not answer: " + error);
           g.rcd_starting = false;
           return;
         }
         g_timeout_add_once(
             200,
             [](gpointer data) {
               WaitForRcd(GPOINTER_TO_INT(data));
             },
             GINT_TO_POINTER(tries + 1));
         return;
       }
       g.rcd_ready = true;
       g.rcd_starting = false;
       Refresh([]() { MountAll([]() { RunWaiting(); }); });
     });
}

void StartRcd() {
  if (g.rcd || g.rclone != Rclone::kReady) {
    return;
  }
  g_mkdir_with_parents(g.runtime_dir.c_str(), 0700);
  g_mkdir_with_parents(g.drives_dir.c_str(), 0700);
  UnmountLeftovers();
  unlink(g.socket_path.c_str());
  const std::string address = "unix://" + g.socket_path;
  const std::string log = g.runtime_dir + "/rclone.log";
  g_autoptr(GError) error = nullptr;
  // In a process group of its own: when the session ends, upstart signals
  // this job's group, and rclone must not end before this has unmounted
  // through it (killed first, it left dead mounts).
  g_autoptr(GSubprocessLauncher) launcher =
      g_subprocess_launcher_new(G_SUBPROCESS_FLAGS_NONE);
  g_subprocess_launcher_set_child_setup(
      launcher, [](gpointer) { setpgid(0, 0); }, nullptr, nullptr);
  g.rcd = g_subprocess_launcher_spawn(
      launcher, &error, g.rclone_path.c_str(), "rcd", "--rc-addr",
      address.c_str(), "--rc-no-auth", "--config", g.config_path.c_str(),
      "--log-file", log.c_str(), "--log-level", "INFO", nullptr);
  if (!g.rcd) {
    Log(std::string("cannot start rclone: ") + error->message);
    return;
  }
  g.rcd_starting = true;
  g_subprocess_wait_async(g.rcd, nullptr, OnRcdExited, nullptr);
  WaitForRcd(0);
}

void SetRcloneFailed(const std::string& error) {
  g.rclone = Rclone::kFailed;
  g.rclone_error = error;
  Log("rclone: " + error);
  EmitChanged();
  RunWaiting();  // they see the failure
}

// The download, its checksum, then the binary out of the zip.
void DownloadRclone() {
  if (g.rclone == Rclone::kDownloading) {
    return;
  }
  g.rclone = Rclone::kDownloading;
  g.rclone_error.clear();
  EmitChanged();
  g_mkdir_with_parents(g.data_dir.c_str(), 0700);
  const std::string zip = g.data_dir + "/rclone.zip.part";
  g_autoptr(GError) error = nullptr;
  GSubprocess* curl = g_subprocess_new(
      G_SUBPROCESS_FLAGS_STDERR_SILENCE, &error, "curl", "-fsSL", "--retry",
      "3", "-o", zip.c_str(), kRcloneUrl, nullptr);
  if (!curl) {
    SetRcloneFailed(std::string("cannot run curl: ") + error->message);
    return;
  }
  g_subprocess_wait_check_async(
      curl, nullptr,
      [](GObject* source, GAsyncResult* result, gpointer) {
        g_autoptr(GError) error = nullptr;
        const bool ok = g_subprocess_wait_check_finish(G_SUBPROCESS(source),
                                                       result, &error);
        g_object_unref(source);
        const std::string zip = g.data_dir + "/rclone.zip.part";
        if (!ok) {
          unlink(zip.c_str());
          SetRcloneFailed("cannot download rclone (no network?)");
          return;
        }
        g_autoptr(GMappedFile) file =
            g_mapped_file_new(zip.c_str(), FALSE, nullptr);
        g_autofree gchar* sum =
            file ? g_compute_checksum_for_data(
                       G_CHECKSUM_SHA256,
                       reinterpret_cast<const guchar*>(
                           g_mapped_file_get_contents(file)),
                       g_mapped_file_get_length(file))
                 : nullptr;
        if (!sum || std::string(sum) != kRcloneSha256) {
          unlink(zip.c_str());
          SetRcloneFailed("the rclone download did not match its checksum");
          return;
        }
        const std::string dir = g.data_dir + "/rclone-" + kRcloneVersion;
        const std::string part = dir + ".part";
        GSubprocess* rm = g_subprocess_new(G_SUBPROCESS_FLAGS_NONE, nullptr,
                                           "rm", "-rf", part.c_str(), nullptr);
        if (rm) {
          g_subprocess_wait(rm, nullptr, nullptr);
          g_object_unref(rm);
        }
        g_mkdir_with_parents(part.c_str(), 0700);
        g_autoptr(GError) unzip_error = nullptr;
        GSubprocess* unzip = g_subprocess_new(
            G_SUBPROCESS_FLAGS_STDERR_SILENCE, &unzip_error, "bsdtar", "-xf",
            zip.c_str(), "-C", part.c_str(), "--strip-components", "1",
            kRcloneZipMember, nullptr);
        const bool unzipped =
            unzip && g_subprocess_wait_check(unzip, nullptr, nullptr);
        if (unzip) {
          g_object_unref(unzip);
        }
        unlink(zip.c_str());
        const std::string binary = part + "/rclone";
        if (!unzipped || chmod(binary.c_str(), 0755) != 0 ||
            rename(part.c_str(), dir.c_str()) != 0) {
          SetRcloneFailed("cannot unpack rclone");
          return;
        }
        g.rclone = Rclone::kReady;
        Log(std::string("rclone ") + kRcloneVersion + " installed");
        EmitChanged();
        StartRcd();
        if (!g.rcd_starting) {
          RunWaiting();
        }
      },
      nullptr);
}

// Runs DONE once rclone is here and its daemon answers (downloading and
// starting them as needed), or right away if that failed (the caller looks
// at g.rclone).
void WithRclone(Done done) {
  if (g.rclone == Rclone::kReady && g.rcd_ready) {
    done();
    return;
  }
  g.waiting.push_back(std::move(done));
  if (g.rclone == Rclone::kMissing || g.rclone == Rclone::kFailed) {
    DownloadRclone();
  } else if (g.rclone == Rclone::kReady && !g.rcd && !g.rcd_starting) {
    StartRcd();
  }
}

// ---------------------------------------------------------------------------
// D-Bus.

std::string StateJson() {
  g_autoptr(JsonBuilder) builder = json_builder_new();
  json_builder_begin_object(builder);
  json_builder_set_member_name(builder, "rclone");
  switch (g.rclone) {
    case Rclone::kMissing:
      json_builder_add_string_value(builder, "missing");
      break;
    case Rclone::kDownloading:
      json_builder_add_string_value(builder, "downloading");
      break;
    case Rclone::kReady:
      json_builder_add_string_value(builder, "ready");
      break;
    case Rclone::kFailed:
      json_builder_add_string_value(builder, "failed");
      break;
  }
  json_builder_set_member_name(builder, "error");
  json_builder_add_string_value(builder, g.rclone_error.c_str());
  json_builder_set_member_name(builder, "adding");
  json_builder_add_string_value(builder, g.adding.c_str());
  json_builder_set_member_name(builder, "accounts");
  json_builder_begin_array(builder);
  for (const Account& account : g.accounts) {
    if (account.name == g.adding || account.name == g.discarding) {
      continue;
    }
    json_builder_begin_object(builder);
    json_builder_set_member_name(builder, "name");
    json_builder_add_string_value(builder, account.name.c_str());
    json_builder_set_member_name(builder, "type");
    json_builder_add_string_value(builder, account.type.c_str());
    json_builder_set_member_name(builder, "mounted");
    json_builder_add_boolean_value(builder, g.mounted.count(account.name) > 0);
    json_builder_set_member_name(builder, "mountPath");
    json_builder_add_string_value(builder, MountPath(account.name).c_str());
    json_builder_set_member_name(builder, "error");
    const auto it = g.mount_errors.find(account.name);
    json_builder_add_string_value(
        builder, it == g.mount_errors.end() ? "" : it->second.c_str());
    json_builder_end_object(builder);
  }
  json_builder_end_array(builder);
  json_builder_end_object(builder);
  g_autoptr(JsonNode) root = json_builder_get_root(builder);
  return ToJson(root);
}

void Reply(GDBusMethodInvocation* invocation, const std::string& text) {
  g_dbus_method_invocation_return_value(invocation,
                                        g_variant_new("(s)", text.c_str()));
}

std::string ErrorJson(const std::string& error) {
  g_autoptr(JsonBuilder) builder = json_builder_new();
  json_builder_begin_object(builder);
  json_builder_set_member_name(builder, "done");
  json_builder_add_boolean_value(builder, FALSE);
  json_builder_set_member_name(builder, "error");
  json_builder_add_string_value(builder, error.c_str());
  json_builder_end_object(builder);
  g_autoptr(JsonNode) root = json_builder_get_root(builder);
  return ToJson(root);
}

void EndAdding() {
  g.adding.clear();
  g_clear_object(&g.add_cancel);
}

// The answer to Add and Continue, from rclone's config state
// ({"State", "Option", "Error", "Result"}): done when no state is left, and
// then the account is mounted.
void AnswerConfig(GDBusMethodInvocation* invocation,
                  const std::string& name,
                  JsonNode* body,
                  const std::string& error) {
  if (!error.empty()) {
    if (name == g.adding) {
      // Whatever rclone half-made goes.
      JsonObject* parameters = json_object_new();
      json_object_set_string_member(parameters, "name", name.c_str());
      Rc("config/delete", parameters, nullptr,
         [](JsonNode*, const std::string&) { Refresh([]() {}); });
      EndAdding();
    }
    Reply(invocation, ErrorJson(error));
    return;
  }
  JsonObject* config = body && JSON_NODE_HOLDS_OBJECT(body)
                           ? json_node_get_object(body)
                           : nullptr;
  const std::string state =
      config ? json_object_get_string_member_with_default(config, "State", "")
             : "";
  const std::string config_error =
      config ? json_object_get_string_member_with_default(config, "Error", "")
             : "";
  g_autoptr(JsonBuilder) builder = json_builder_new();
  json_builder_begin_object(builder);
  json_builder_set_member_name(builder, "done");
  json_builder_add_boolean_value(builder, state.empty());
  json_builder_set_member_name(builder, "state");
  json_builder_add_string_value(builder, state.c_str());
  json_builder_set_member_name(builder, "error");
  json_builder_add_string_value(builder, config_error.c_str());
  if (config && json_object_has_member(config, "Option") &&
      !json_object_get_null_member(config, "Option")) {
    json_builder_set_member_name(builder, "option");
    json_builder_add_value(
        builder, json_node_copy(json_object_get_member(config, "Option")));
  }
  json_builder_end_object(builder);
  g_autoptr(JsonNode) root = json_builder_get_root(builder);
  const std::string text = ToJson(root);
  if (!state.empty()) {
    Reply(invocation, text);
    return;
  }
  // Done: mount it before answering, so Files has it when Settings closes.
  EndAdding();
  g_object_ref(invocation);
  Refresh([invocation, name, text]() {
    MountOne(name, [invocation, text]() {
      EmitChanged();
      Reply(invocation, text);
      g_object_unref(invocation);
    });
  });
}

void HandleAdd(GDBusMethodInvocation* invocation,
               const std::string& name,
               const std::string& type,
               const std::string& parameters_json) {
  if (!g.adding.empty()) {
    Reply(invocation, ErrorJson("another account is being added"));
    return;
  }
  if (!ValidName(name)) {
    Reply(invocation,
          ErrorJson("a name can't be empty, start or end with a space, or "
                    "contain : or /"));
    return;
  }
  for (const std::string& existing : ConfiguredNames()) {
    if (existing == name) {
      Reply(invocation, ErrorJson("an account with that name exists"));
      return;
    }
  }
  g_autoptr(JsonParser) parser = json_parser_new();
  if (!json_parser_load_from_data(parser, parameters_json.c_str(), -1,
                                  nullptr) ||
      !JSON_NODE_HOLDS_OBJECT(json_parser_get_root(parser))) {
    Reply(invocation, ErrorJson("bad parameters"));
    return;
  }
  JsonObject* parameters =
      json_object_ref(json_node_get_object(json_parser_get_root(parser)));
  g.adding = name;
  g.add_cancel = g_cancellable_new();
  EmitChanged();
  g_object_ref(invocation);
  WithRclone([invocation, name, type, parameters]() {
    if (g.rclone != Rclone::kReady || !g.rcd_ready) {
      json_object_unref(parameters);
      EndAdding();
      Reply(invocation, ErrorJson(g.rclone_error.empty()
                                      ? "rclone did not start"
                                      : g.rclone_error));
      g_object_unref(invocation);
      return;
    }
    JsonObject* call = json_object_new();
    json_object_set_string_member(call, "name", name.c_str());
    json_object_set_string_member(call, "type", type.c_str());
    json_object_set_object_member(call, "parameters", parameters);
    JsonObject* opt = json_object_new();
    json_object_set_boolean_member(opt, "nonInteractive", TRUE);
    json_object_set_boolean_member(opt, "obscure", TRUE);
    json_object_set_object_member(call, "opt", opt);
    Rc("config/create", call, g.add_cancel,
       [invocation, name](JsonNode* body, const std::string& error) {
         AnswerConfig(invocation, name, body, error);
         g_object_unref(invocation);
       });
  });
}

void HandleContinue(GDBusMethodInvocation* invocation,
                    const std::string& name,
                    const std::string& state,
                    const std::string& answer) {
  if (name.empty() || name != g.adding) {
    Reply(invocation, ErrorJson("that account is not being added"));
    return;
  }
  JsonObject* call = json_object_new();
  json_object_set_string_member(call, "name", name.c_str());
  json_object_set_object_member(call, "parameters", json_object_new());
  JsonObject* opt = json_object_new();
  json_object_set_boolean_member(opt, "nonInteractive", TRUE);
  json_object_set_boolean_member(opt, "obscure", TRUE);
  json_object_set_boolean_member(opt, "continue", TRUE);
  json_object_set_string_member(opt, "state", state.c_str());
  json_object_set_string_member(opt, "result", answer.c_str());
  json_object_set_object_member(call, "opt", opt);
  g_object_ref(invocation);
  Rc("config/update", call, g.add_cancel,
     [invocation, name](JsonNode* body, const std::string& error) {
       AnswerConfig(invocation, name, body, error);
       g_object_unref(invocation);
     });
}

// A sign-in in progress keeps rclone's sign-in server waiting: rclone
// starts again to end it, then the half-made account goes.
void HandleCancel(const std::string& name) {
  if (name.empty() || name != g.adding) {
    return;
  }
  if (g.add_cancel) {
    g_cancellable_cancel(g.add_cancel);
  }
  EndAdding();
  g.discarding = name;
  auto remove = [name]() {
    JsonObject* parameters = json_object_new();
    json_object_set_string_member(parameters, "name", name.c_str());
    Rc("config/delete", parameters, nullptr,
       [name](JsonNode*, const std::string&) {
         if (g.discarding == name) {
           g.discarding.clear();
         }
         g.mount_errors.erase(name);
         Refresh([]() {});
       });
  };
  if (g.rcd) {
    g.waiting.push_back(remove);
    g_subprocess_send_signal(g.rcd, SIGTERM);  // OnRcdExited starts it again
    if (ConfiguredNames().empty()) {
      g_timeout_add_seconds(
          1,
          [](gpointer) -> gboolean {
            StartRcd();
            return G_SOURCE_REMOVE;
          },
          nullptr);
    }
  }
  EmitChanged();
}

// Runs DONE(true) once nothing of NAME's waits to upload (rclone's vfs/stats;
// an account that isn't mounted has nothing), or DONE(false) after TRIES more
// seconds of uploads.
void WaitForUploads(const std::string& name,
                    int tries,
                    std::function<void(bool)> done) {
  JsonObject* parameters = json_object_new();
  json_object_set_string_member(parameters, "fs", (name + ":").c_str());
  Rc("vfs/stats", parameters, nullptr,
     [name, tries, done](JsonNode* body, const std::string& error) {
       gint64 pending = 0;
       if (error.empty() && body && JSON_NODE_HOLDS_OBJECT(body)) {
         JsonObject* stats = json_node_get_object(body);
         JsonNode* cache = json_object_get_member(stats, "diskCache");
         if (cache && JSON_NODE_HOLDS_OBJECT(cache)) {
           JsonObject* disk = json_node_get_object(cache);
           pending = json_object_get_int_member_with_default(
                         disk, "uploadsInProgress", 0) +
                     json_object_get_int_member_with_default(
                         disk, "uploadsQueued", 0);
         }
       }
       if (pending == 0) {
         done(true);
         return;
       }
       if (tries <= 0) {
         done(false);
         return;
       }
       Log(name + ": " + std::to_string(pending) + " uploads to finish");
       auto* next = new std::function<void()>(
           [name, tries, done]() { WaitForUploads(name, tries - 1, done); });
       g_timeout_add_seconds(
           1,
           [](gpointer data) -> gboolean {
             auto* next = static_cast<std::function<void()>*>(data);
             (*next)();
             delete next;
             return G_SOURCE_REMOVE;
           },
           next);
     });
}

// Unmounts NAME and deletes it and its cache, then answers INVOCATION.
void RemoveAccount(GDBusMethodInvocation* invocation, const std::string& name) {
  JsonObject* parameters = json_object_new();
  json_object_set_string_member(parameters, "mountPoint",
                                MountPath(name).c_str());
  Rc("mount/unmount", parameters, nullptr,
     [invocation, name](JsonNode*, const std::string&) {
       JsonObject* parameters = json_object_new();
       json_object_set_string_member(parameters, "name", name.c_str());
       Rc("config/delete", parameters, nullptr,
          [invocation, name](JsonNode*, const std::string& error) {
            g.mounted.erase(name);
            g.mount_errors.erase(name);
            rmdir(MountPath(name).c_str());
            // Its cache, in the user's home.
            const std::string cache =
                std::string(g_get_user_cache_dir()) + "/rclone";
            const std::string vfs = cache + "/vfs/" + name;
            const std::string meta = cache + "/vfsMeta/" + name;
            GSubprocess* rm =
                g_subprocess_new(G_SUBPROCESS_FLAGS_NONE, nullptr, "rm",
                                 "-rf", "--", vfs.c_str(), meta.c_str(),
                                 nullptr);
            if (rm) {
              g_subprocess_wait(rm, nullptr, nullptr);
              g_object_unref(rm);
            }
            Log("removed " + name);
            Refresh([invocation, error]() {
              if (error.empty()) {
                g_dbus_method_invocation_return_value(invocation, nullptr);
              } else {
                g_dbus_method_invocation_return_dbus_error(
                    invocation, "org.nickelos.Cloud.Error", error.c_str());
              }
              g_object_unref(invocation);
            });
          });
     });
}

void HandleRemove(GDBusMethodInvocation* invocation, const std::string& name) {
  g_object_ref(invocation);
  WithRclone([invocation, name]() {
    // Its cache goes too: changes still uploading would be lost.
    WaitForUploads(name, kUploadWaitSeconds, [invocation, name](bool done) {
      if (done) {
        RemoveAccount(invocation, name);
        return;
      }
      const std::string message =
          "Changes in " + name +
          " are still uploading. Try again when they're done.";
      g_dbus_method_invocation_return_dbus_error(
          invocation, "org.nickelos.Cloud.Error", message.c_str());
      g_object_unref(invocation);
    });
  });
}

void OnMethodCall(GDBusConnection*,
                  const gchar*,
                  const gchar*,
                  const gchar*,
                  const gchar* method,
                  GVariant* parameters,
                  GDBusMethodInvocation* invocation,
                  gpointer) {
  const std::string name(method);
  if (name == "GetState") {
    Reply(invocation, StateJson());
  } else if (name == "Add") {
    const gchar *account, *type, *options;
    g_variant_get(parameters, "(&s&s&s)", &account, &type, &options);
    HandleAdd(invocation, account, type, options);
  } else if (name == "Continue") {
    const gchar *account, *state, *answer;
    g_variant_get(parameters, "(&s&s&s)", &account, &state, &answer);
    HandleContinue(invocation, account, state, answer);
  } else if (name == "Cancel") {
    const gchar* account;
    g_variant_get(parameters, "(&s)", &account);
    HandleCancel(account);
    g_dbus_method_invocation_return_value(invocation, nullptr);
  } else if (name == "Remove") {
    const gchar* account;
    g_variant_get(parameters, "(&s)", &account);
    HandleRemove(invocation, account);
  }
}

gboolean OnRemountTimer(gpointer) {
  if (g.rcd_ready && g.adding.empty() && !g.mount_errors.empty()) {
    MountAll([]() {});
  }
  return G_SOURCE_CONTINUE;
}

// Unmounts everything before the session ends, so the vault can go.
gboolean OnTerm(gpointer) {
  g.quitting = true;
  if (!g.rcd) {
    g_main_loop_quit(g.loop);
    return G_SOURCE_REMOVE;
  }
  Rc("mount/unmountall", json_object_new(), nullptr,
     [](JsonNode*, const std::string&) {
       if (g.rcd) {
         g_subprocess_send_signal(g.rcd, SIGTERM);
       } else {
         g_main_loop_quit(g.loop);
       }
     });
  g_timeout_add_seconds(
      10,
      [](gpointer) -> gboolean {
        g_main_loop_quit(g.loop);
        return G_SOURCE_REMOVE;
      },
      nullptr);
  return G_SOURCE_REMOVE;
}

}  // namespace

int main() {
  const char* runtime = g_get_user_runtime_dir();
  g.data_dir = std::string(g_get_user_data_dir()) + "/nickel-cloud";
  g.rclone_path =
      g.data_dir + "/rclone-" + kRcloneVersion + "/rclone";
  g.config_path = std::string(g_get_user_config_dir()) + "/rclone/rclone.conf";
  g.runtime_dir = std::string(runtime) + "/nickel-cloud";
  g.socket_path = g.runtime_dir + "/rc.sock";
  g.drives_dir = g.runtime_dir + "/drives";
  if (g_file_test(g.rclone_path.c_str(), G_FILE_TEST_IS_EXECUTABLE)) {
    g.rclone = Rclone::kReady;
  }

  g.loop = g_main_loop_new(nullptr, FALSE);
  g_autoptr(GError) error = nullptr;
  g.session = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, &error);
  if (!g.session) {
    g_printerr("nickel-cloud: no session bus: %s\n", error->message);
    return 1;
  }
  g_autoptr(GDBusNodeInfo) node =
      g_dbus_node_info_new_for_xml(kIntrospection, nullptr);
  static const GDBusInterfaceVTable kVTable = {OnMethodCall, nullptr, nullptr,
                                               {nullptr}};
  if (!g_dbus_connection_register_object(g.session, kObjectPath,
                                         node->interfaces[0], &kVTable,
                                         nullptr, nullptr, &error)) {
    g_printerr("nickel-cloud: %s\n", error->message);
    return 1;
  }
  g_bus_own_name_on_connection(g.session, kBusName,
                               G_BUS_NAME_OWNER_FLAGS_NONE, nullptr, nullptr,
                               nullptr, nullptr);
  g_unix_signal_add(SIGTERM, OnTerm, nullptr);
  g_unix_signal_add(SIGINT, OnTerm, nullptr);
  g_timeout_add_seconds(kRemountSeconds, OnRemountTimer, nullptr);

  // Accounts from an earlier session: mount them (fetching rclone first if
  // it went, e.g. a new pinned version after an OS update).
  if (!ConfiguredNames().empty()) {
    WithRclone([]() {});
  }
  g_main_loop_run(g.loop);
  return 0;
}
