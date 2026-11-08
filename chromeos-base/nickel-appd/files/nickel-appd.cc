// Distributed under the terms of the GNU General Public License v2
//
// nickel-appd: puts the Linux apps installed on NickelOS (Flatpak, Portage,
// /usr/local) into Chrome's launcher and starts them when they are launched.
//
// Chrome keeps Linux apps in its GuestOS registry, which vm_cicerone normally
// fills from a VM. nickel-appd fills it for the host itself, as VM "nickel",
// container "host":
//  - it reads every .desktop file in XDG_DATA_DIRS and sends the list to
//    org.chromium.VmApplicationsService.UpdateApplicationList, again whenever
//    apps are installed or removed;
//  - before that it copies each app's icon to
//    <profile>/crostini.icons/<app id>/, where Chrome looks for icons first;
//  - it owns org.nickelos.Apps on the system bus. NickelOS's Chrome calls
//    LaunchApplication(desktop_file_id, args) to start one of these apps;
//  - LaunchTool(desktop_file_id, kind) starts an app as the system's tool
//    of that kind: "screenshot" for the app Print Screen opens instead of
//    ChromeOS's own capture (Settings > Apps > Default apps), started with
//    its desktop entry's capture action when it has one;
//  - SpawnShell(terminal, status, command, env) starts a login shell (or a
//    command) on the terminal it is handed, and writes its exit status (an
//    int32) to the pipe |status| when it ends.
//    The Terminal app runs nickel-terminal-shell, which hands over its
//    terminal: a shell Chrome started itself would inherit the no_new_privs
//    and Landlock sandbox session_manager puts Chrome in (no sudo, no mount).
//  - SetAppearance(dark, accent) is how Chrome reports the system's dark or
//    light mode and accent color (ARGB), at start and on every change.
//    GetAppearance() returns them (known is false until Chrome has
//    reported), and the AppearanceChanged signal announces changes. The
//    NickelOS portal backend (xdg-desktop-portal-nickel) passes them on to
//    Linux apps.
//  - FileSelected(token, uris) is how Chrome answers the portal backend's
//    file dialog request (org.chromium.VmApplicationsService.SelectFile for
//    VM "nickel"): the chosen files as file: URLs, none if canceled. It is
//    passed on as the FileSelected signal, which the backend waits for.
//  - NotificationEvent(key, event, button, token) is how Chrome reports what
//    the user did with a Linux app's notification (vm_applications'
//    NickelNotify): "clicked" (button -1 for the notification itself) with
//    an xdg-activation token, or "closed" (button 1 if the user closed it).
//    It is passed on as the NotificationEvent signal for the portal backend,
//    which shows the notifications.
//  - TrayEvent(key, event, item, token) is how Chrome reports what the user
//    did with a Linux app's tray icon, which ChromeOS shows on the shelf
//    (tray.cc: nickel-appd is the tray Linux apps register with, on the
//    session bus): "activate", "menu" (entry |item|) or "quit".
//  - UninstallApplication(desktop_file_id) removes an app through what
//    installed it: a Flatpak with `flatpak uninstall` (a system-wide one
//    after polkit's check), a Gentoo package with nickel-gentoo-uninstall
//    under pkexec (asks for an administrator's password), an AppImage by
//    deleting it and its desktop file, an Android app the user installed
//    through Android's package manager (nickel-waydroid-apk --uninstall,
//    which asks nickel-waydroidd). It answers at once; the app leaves
//    Chrome's list when its desktop file goes. The app list marks the apps
//    this can remove with a package id, for which Chrome offers "Uninstall".
//  - At sign-in it starts the session's autostart entries (~/.config/autostart
//    and /etc/xdg/autostart; autostart.cc): apps' "start at sign-in".
//  - It is the user's GeoClue agent (location.cc): Linux apps get the location
//    only while ChromeOS's location switch lets apps use it.
//  - Steam's installed games are apps (steam.cc): in the launcher, and on
//    the shelf by name and icon while they run; uninstalled through Steam.
//  - Thumbnail(path, size) -> (png): a picture preview of a file Chrome
//    can't decode itself (HEIC photos, H.265 videos), for the desktop's icons
//    and Files' thumbnails, made by sandboxed thumbnailers and kept in
//    ~/.cache/thumbnails (thumbnail.cc).
//  - GetAppDetails(desktop_file_id) -> (version, app_size, data_size), for
//    Chrome's App Management page: a Flatpak's from `flatpak info` and its
//    data in ~/.var/app/<id>, a Portage package's from the package database,
//    an AppImage's file size and X-AppImage-Version. Unknown values are ""
//    and 0.
// Only the session user (the user Chrome runs as) may call these.
//
// The nickel-appd job runs it as the session user, with the session
// environment, inside Chrome's mount namespace; apps inherit all of that.

#include <gio/gdesktopappinfo.h>
#include <gio/gio.h>
#include <gio/gunixfdlist.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <syslog.h>
#include <unistd.h>

#include <cstring>

#include <string>
#include <vector>

#include <chromeos/dbus/vm_applications/dbus-constants.h>
#include <vm_applications/apps.pb.h>

#include "autostart.h"
#include "location.h"
#include "steam.h"
#include "thumbnail.h"
#include "tray.h"

namespace {

// Chrome derives each app's id from these names and its desktop file id;
// they name no VM.
constexpr char kVmName[] = "nickel";
constexpr char kContainerName[] = "host";
constexpr char kDesktopGroup[] = "Desktop Entry";
// ChromeOS's own handler for links and files (nickel-open, the default in
// nickelos-mimeapps.list). Not an app; and as a file handler in Chrome it
// would send files back to itself.
constexpr char kChromeOSHandlerId[] = "org.nickelos.ChromeOS.desktop";

constexpr char kServiceName[] = "org.nickelos.Apps";
// Gentoo's layer (nickel-gentoo): its /usr, which the overlay shows in /usr.
constexpr char kGentooLayer[] = "/mnt/stateful_partition/nickel-gentoo";
constexpr char kObjectPath[] = "/org/nickelos/Apps";
constexpr char kIntrospection[] =
    "<node>"
    "  <interface name='org.nickelos.Apps'>"
    "    <method name='LaunchApplication'>"
    "      <arg type='s' name='desktop_file_id' direction='in'/>"
    "      <arg type='as' name='args' direction='in'/>"
    "    </method>"
    "    <method name='LaunchTool'>"
    "      <arg type='s' name='desktop_file_id' direction='in'/>"
    "      <arg type='s' name='kind' direction='in'/>"
    "    </method>"
    "    <method name='SpawnShell'>"
    "      <arg type='h' name='terminal' direction='in'/>"
    "      <arg type='h' name='status' direction='in'/>"
    "      <arg type='as' name='command' direction='in'/>"
    "      <arg type='as' name='env' direction='in'/>"
    "    </method>"
    "    <method name='SetAppearance'>"
    "      <arg type='b' name='dark' direction='in'/>"
    "      <arg type='u' name='accent' direction='in'/>"
    "    </method>"
    "    <method name='GetAppearance'>"
    "      <arg type='b' name='known' direction='out'/>"
    "      <arg type='b' name='dark' direction='out'/>"
    "      <arg type='u' name='accent' direction='out'/>"
    "    </method>"
    "    <signal name='AppearanceChanged'>"
    "      <arg type='b' name='dark'/>"
    "      <arg type='u' name='accent'/>"
    "    </signal>"
    "    <method name='GetAppDetails'>"
    "      <arg type='s' name='desktop_file_id' direction='in'/>"
    "      <arg type='s' name='version' direction='out'/>"
    "      <arg type='t' name='app_size' direction='out'/>"
    "      <arg type='t' name='data_size' direction='out'/>"
    "    </method>"
    "    <method name='UninstallApplication'>"
    "      <arg type='s' name='desktop_file_id' direction='in'/>"
    "    </method>"
    "    <method name='FileSelected'>"
    "      <arg type='s' name='token' direction='in'/>"
    "      <arg type='as' name='uris' direction='in'/>"
    "    </method>"
    "    <signal name='FileSelected'>"
    "      <arg type='s' name='token'/>"
    "      <arg type='as' name='uris'/>"
    "    </signal>"
    "    <method name='NotificationEvent'>"
    "      <arg type='s' name='key' direction='in'/>"
    "      <arg type='s' name='event' direction='in'/>"
    "      <arg type='i' name='button' direction='in'/>"
    "      <arg type='s' name='token' direction='in'/>"
    "    </method>"
    "    <signal name='NotificationEvent'>"
    "      <arg type='s' name='key'/>"
    "      <arg type='s' name='event'/>"
    "      <arg type='i' name='button'/>"
    "      <arg type='s' name='token'/>"
    "    </signal>"
    "    <method name='TrayEvent'>"
    "      <arg type='s' name='key' direction='in'/>"
    "      <arg type='s' name='event' direction='in'/>"
    "      <arg type='i' name='item' direction='in'/>"
    "      <arg type='s' name='token' direction='in'/>"
    "    </method>"
    "    <method name='SetDefaultHandler'>"
    "      <arg type='s' name='mime_type' direction='in'/>"
    "      <arg type='s' name='desktop_file_id' direction='in'/>"
    "    </method>"
    "    <method name='Thumbnail'>"
    "      <arg type='s' name='path' direction='in'/>"
    "      <arg type='u' name='size' direction='in'/>"
    "      <arg type='ay' name='png' direction='out'/>"
    "    </method>"
    "  </interface>"
    "</node>";

// Retry while Chrome has no registry for the session yet (it answers
// "Shutting down" until the profile is loaded) or is not on the bus.
constexpr guint kRetrySeconds = 2;
constexpr guint kMaxRetrySeconds = 30;
// Installs touch several files; publish once things settle.
constexpr guint kChangeDelayMs = 1500;

struct State {
  GDBusConnection* bus = nullptr;
  std::string icon_dir;
  guint send_source = 0;
  guint retry_seconds = kRetrySeconds;
  // The system's appearance, as Chrome last reported it.
  bool appearance_known = false;
  bool dark = false;
  guint32 accent = 0;
  // Linux apps' tray icons (tray.cc); null without a session bus.
  Tray* tray = nullptr;
  // Picture previews (thumbnail.cc).
  Thumbnails* thumbnails = nullptr;
  // Steam's games as apps (steam.cc).
  SteamGames* steam = nullptr;
};

#define NICKEL_INFO(...) syslog(LOG_INFO, __VA_ARGS__)
#define NICKEL_WARN(...) syslog(LOG_WARNING, __VA_ARGS__)

// Chrome's app id: crx_file::id_util::GenerateId("crostini:<vm>/<container>/
// <desktop file id>"), i.e. the first 128 bits of the SHA-256 in hex, with the
// hex digits 0-f written as the letters a-p.
std::string AppId(const std::string& desktop_file_id) {
  std::string input = std::string("crostini:") + kVmName + "/" +
                      kContainerName + "/" + desktop_file_id;
  gchar* hex =
      g_compute_checksum_for_string(G_CHECKSUM_SHA256, input.c_str(), -1);
  std::string id;
  for (int i = 0; i < 32; ++i) {
    char c = hex[i];
    id += static_cast<char>('a' + (c <= '9' ? c - '0' : c - 'a' + 10));
  }
  g_free(hex);
  return id;
}

// "Name" -> "", "Name[fr]" -> "fr"; false for other keys.
bool LocaleOfKey(const std::string& key,
                 const std::string& base,
                 std::string* locale) {
  if (key == base) {
    locale->clear();
    return true;
  }
  if (key.size() > base.size() + 2 && key.compare(0, base.size(), base) == 0 &&
      key[base.size()] == '[' && key.back() == ']') {
    *locale = key.substr(base.size() + 1, key.size() - base.size() - 2);
    return true;
  }
  return false;
}

void FillLocaleString(GKeyFile* kf,
                      gchar** keys,
                      const char* base,
                      vm_tools::apps::App::LocaleString* out) {
  std::string locale;
  for (gchar** k = keys; k && *k; ++k) {
    if (!LocaleOfKey(*k, base, &locale)) {
      continue;
    }
    gchar* value = g_key_file_get_string(kf, kDesktopGroup, *k, nullptr);
    if (value) {
      auto* entry = out->add_values();
      entry->set_locale(locale);
      entry->set_value(value);
      g_free(value);
    }
  }
}

void FillLocaleStrings(GKeyFile* kf,
                       gchar** keys,
                       const char* base,
                       vm_tools::apps::App::LocaleStrings* out) {
  std::string locale;
  for (gchar** k = keys; k && *k; ++k) {
    if (!LocaleOfKey(*k, base, &locale)) {
      continue;
    }
    gchar** values =
        g_key_file_get_string_list(kf, kDesktopGroup, *k, nullptr, nullptr);
    if (values) {
      auto* entry = out->add_values();
      entry->set_locale(locale);
      for (gchar** v = values; *v; ++v) {
        entry->add_value(*v);
      }
      g_strfreev(values);
    }
  }
}

// An app's GenericName ("Web Browser", "Image Viewer", "ksnip Screenshot
// Tool") is one more keyword in its language, as GNOME's search matches it:
// the launcher finds apps by what they are, and Settings > Apps > Default
// apps finds screenshot tools that say so only there.
void AddGenericNameKeywords(GKeyFile* kf,
                            gchar** keys,
                            vm_tools::apps::App::LocaleStrings* keywords) {
  std::string locale;
  for (gchar** k = keys; k && *k; ++k) {
    if (!LocaleOfKey(*k, "GenericName", &locale)) {
      continue;
    }
    gchar* value = g_key_file_get_string(kf, kDesktopGroup, *k, nullptr);
    if (!value) {
      continue;
    }
    vm_tools::apps::App::LocaleStrings::StringsWithLocale* entry = nullptr;
    for (auto& e : *keywords->mutable_values()) {
      if (e.locale() == locale) {
        entry = &e;
      }
    }
    if (!entry) {
      entry = keywords->add_values();
      entry->set_locale(locale);
    }
    entry->add_value(value);
    g_free(value);
  }
}

bool ReadFile(const std::string& path, std::string* contents) {
  gchar* data = nullptr;
  gsize len = 0;
  if (!g_file_get_contents(path.c_str(), &data, &len, nullptr)) {
    return false;
  }
  contents->assign(data, len);
  g_free(data);
  return true;
}

// Finds a file for a themed icon name in the hicolor theme or pixmaps of
// every data dir, preferring scalable icons, then the largest PNG.
std::string FindIconByName(const char* name) {
  if (g_path_is_absolute(name)) {
    return g_file_test(name, G_FILE_TEST_IS_REGULAR) ? name : "";
  }
  std::vector<std::string> dirs = {g_get_user_data_dir()};
  for (const gchar* const* d = g_get_system_data_dirs(); *d; ++d) {
    dirs.push_back(*d);
  }
  static const char* const kSizes[] = {"512x512", "256x256", "192x192",
                                       "128x128", "96x96",   "64x64",
                                       "48x48",   "32x32"};
  std::vector<std::string> candidates;
  for (const auto& dir : dirs) {
    candidates.push_back(dir + "/icons/hicolor/scalable/apps/" + name +
                         ".svg");
  }
  for (const char* size : kSizes) {
    for (const auto& dir : dirs) {
      candidates.push_back(dir + "/icons/hicolor/" + size + "/apps/" + name +
                           ".png");
    }
  }
  for (const auto& dir : dirs) {
    candidates.push_back(dir + "/pixmaps/" + name + ".svg");
    candidates.push_back(dir + "/pixmaps/" + name + ".png");
  }
  for (const auto& path : candidates) {
    if (g_file_test(path.c_str(), G_FILE_TEST_IS_REGULAR)) {
      return path;
    }
  }
  return "";
}

std::string FindIconFile(GIcon* icon) {
  if (!icon) {
    return "";
  }
  if (G_IS_FILE_ICON(icon)) {
    gchar* path = g_file_get_path(g_file_icon_get_file(G_FILE_ICON(icon)));
    std::string result = path ? path : "";
    g_free(path);
    return result;
  }
  if (G_IS_THEMED_ICON(icon)) {
    for (const gchar* const* n = g_themed_icon_get_names(G_THEMED_ICON(icon));
         n && *n; ++n) {
      std::string path = FindIconByName(*n);
      if (!path.empty()) {
        return path;
      }
    }
  }
  return "";
}

// Writes |contents| to |path| unless it already holds exactly that.
void WriteIfChanged(const std::string& path, const std::string& contents) {
  std::string old;
  if (ReadFile(path, &old) && old == contents) {
    return;
  }
  GError* error = nullptr;
  if (!g_file_set_contents(path.c_str(), contents.data(), contents.size(),
                           &error)) {
    NICKEL_WARN("cannot write %s: %s", path.c_str(), error->message);
    g_error_free(error);
  }
}

// Chrome reads <icon_dir>/<app id>/icon_{100,200,300}p.png, or transcodes
// icon.svg, before it would ask a VM for the icon.
void InstallIcon(const State& state,
                 const std::string& app_id,
                 const std::string& source) {
  std::string contents;
  if (source.empty() || !ReadFile(source, &contents)) {
    return;
  }
  std::string dir = state.icon_dir + "/" + app_id;
  if (g_mkdir_with_parents(dir.c_str(), 0700) != 0) {
    NICKEL_WARN("cannot create %s", dir.c_str());
    return;
  }
  if (g_str_has_suffix(source.c_str(), ".svg")) {
    for (const char* png : {"icon_100p.png", "icon_200p.png", "icon_300p.png"}) {
      g_unlink((dir + "/" + png).c_str());
    }
    WriteIfChanged(dir + "/icon.svg", contents);
  } else {
    g_unlink((dir + "/icon.svg").c_str());
    for (const char* png : {"icon_100p.png", "icon_200p.png", "icon_300p.png"}) {
      WriteIfChanged(dir + "/" + png, contents);
    }
  }
}

// What installed an app, and so how it is removed. The OS image's own apps
// (/usr/share/applications) have no source: they cannot be uninstalled.
struct Source {
  // kGentoo: Gentoo's world at / (nickel-gentoo): its launcher entries are
  // in /usr/share/applications beside the OS's, and in Gentoo's layer only.

  // kSteam: a game a Steam installed (steam.cc).
  // kWaydroid: an Android app the user installed in their Android.
  enum Kind { kNone, kFlatpak, kGentoo, kAppImage, kSteam, kWaydroid };
  Kind kind = kNone;
  bool system = false;  // Flatpak: the system-wide installation
  std::string id;  // Flatpak: app id; AppImage: its file; Steam: game;
                   // Waydroid: the Android package
};

// Where an Android app the user installed lives in their Android: its
// directory in Android's /data/app, which Waydroid keeps in
// ~/.local/share/waydroid/data/app ("~~<random>/<package>-<random>" since
// Android 11, "<package>-<random>" before). Android's own apps are in its
// system image and have none: "" for those, and they can't be uninstalled.
// Android's system user is uid and gid 1000, chronos here, and every local
// account is in that group, so the user can look (Android's usual 0771).
std::string WaydroidAppDir(const std::string& package) {
  const std::string app =
      std::string(g_get_user_data_dir()) + "/waydroid/data/app";
  const std::string prefix = package + "-";  // package names have no '-'
  std::string found;
  GDir* dir = g_dir_open(app.c_str(), 0, nullptr);
  if (!dir) {
    return found;
  }
  while (const char* name = g_dir_read_name(dir)) {
    const std::string path = app + "/" + name;
    if (g_str_has_prefix(name, prefix.c_str())) {
      found = path;
      break;
    }
    if (!g_str_has_prefix(name, "~~")) {
      continue;
    }
    if (GDir* sub = g_dir_open(path.c_str(), 0, nullptr)) {
      while (const char* inner = g_dir_read_name(sub)) {
        if (g_str_has_prefix(inner, prefix.c_str())) {
          found = path + "/" + inner;
          break;
        }
      }
      g_dir_close(sub);
    }
    if (!found.empty()) {
      break;
    }
  }
  g_dir_close(dir);
  return found;
}

// The Android package a launcher entry of Waydroid's opens: Waydroid's
// session writes waydroid.<package>.desktop, Exec=waydroid app launch
// <package>. "" for any other desktop file.
std::string WaydroidPackageOf(const char* filename, char** argv) {
  if (!argv || g_strv_length(argv) != 4 || g_strcmp0(argv[0], "waydroid") ||
      g_strcmp0(argv[1], "app") || g_strcmp0(argv[2], "launch")) {
    return "";
  }
  gchar* base = g_path_get_basename(filename);
  const std::string expected = std::string("waydroid.") + argv[3] + ".desktop";
  const bool match = expected == base;
  g_free(base);
  return match ? argv[3] : "";
}

Source FindSource(GDesktopAppInfo* desktop) {
  Source source;
  const char* filename = g_desktop_app_info_get_filename(desktop);
  if (!filename) {
    return source;
  }
  std::string home = g_get_home_dir();
  if (strstr(filename, "/flatpak/exports/share/applications/")) {
    if (gchar* flatpak_id = g_desktop_app_info_get_string(desktop, "X-Flatpak")) {
      source.kind = Source::kFlatpak;
      source.system = !g_str_has_prefix(filename, (home + "/").c_str());
      source.id = flatpak_id;
      g_free(flatpak_id);
    }
  } else if (g_str_has_prefix(filename, "/usr/share/applications/") &&
             g_file_test((std::string(kGentooLayer) + filename).c_str(),
                         G_FILE_TEST_EXISTS)) {
    source.kind = Source::kGentoo;
  } else if (g_str_has_prefix(filename,
                              (home + "/.local/share/applications/").c_str())) {
    // AppImage integration (Gear Lever, AppImageLauncher) writes a desktop
    // file that runs the AppImage, kept in the user's home; so do Steam
    // (from Portage), nickel-appd for Steam's games, and Waydroid's session
    // for Android apps.
    gchar* exec = g_desktop_app_info_get_string(desktop, "Exec");
    gchar** argv = nullptr;
    const std::string game = SteamGameOfExec(exec);
    if (!game.empty()) {
      source.kind = Source::kSteam;
      source.id = game;
    } else if (exec && g_shell_parse_argv(exec, nullptr, &argv, nullptr) &&
               argv[0]) {
      // An Android app Waydroid's session put in the launcher, if the user
      // installed it (Android's own apps stay without a source).
      const std::string package = WaydroidPackageOf(filename, argv);
      if (!package.empty() && !WaydroidAppDir(package).empty()) {
        source.kind = Source::kWaydroid;
        source.id = package;
      }
      gchar* lower = g_ascii_strdown(argv[0], -1);
      if (g_str_has_suffix(lower, ".appimage") &&
          g_str_has_prefix(argv[0], (home + "/").c_str()) &&
          g_file_test(argv[0], G_FILE_TEST_IS_REGULAR)) {
        source.kind = Source::kAppImage;
        source.id = argv[0];
      }
      g_free(lower);
    }
    g_strfreev(argv);
    g_free(exec);
  }
  return source;
}

// The package id Chrome gets; any non-empty one means "can be uninstalled".
std::string PackageId(const Source& source) {
  switch (source.kind) {
    case Source::kFlatpak:
      return std::string("flatpak:") + (source.system ? "system:" : "user:") +
             source.id;
    case Source::kGentoo:
      return "portage";
    case Source::kAppImage:
      return "appimage:" + source.id;
    case Source::kSteam:
      return "steam:" + source.id;
    case Source::kWaydroid:
      return "android:" + source.id;
    case Source::kNone:
      break;
  }
  return "";
}

vm_tools::apps::ApplicationList BuildApplicationList(const State& state) {
  vm_tools::apps::ApplicationList list;
  list.set_vm_name(kVmName);
  list.set_container_name(kContainerName);
  // Chrome lists these as NickelOS host apps, not Crostini's
  // (docs/host-apps.md).
  list.set_vm_type(vm_tools::apps::NICKEL_HOST);

  GList* all = g_app_info_get_all();
  for (GList* l = all; l; l = l->next) {
    GAppInfo* info = G_APP_INFO(l->data);
    if (!G_IS_DESKTOP_APP_INFO(info)) {
      continue;
    }
    GDesktopAppInfo* desktop = G_DESKTOP_APP_INFO(info);
    const char* file_id = g_app_info_get_id(info);
    const char* filename = g_desktop_app_info_get_filename(desktop);
    if (!file_id || !filename || !g_str_has_suffix(file_id, ".desktop")) {
      continue;
    }
    if (g_str_equal(file_id, kChromeOSHandlerId)) {
      continue;
    }
    GKeyFile* kf = g_key_file_new();
    if (!g_key_file_load_from_file(kf, filename, G_KEY_FILE_NONE, nullptr)) {
      g_key_file_free(kf);
      continue;
    }
    gchar** keys = g_key_file_get_keys(kf, kDesktopGroup, nullptr, nullptr);

    auto* app = list.add_apps();
    std::string desktop_file_id(file_id, strlen(file_id) - strlen(".desktop"));
    app->set_desktop_file_id(desktop_file_id);
    FillLocaleString(kf, keys, "Name", app->mutable_name());
    FillLocaleString(kf, keys, "Comment", app->mutable_comment());
    FillLocaleStrings(kf, keys, "Keywords", app->mutable_keywords());
    AddGenericNameKeywords(kf, keys, app->mutable_keywords());
    if (gchar** mimes = g_key_file_get_string_list(kf, kDesktopGroup,
                                                   "MimeType", nullptr,
                                                   nullptr)) {
      for (gchar** m = mimes; *m; ++m) {
        if (**m) {
          app->add_mime_types(*m);
        }
      }
      g_strfreev(mimes);
    }
    app->set_no_display(!g_app_info_should_show(info));
    if (const char* wm_class = g_desktop_app_info_get_startup_wm_class(desktop)) {
      app->set_startup_wm_class(wm_class);
    } else if (gchar* exec = g_key_file_get_string(kf, kDesktopGroup, "Exec",
                                                   nullptr)) {
      // A Steam game's own desktop file has none: the shelf knows the
      // game's windows by Proton's class.
      const std::string game = SteamGameOfExec(exec);
      if (!game.empty()) {
        app->set_startup_wm_class(SteamWindowClass(game));
      }
      g_free(exec);
    }
    app->set_startup_notify(
        g_key_file_get_boolean(kf, kDesktopGroup, "StartupNotify", nullptr));
    if (gchar* exec = g_key_file_get_string(kf, kDesktopGroup, "Exec", nullptr)) {
      app->set_exec(exec);
      g_free(exec);
    }
    app->set_terminal(
        g_key_file_get_boolean(kf, kDesktopGroup, "Terminal", nullptr));
    app->set_package_id(PackageId(FindSource(desktop)));
    if (const char* executable = g_app_info_get_executable(info)) {
      app->set_executable_file_name(executable);
    }

    InstallIcon(state, AppId(desktop_file_id),
                FindIconFile(g_app_info_get_icon(info)));

    g_strfreev(keys);
    g_key_file_free(kf);
  }
  g_list_free_full(all, g_object_unref);
  return list;
}

void ScheduleSend(State* state, guint delay_ms);

void OnApplicationListSent(GObject* source,
                           GAsyncResult* result,
                           gpointer user_data) {
  State* state = static_cast<State*>(user_data);
  GError* error = nullptr;
  GVariant* reply =
      g_dbus_connection_call_finish(G_DBUS_CONNECTION(source), result, &error);
  if (reply) {
    g_variant_unref(reply);
    state->retry_seconds = kRetrySeconds;
    return;
  }
  NICKEL_WARN("UpdateApplicationList failed, retrying in %us: %s",
           state->retry_seconds, error->message);
  g_error_free(error);
  ScheduleSend(state, state->retry_seconds * 1000);
  state->retry_seconds = MIN(state->retry_seconds * 2, kMaxRetrySeconds);
}

void SendApplicationList(State* state) {
  vm_tools::apps::ApplicationList list = BuildApplicationList(*state);
  std::string bytes;
  list.SerializeToString(&bytes);
  NICKEL_INFO("publishing %d apps", list.apps_size());
  GVariant* array = g_variant_new_fixed_array(G_VARIANT_TYPE_BYTE, bytes.data(),
                                              bytes.size(), 1);
  g_dbus_connection_call(
      state->bus, vm_tools::apps::kVmApplicationsServiceName,
      vm_tools::apps::kVmApplicationsServicePath,
      vm_tools::apps::kVmApplicationsServiceInterface,
      vm_tools::apps::kVmApplicationsServiceUpdateApplicationListMethod,
      g_variant_new_tuple(&array, 1), nullptr, G_DBUS_CALL_FLAGS_NONE, 10000,
      nullptr, OnApplicationListSent, state);
}

gboolean OnSendTimeout(gpointer user_data) {
  State* state = static_cast<State*>(user_data);
  state->send_source = 0;
  SendApplicationList(state);
  return G_SOURCE_REMOVE;
}

void ScheduleSend(State* state, guint delay_ms) {
  if (state->send_source) {
    g_source_remove(state->send_source);
  }
  state->send_source = g_timeout_add(delay_ms, OnSendTimeout, state);
}

void OnAppsChanged(GAppInfoMonitor*, gpointer user_data) {
  State* state = static_cast<State*>(user_data);
  // Steam itself may just have come or gone. (Its games' desktop files are
  // only written when they change, so this does not run again by itself.)
  if (state->steam) {
    SteamGamesSync(state->steam);
  }
  ScheduleSend(state, kChangeDelayMs);
}

void HandleLaunchApplication(GDBusMethodInvocation* invocation,
                             GVariant* parameters) {
  const gchar* desktop_file_id = nullptr;
  const gchar** args = nullptr;
  g_variant_get(parameters, "(&s^a&s)", &desktop_file_id, &args);

  std::string file_id = std::string(desktop_file_id) + ".desktop";
  GDesktopAppInfo* desktop = g_desktop_app_info_new(file_id.c_str());
  if (!desktop) {
    g_dbus_method_invocation_return_error(
        invocation, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "no app %s",
        desktop_file_id);
    g_free(args);
    return;
  }

  // Chrome passes files as absolute paths (apps share its mount namespace)
  // and anything else, such as URLs, as is.
  GList* uris = nullptr;
  for (const gchar** a = args; a && *a; ++a) {
    gchar* uri = g_path_is_absolute(*a)
                     ? g_filename_to_uri(*a, nullptr, nullptr)
                     : g_strdup(*a);
    if (uri) {
      uris = g_list_append(uris, uri);
    }
  }
  g_free(args);

  GAppLaunchContext* context = g_app_launch_context_new();
  GError* error = nullptr;
  gboolean launched =
      g_app_info_launch_uris(G_APP_INFO(desktop), uris, context, &error);
  g_list_free_full(uris, g_free);
  g_object_unref(context);
  g_object_unref(desktop);

  if (!launched) {
    NICKEL_WARN("launching %s failed: %s", file_id.c_str(), error->message);
    g_dbus_method_invocation_take_error(invocation, error);
    return;
  }
  NICKEL_INFO("launched %s", file_id.c_str());
  g_dbus_method_invocation_return_value(invocation, nullptr);
}

// A screenshot tool's desktop action that captures (an area, as ChromeOS's
// own capture starts), in their desktop files on Flathub: launched as they
// are, Flameshot only starts its tray icon and ksnip and Gradia open their
// main windows. Tools without one (adishatz's Screenshot) capture or ask
// what to capture when launched.
constexpr const char* kCaptureActions[] = {
    "Capture",                      // Flameshot (flameshot gui)
    "Area",                         // ksnip (ksnip -r)
    "take-screenshot",              // Gradia
    "RectangularRegionScreenShot",  // Spectacle
};

void HandleLaunchTool(GDBusMethodInvocation* invocation, GVariant* parameters) {
  const gchar* desktop_file_id = nullptr;
  const gchar* kind = nullptr;
  g_variant_get(parameters, "(&s&s)", &desktop_file_id, &kind);

  std::string file_id = std::string(desktop_file_id) + ".desktop";
  GDesktopAppInfo* desktop = g_desktop_app_info_new(file_id.c_str());
  if (!desktop) {
    g_dbus_method_invocation_return_error(
        invocation, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "no app %s",
        desktop_file_id);
    return;
  }

  GAppLaunchContext* context = g_app_launch_context_new();
  const char* action = nullptr;
  if (g_strcmp0(kind, "screenshot") == 0) {
    for (const gchar* const* a = g_desktop_app_info_list_actions(desktop);
         a && *a && !action; ++a) {
      for (const char* capture : kCaptureActions) {
        if (g_strcmp0(*a, capture) == 0) {
          action = capture;
        }
      }
    }
  }
  GError* error = nullptr;
  gboolean launched = TRUE;
  if (action) {
    g_desktop_app_info_launch_action(desktop, action, context);
  } else {
    launched = g_app_info_launch(G_APP_INFO(desktop), nullptr, context, &error);
  }
  g_object_unref(context);
  g_object_unref(desktop);

  if (!launched) {
    NICKEL_WARN("launching %s as the %s tool failed: %s", file_id.c_str(),
                kind, error->message);
    g_dbus_method_invocation_take_error(invocation, error);
    return;
  }
  NICKEL_INFO("launched %s as the %s tool%s%s", file_id.c_str(), kind,
              action ? ", action " : "", action ? action : "");
  g_dbus_method_invocation_return_value(invocation, nullptr);
}

// Terminal variables a shell takes from the terminal it runs on; everything
// else comes from the session environment nickel-appd runs with.
bool IsTerminalVariable(const char* assignment) {
  for (const char* name : {"TERM=", "COLORTERM=", "TERM_PROGRAM="}) {
    if (g_str_has_prefix(assignment, name)) {
      return true;
    }
  }
  return false;
}

// Runs in the shell's process before exec: a new session whose controlling
// terminal is the one on stdin (nickel-terminal-shell gave it up).
void BecomeTerminalSessionLeader(gpointer) {
  setsid();
  ioctl(STDIN_FILENO, TIOCSCTTY, 0);
}

// The shell ended: report its status on the pipe the caller waits on.
void OnShellExited(GPid pid, gint wait_status, gpointer user_data) {
  int status_fd = GPOINTER_TO_INT(user_data);
  gint32 status = WIFEXITED(wait_status)     ? WEXITSTATUS(wait_status)
                  : WIFSIGNALED(wait_status) ? 128 + WTERMSIG(wait_status)
                                             : 1;
  if (write(status_fd, &status, sizeof(status)) != sizeof(status)) {
    NICKEL_WARN("cannot report the exit of shell %d", pid);
  }
  close(status_fd);
  g_spawn_close_pid(pid);
}

void HandleSpawnShell(GDBusMethodInvocation* invocation, GVariant* parameters) {
  gint32 terminal_handle = -1;
  gint32 status_handle = -1;
  const gchar** command = nullptr;
  const gchar** env = nullptr;
  g_variant_get(parameters, "(hh^a&s^a&s)", &terminal_handle, &status_handle,
                &command, &env);

  GUnixFDList* fds =
      g_dbus_message_get_unix_fd_list(g_dbus_method_invocation_get_message(
          invocation));
  int terminal = fds ? g_unix_fd_list_get(fds, terminal_handle, nullptr) : -1;
  int status_fd = fds ? g_unix_fd_list_get(fds, status_handle, nullptr) : -1;
  if (terminal < 0 || !isatty(terminal) || status_fd < 0) {
    if (terminal >= 0) {
      close(terminal);
    }
    if (status_fd >= 0) {
      close(status_fd);
    }
    g_free(command);
    g_free(env);
    g_dbus_method_invocation_return_error_literal(
        invocation, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS,
        "needs a terminal and a status pipe");
    return;
  }

  // A login shell reads /etc/profile (PATH, the session environment);
  // a command runs in one instead of a prompt.
  GPtrArray* argv = g_ptr_array_new();
  g_ptr_array_add(argv, const_cast<gchar*>("/bin/bash"));
  g_ptr_array_add(argv, const_cast<gchar*>("-l"));
  if (command && command[0]) {
    g_ptr_array_add(argv, const_cast<gchar*>("-c"));
    g_ptr_array_add(argv, const_cast<gchar*>("exec \"$@\""));
    g_ptr_array_add(argv, const_cast<gchar*>("bash"));
    for (const gchar** c = command; *c; ++c) {
      g_ptr_array_add(argv, const_cast<gchar*>(*c));
    }
  }
  g_ptr_array_add(argv, nullptr);

  gchar** envp = g_get_environ();
  for (const gchar** e = env; e && *e; ++e) {
    const char* eq = strchr(*e, '=');
    if (eq && IsTerminalVariable(*e)) {
      std::string name(*e, eq - *e);
      envp = g_environ_setenv(envp, name.c_str(), eq + 1, TRUE);
    }
  }

  GPid pid = 0;
  GError* error = nullptr;
  gboolean spawned = g_spawn_async_with_fds(
      g_get_home_dir(), reinterpret_cast<gchar**>(argv->pdata), envp,
      static_cast<GSpawnFlags>(G_SPAWN_DO_NOT_REAP_CHILD),
      BecomeTerminalSessionLeader, nullptr, &pid, terminal, terminal, terminal,
      &error);
  close(terminal);
  g_strfreev(envp);
  g_ptr_array_free(argv, TRUE);
  g_free(command);
  g_free(env);
  if (!spawned) {
    NICKEL_WARN("starting a shell failed: %s", error->message);
    close(status_fd);
    g_dbus_method_invocation_take_error(invocation, error);
    return;
  }
  g_child_watch_add(pid, OnShellExited, GINT_TO_POINTER(status_fd));
  g_dbus_method_invocation_return_value(invocation, nullptr);
}

// The session user's uid is nickel-appd's own.
// GetAppDetails runs in a worker thread: flatpak info and du can take a
// moment.
struct Details {
  Source source;
  std::string desktop_path;
  std::string version;
  guint64 app_size = 0;
  guint64 data_size = 0;
};

// Runs ARGV and returns its standard output ("" if it fails).
std::string Output(std::vector<const char*> argv) {
  argv.push_back(nullptr);
  gchar* out = nullptr;
  gint status = 0;
  if (!g_spawn_sync(nullptr, const_cast<gchar**>(argv.data()), nullptr,
                    static_cast<GSpawnFlags>(G_SPAWN_SEARCH_PATH |
                                             G_SPAWN_STDERR_TO_DEV_NULL),
                    nullptr, nullptr, &out, nullptr, &status, nullptr) ||
      !g_spawn_check_wait_status(status, nullptr)) {
    g_free(out);
    return "";
  }
  std::string result = out ? out : "";
  g_free(out);
  return result;
}

// "58.0 MB" (flatpak's g_format_size, SI units) in bytes.
guint64 ParseSize(const std::string& text) {
  double value = g_ascii_strtod(text.c_str(), nullptr);
  const char* units[] = {"kB", "MB", "GB", "TB"};
  double factor = 1;
  for (size_t i = 0; i < G_N_ELEMENTS(units); ++i) {
    if (text.find(units[i]) != std::string::npos) {
      factor = 1;
      for (size_t j = 0; j <= i; ++j) {
        factor *= 1000;
      }
    }
  }
  return static_cast<guint64>(value * factor);
}

// The size of everything below PATH (du), 0 if it does not exist.
guint64 TreeSize(const std::string& path) {
  if (!g_file_test(path.c_str(), G_FILE_TEST_EXISTS)) {
    return 0;
  }
  return g_ascii_strtoull(Output({"du", "-sb", path.c_str()}).c_str(),
                          nullptr, 10);
}

// The size of what the user can read below PATH, skipping what they can't
// (du gives up on the whole tree for one unreadable directory: an Android
// app's oat/, which belongs to Android's installd).
guint64 ReadableTreeSize(const std::string& path) {
  GStatBuf st;
  if (g_lstat(path.c_str(), &st) != 0) {
    return 0;
  }
  if (!S_ISDIR(st.st_mode)) {
    return S_ISREG(st.st_mode) ? st.st_size : 0;
  }
  guint64 total = 0;
  if (GDir* dir = g_dir_open(path.c_str(), 0, nullptr)) {
    while (const char* name = g_dir_read_name(dir)) {
      total += ReadableTreeSize(path + "/" + name);
    }
    g_dir_close(dir);
  }
  return total;
}

void FindDetails(GTask* task, gpointer, gpointer task_data, GCancellable*) {
  Details* d = static_cast<Details*>(task_data);
  switch (d->source.kind) {
    case Source::kFlatpak: {
      std::string info =
          Output({"flatpak", "info", d->source.system ? "--system" : "--user",
                  d->source.id.c_str()});
      gchar** lines = g_strsplit(info.c_str(), "\n", -1);
      for (gchar** line = lines; *line; ++line) {
        gchar* text = g_strstrip(*line);
        if (g_str_has_prefix(text, "Version:")) {
          d->version = g_strstrip(text + strlen("Version:"));
        } else if (g_str_has_prefix(text, "Installed Size:")) {
          // "Installed Size: 14.0 MB" (older flatpak: "Installed:").
          d->app_size = ParseSize(text + strlen("Installed Size:"));
        } else if (g_str_has_prefix(text, "Installed:")) {
          d->app_size = ParseSize(text + strlen("Installed:"));
        }
      }
      g_strfreev(lines);
      d->data_size = TreeSize(std::string(g_get_home_dir()) + "/.var/app/" +
                              d->source.id);
      break;
    }
    case Source::kGentoo: {
      // The package that owns the desktop file; its version and the size
      // of its files (Gentoo's package database, /var/db/pkg).
      const std::string db = "/var/db/pkg/";
      const std::string root = "";
      gchar* name = g_path_get_basename(d->desktop_path.c_str());
      std::string needle =
          std::string("obj /usr/share/applications/") + name + " ";
      g_free(name);
      GDir* categories = g_dir_open(db.c_str(), 0, nullptr);
      const gchar* category = nullptr;
      while (categories && d->version.empty() &&
             (category = g_dir_read_name(categories))) {
        std::string cdir = db + category;
        GDir* packages = g_dir_open(cdir.c_str(), 0, nullptr);
        const gchar* pf = nullptr;
        while (packages && (pf = g_dir_read_name(packages))) {
          gchar* contents = nullptr;
          std::string path = cdir + "/" + pf + "/CONTENTS";
          if (!g_file_get_contents(path.c_str(), &contents, nullptr,
                                   nullptr)) {
            continue;
          }
          if (strstr(contents, needle.c_str())) {
            gchar* pvr = nullptr;
            g_file_get_contents((cdir + "/" + pf + "/PF").c_str(), &pvr,
                                nullptr, nullptr);
            // PF is <name>-<version>[-r<n>]: the version starts after the
            // last '-' that is followed by a digit (before any -r<n>).
            std::string full = pvr ? g_strstrip(pvr) : pf;
            g_free(pvr);
            for (size_t i = 0; i + 1 < full.size(); ++i) {
              if (full[i] == '-' && g_ascii_isdigit(full[i + 1])) {
                d->version = full.substr(i + 1);
                break;
              }
            }
            gchar** lines = g_strsplit(contents, "\n", -1);
            for (gchar** line = lines; *line; ++line) {
              if (!g_str_has_prefix(*line, "obj ")) {
                continue;
              }
              // "obj <path> <md5> <mtime>": the path may hold spaces.
              gchar** f = g_strsplit(*line + 4, " ", -1);
              guint n = g_strv_length(f);
              if (n >= 3) {
                std::string file;
                for (guint i = 0; i + 2 < n; ++i) {
                  file += (i ? " " : "") + std::string(f[i]);
                }
                GStatBuf st;
                if (g_stat((root + file).c_str(), &st) == 0) {
                  d->app_size += st.st_size;
                }
              }
              g_strfreev(f);
            }
            g_strfreev(lines);
            g_free(contents);
            break;
          }
          g_free(contents);
        }
        if (packages) {
          g_dir_close(packages);
        }
      }
      if (categories) {
        g_dir_close(categories);
      }
      break;
    }
    case Source::kAppImage: {
      GStatBuf st;
      if (g_stat(d->source.id.c_str(), &st) == 0) {
        d->app_size = st.st_size;
      }
      GKeyFile* kf = g_key_file_new();
      if (g_key_file_load_from_file(kf, d->desktop_path.c_str(),
                                    G_KEY_FILE_NONE, nullptr)) {
        if (gchar* v = g_key_file_get_string(kf, kDesktopGroup,
                                             "X-AppImage-Version", nullptr)) {
          d->version = v;
          g_free(v);
        }
      }
      g_key_file_free(kf);
      break;
    }
    case Source::kSteam:
      SteamGameSizes(d->source.id, &d->app_size, &d->data_size);
      break;
    case Source::kWaydroid: {
      // The APKs and their native libraries (Android keeps its compiled
      // code, oat/, and the app's data from the user).
      const std::string dir = WaydroidAppDir(d->source.id);
      d->app_size = dir.empty() ? 0 : ReadableTreeSize(dir);
      break;
    }
    case Source::kNone:
      break;
  }
  g_task_return_boolean(task, TRUE);
}

void OnDetailsFound(GObject*, GAsyncResult* result, gpointer data) {
  GDBusMethodInvocation* invocation = static_cast<GDBusMethodInvocation*>(data);
  Details* d =
      static_cast<Details*>(g_task_get_task_data(G_TASK(result)));
  g_dbus_method_invocation_return_value(
      invocation,
      g_variant_new("(stt)", d->version.c_str(), d->app_size, d->data_size));
}

void HandleGetAppDetails(GDBusMethodInvocation* invocation,
                         GVariant* parameters) {
  const gchar* desktop_file_id = nullptr;
  g_variant_get(parameters, "(&s)", &desktop_file_id);
  std::string file_id = std::string(desktop_file_id) + ".desktop";
  GDesktopAppInfo* desktop = g_desktop_app_info_new(file_id.c_str());
  if (!desktop) {
    g_dbus_method_invocation_return_value(invocation,
                                          g_variant_new("(stt)", "", 0, 0));
    return;
  }
  Details* d = new Details;
  d->source = FindSource(desktop);
  d->desktop_path = g_desktop_app_info_get_filename(desktop);
  g_object_unref(desktop);
  GTask* task = g_task_new(nullptr, nullptr, OnDetailsFound, invocation);
  g_task_set_task_data(task, d, [](gpointer p) {
    delete static_cast<Details*>(p);
  });
  g_task_run_in_thread(task, FindDetails);
  g_object_unref(task);
}

void OnUninstalled(GObject* source, GAsyncResult* result, gpointer data) {
  gchar* what = static_cast<gchar*>(data);
  GError* error = nullptr;
  GSubprocess* process = G_SUBPROCESS(source);
  if (!g_subprocess_wait_check_finish(process, result, &error)) {
    NICKEL_WARN("uninstalling %s failed: %s", what, error->message);
    g_error_free(error);
  } else {
    NICKEL_INFO("uninstalled %s", what);
  }
  g_free(what);
}

void HandleUninstallApplication(GDBusMethodInvocation* invocation,
                                GVariant* parameters) {
  const gchar* desktop_file_id = nullptr;
  g_variant_get(parameters, "(&s)", &desktop_file_id);
  std::string file_id = std::string(desktop_file_id) + ".desktop";
  GDesktopAppInfo* desktop = g_desktop_app_info_new(file_id.c_str());
  if (!desktop) {
    g_dbus_method_invocation_return_error(invocation, G_DBUS_ERROR,
                                          G_DBUS_ERROR_INVALID_ARGS,
                                          "no app %s", desktop_file_id);
    return;
  }
  Source source = FindSource(desktop);
  std::vector<const char*> argv;
  gchar* name = g_path_get_basename(g_desktop_app_info_get_filename(desktop));
  // A Steam game: the same Steam's uninstall (it asks, then removes the
  // game; steam.cc then removes the desktop file).
  gchar** steam_argv = nullptr;
  if (source.kind == Source::kSteam) {
    gchar* exec = g_desktop_app_info_get_string(desktop, "Exec");
    std::string command = exec ? exec : "";
    g_free(exec);
    const std::string run = "steam://rungameid/";
    size_t at = command.find(run);
    if (at != std::string::npos) {
      command.replace(at, run.size(), "steam://uninstall/");
    }
    if (g_shell_parse_argv(command.c_str(), nullptr, &steam_argv, nullptr)) {
      for (gchar** a = steam_argv; *a; ++a) {
        argv.push_back(*a);
      }
    }
  }
  switch (source.kind) {
    case Source::kFlatpak:
      argv = {"flatpak", "uninstall", source.system ? "--system" : "--user",
              "--noninteractive", "-y", source.id.c_str()};
      break;
    case Source::kGentoo:
      argv = {"pkexec", "/usr/libexec/nickel-gentoo-uninstall", name};
      break;
    case Source::kAppImage:
      if (g_unlink(source.id.c_str()) != 0 ||
          g_unlink(g_desktop_app_info_get_filename(desktop)) != 0) {
        NICKEL_WARN("removing AppImage %s failed", source.id.c_str());
      } else {
        NICKEL_INFO("removed AppImage %s", source.id.c_str());
      }
      break;
    case Source::kSteam:
      break;
    case Source::kWaydroid:
      // Through Android's package manager, in the user's Android (started
      // if it isn't running): nickel-waydroid's APK helper and
      // nickel-waydroidd. Waydroid's session then removes the desktop file.
      argv = {"/usr/libexec/nickel-waydroid/nickel-waydroid-apk", "--uninstall",
              source.id.c_str()};
      break;
    case Source::kNone:
      g_free(name);
      g_object_unref(desktop);
      g_dbus_method_invocation_return_error(
          invocation, G_DBUS_ERROR, G_DBUS_ERROR_NOT_SUPPORTED,
          "%s is part of NickelOS", desktop_file_id);
      return;
  }
  if (!argv.empty()) {
    argv.push_back(nullptr);
    GError* error = nullptr;
    GSubprocess* process = g_subprocess_newv(argv.data(),
                                             G_SUBPROCESS_FLAGS_NONE, &error);
    if (!process) {
      NICKEL_WARN("cannot uninstall %s: %s", desktop_file_id, error->message);
      g_error_free(error);
    } else {
      NICKEL_INFO("uninstalling %s (%s)", desktop_file_id,
                  PackageId(source).c_str());
      g_subprocess_wait_check_async(process, nullptr, OnUninstalled,
                                    g_strdup(desktop_file_id));
      g_object_unref(process);
    }
  }
  g_strfreev(steam_argv);
  g_free(name);
  g_object_unref(desktop);
  g_dbus_method_invocation_return_value(invocation, nullptr);
}

void HandleSetAppearance(State* state,
                         GDBusMethodInvocation* invocation,
                         GVariant* parameters) {
  gboolean dark = FALSE;
  guint32 accent = 0;
  g_variant_get(parameters, "(bu)", &dark, &accent);
  bool changed = !state->appearance_known || state->dark != !!dark ||
                 state->accent != accent;
  state->appearance_known = true;
  state->dark = dark;
  state->accent = accent;
  g_dbus_method_invocation_return_value(invocation, nullptr);
  if (changed) {
    NICKEL_INFO("appearance: %s, accent #%06x", dark ? "dark" : "light",
                accent & 0xffffff);
    g_dbus_connection_emit_signal(state->bus, nullptr, kObjectPath,
                                  kServiceName, "AppearanceChanged",
                                  g_variant_new("(bu)", dark, accent),
                                  nullptr);
  }
}

bool IsSessionUser(GDBusConnection* bus, const char* sender) {
  GVariant* reply = g_dbus_connection_call_sync(
      bus, "org.freedesktop.DBus", "/org/freedesktop/DBus",
      "org.freedesktop.DBus", "GetConnectionUnixUser",
      g_variant_new("(s)", sender), G_VARIANT_TYPE("(u)"),
      G_DBUS_CALL_FLAGS_NONE, 5000, nullptr, nullptr);
  if (!reply) {
    return false;
  }
  guint32 uid = 0;
  g_variant_get(reply, "(u)", &uid);
  g_variant_unref(reply);
  return uid == getuid();
}

// The user's default app for mail links (Settings > Apps > Default apps,
// Email): ~/.config/nickelos-mimeapps.list, which xdg-open, GIO and the
// portals read before the system's own list, so Linux apps' mail links go
// straight to it. An empty |desktop_file_id| gives them back to ChromeOS
// (org.nickelos.ChromeOS.desktop: nickel-open asks Chrome, which opens the
// website chosen there).
void HandleSetDefaultHandler(GDBusMethodInvocation* invocation,
                             GVariant* parameters) {
  const char* mime_type = nullptr;
  const char* desktop_file_id = nullptr;
  g_variant_get(parameters, "(&s&s)", &mime_type, &desktop_file_id);
  if (g_strcmp0(mime_type, "x-scheme-handler/mailto") != 0) {
    g_dbus_method_invocation_return_error(invocation, G_DBUS_ERROR,
                                          G_DBUS_ERROR_INVALID_ARGS,
                                          "only mail links (mailto)");
    return;
  }
  std::string id = *desktop_file_id ? desktop_file_id
                                    : "org.nickelos.ChromeOS.desktop";
  if (!g_str_has_suffix(id.c_str(), ".desktop")) {
    id += ".desktop";
  }
  g_autofree char* path = g_build_filename(g_get_user_config_dir(),
                                           "nickelos-mimeapps.list", nullptr);
  g_autoptr(GKeyFile) list = g_key_file_new();
  g_key_file_load_from_file(list, path, G_KEY_FILE_KEEP_COMMENTS, nullptr);
  g_key_file_set_string(list, "Default Applications", mime_type, id.c_str());
  g_mkdir_with_parents(g_get_user_config_dir(), 0700);
  g_autoptr(GError) error = nullptr;
  if (!g_key_file_save_to_file(list, path, &error)) {
    g_dbus_method_invocation_return_gerror(invocation, error);
    return;
  }
  NICKEL_INFO("%s opens with %s", mime_type, id.c_str());
  g_dbus_method_invocation_return_value(invocation, nullptr);
}

void OnMethodCall(GDBusConnection* bus,
                  const gchar* sender,
                  const gchar*,
                  const gchar*,
                  const gchar* method_name,
                  GVariant* parameters,
                  GDBusMethodInvocation* invocation,
                  gpointer user_data) {
  State* state = static_cast<State*>(user_data);
  if (!IsSessionUser(bus, sender)) {
    g_dbus_method_invocation_return_error_literal(
        invocation, G_DBUS_ERROR, G_DBUS_ERROR_ACCESS_DENIED,
        "only the session user may call this");
    return;
  }
  if (g_strcmp0(method_name, "LaunchApplication") == 0) {
    HandleLaunchApplication(invocation, parameters);
    return;
  }
  if (g_strcmp0(method_name, "LaunchTool") == 0) {
    HandleLaunchTool(invocation, parameters);
    return;
  }
  if (g_strcmp0(method_name, "SpawnShell") == 0) {
    HandleSpawnShell(invocation, parameters);
    return;
  }
  if (g_strcmp0(method_name, "SetAppearance") == 0) {
    HandleSetAppearance(state, invocation, parameters);
    return;
  }
  if (g_strcmp0(method_name, "GetAppDetails") == 0) {
    HandleGetAppDetails(invocation, parameters);
    return;
  }
  if (g_strcmp0(method_name, "SetDefaultHandler") == 0) {
    HandleSetDefaultHandler(invocation, parameters);
    return;
  }
  if (g_strcmp0(method_name, "UninstallApplication") == 0) {
    HandleUninstallApplication(invocation, parameters);
    return;
  }
  if (g_strcmp0(method_name, "FileSelected") == 0 ||
      g_strcmp0(method_name, "NotificationEvent") == 0) {
    g_dbus_connection_emit_signal(state->bus, nullptr, kObjectPath,
                                  kServiceName, method_name, parameters,
                                  nullptr);
    g_dbus_method_invocation_return_value(invocation, nullptr);
    return;
  }
  if (g_strcmp0(method_name, "TrayEvent") == 0) {
    const char *key = nullptr, *event = nullptr, *token = nullptr;
    gint32 item = 0;
    g_variant_get(parameters, "(&s&si&s)", &key, &event, &item, &token);
    if (state->tray) {
      TrayEvent(state->tray, key, event, item, token);
    }
    g_dbus_method_invocation_return_value(invocation, nullptr);
    return;
  }
  if (g_strcmp0(method_name, "Thumbnail") == 0) {
    HandleThumbnail(state->thumbnails, invocation, parameters);
    return;
  }
  if (g_strcmp0(method_name, "GetAppearance") == 0) {
    g_dbus_method_invocation_return_value(
        invocation, g_variant_new("(bbu)", state->appearance_known,
                                  state->dark, state->accent));
    return;
  }
  g_dbus_method_invocation_return_error(invocation, G_DBUS_ERROR,
                                        G_DBUS_ERROR_UNKNOWN_METHOD,
                                        "unknown method %s", method_name);
}

void OnNameLost(GDBusConnection*, const gchar* name, gpointer user_data) {
  NICKEL_WARN("lost or could not own %s", name);
  g_main_loop_quit(static_cast<GMainLoop*>(user_data));
}

}  // namespace

std::string NickelAppId(const std::string& desktop_file_id) {
  return AppId(desktop_file_id);
}

int main(int argc, char** argv) {
  openlog("nickel-appd", LOG_PID, LOG_DAEMON);

  State state;
  state.icon_dir = "/home/chronos/user/crostini.icons";
  GOptionEntry options[] = {
      {"icon-dir", 0, 0, G_OPTION_ARG_FILENAME, nullptr,
       "Chrome profile's crostini.icons directory", "DIR"},
      {nullptr}};
  gchar* icon_dir = nullptr;
  options[0].arg_data = &icon_dir;
  GOptionContext* option_context = g_option_context_new(nullptr);
  g_option_context_add_main_entries(option_context, options, nullptr);
  GError* error = nullptr;
  if (!g_option_context_parse(option_context, &argc, &argv, &error)) {
    g_printerr("%s\n", error->message);
    return 2;
  }
  g_option_context_free(option_context);
  if (icon_dir) {
    state.icon_dir = icon_dir;
    g_free(icon_dir);
  }

  GMainLoop* loop = g_main_loop_new(nullptr, FALSE);
  state.thumbnails = ThumbnailsNew();
  state.bus = g_bus_get_sync(G_BUS_TYPE_SYSTEM, nullptr, &error);
  if (!state.bus) {
    NICKEL_WARN("no system bus: %s", error->message);
    return 1;
  }

  GDBusNodeInfo* node = g_dbus_node_info_new_for_xml(kIntrospection, nullptr);
  static const GDBusInterfaceVTable kVTable = {OnMethodCall, nullptr, nullptr};
  if (!g_dbus_connection_register_object(state.bus, kObjectPath,
                                         node->interfaces[0], &kVTable,
                                         &state, nullptr, &error)) {
    NICKEL_WARN("cannot export %s: %s", kObjectPath, error->message);
    return 1;
  }
  g_bus_own_name_on_connection(state.bus, kServiceName,
                               G_BUS_NAME_OWNER_FLAGS_NONE, nullptr,
                               OnNameLost, loop, nullptr);

  // The location for Linux apps: GeoClue's agent.
  LocationAgentStart(state.bus);

  // Linux apps register their tray icons on the session bus.
  GDBusConnection* session = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr,
                                            &error);
  if (session) {
    state.tray = TrayNew(session, state.bus);
    AutostartRun(session);
  } else {
    NICKEL_WARN("no session bus, so no tray: %s", error->message);
    g_clear_error(&error);
  }

  // Steam's games, before the first list.
  state.steam = SteamGamesNew();

  // The monitor only reports changes after the app list has been read once,
  // which the first SendApplicationList() does.
  g_signal_connect(g_app_info_monitor_get(), "changed",
                   G_CALLBACK(OnAppsChanged), &state);
  SendApplicationList(&state);

  g_main_loop_run(loop);
  return 0;
}
