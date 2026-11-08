// Distributed under the terms of the GNU General Public License v2
//
// Autostart, as the XDG Autostart Specification has it: at sign-in,
// nickel-appd starts the entries in ~/.config/autostart (an app's own "start
// at sign-in" setting, such as Discord's or Steam's, and the Background
// portal's for Flatpak apps, which xdg-desktop-portal writes there) and in
// the system's autostart directories (/etc/xdg/autostart); a user entry
// overrides a system one of the same name, which is how an entry is turned
// off (Hidden=true).
//
// Not started: Hidden=true, X-GNOME-Autostart-enabled=false, an OnlyShowIn
// without NickelOS or a NotShowIn with it (XDG_CURRENT_DESKTOP), a TryExec
// that isn't there, an AutostartCondition "if-exists"/"unless-exists" (a
// file in ~/.config) not met; and what NickelOS starts itself or has no use
// for: Valent (nickel-phone starts it), Evolution's alarm notifier (no
// Evolution calendar on NickelOS). X-GNOME-Autostart-Delay is honoured.
//
// Once per sign-in, after the session's Wayland and X11 displays are up:
// nickel-appd may be restarted within a session, and the session bus's GUID
// (a new bus each sign-in) marks the sign-in it already started them in.

#include "autostart.h"

#include <glib/gstdio.h>
#include <gio/gdesktopappinfo.h>
#include <syslog.h>

#include <map>
#include <string>

namespace {

constexpr char kGroup[] = "Desktop Entry";
constexpr char kDesktop[] = "NickelOS";
// Waiting for the displays: checked every second, at most a minute.
constexpr guint kDisplayWaitSeconds = 60;
// Once they are there, a moment for the shell to settle.
constexpr guint kSettleSeconds = 3;

// Entries NickelOS starts itself or has no use for.
constexpr const char* kNotStarted[] = {
    "ca.andyholmes.Valent-autostart.desktop",
    "org.gnome.Evolution-alarm-notify.desktop",
};

struct Run {
  std::string marker;
  guint waited = 0;
};

bool DisplaysUp() {
  const char* wayland = g_getenv("WAYLAND_DISPLAY");
  if (wayland && *wayland) {
    g_autofree gchar* socket =
        g_path_is_absolute(wayland)
            ? g_strdup(wayland)
            : g_build_filename(g_get_user_runtime_dir(), wayland, nullptr);
    if (!g_file_test(socket, G_FILE_TEST_EXISTS)) {
      return false;
    }
  }
  const char* x11 = g_getenv("DISPLAY");
  if (x11 && x11[0] == ':') {
    std::string number(x11 + 1);
    number = number.substr(0, number.find('.'));
    std::string socket = "/tmp/.X11-unix/X" + number;
    if (!g_file_test(socket.c_str(), G_FILE_TEST_EXISTS)) {
      return false;
    }
  }
  return true;
}

bool ListHas(GKeyFile* entry, const char* key, const char* value) {
  g_auto(GStrv) list =
      g_key_file_get_string_list(entry, kGroup, key, nullptr, nullptr);
  for (gchar** item = list; item && *item; ++item) {
    if (g_strcmp0(*item, value) == 0) {
      return true;
    }
  }
  return false;
}

bool OnThisDesktop(GKeyFile* entry) {
  if (g_key_file_has_key(entry, kGroup, "OnlyShowIn", nullptr) &&
      !ListHas(entry, "OnlyShowIn", kDesktop)) {
    return false;
  }
  return !ListHas(entry, "NotShowIn", kDesktop);
}

// "if-exists FILE" / "unless-exists FILE", FILE in ~/.config; other
// conditions (GNOME's GSettings keys) don't apply here and count as met.
bool ConditionMet(GKeyFile* entry) {
  g_autofree gchar* condition =
      g_key_file_get_string(entry, kGroup, "AutostartCondition", nullptr);
  if (!condition) {
    return true;
  }
  g_auto(GStrv) words = g_strsplit(g_strstrip(condition), " ", 2);
  if (!words[0] || !words[1]) {
    return true;
  }
  g_autofree gchar* file =
      g_build_filename(g_get_user_config_dir(), words[1], nullptr);
  const bool exists = g_file_test(file, G_FILE_TEST_EXISTS);
  if (g_ascii_strcasecmp(words[0], "if-exists") == 0) {
    return exists;
  }
  if (g_ascii_strcasecmp(words[0], "unless-exists") == 0) {
    return !exists;
  }
  return true;
}

bool NotStarted(const std::string& name) {
  for (const char* skipped : kNotStarted) {
    if (name == skipped) {
      return true;
    }
  }
  return false;
}

// Every autostart entry by file name, the user's first.
std::map<std::string, std::string> Entries() {
  std::map<std::string, std::string> entries;
  auto add_dir = [&entries](const std::string& dir) {
    GDir* listing = g_dir_open(dir.c_str(), 0, nullptr);
    if (!listing) {
      return;
    }
    while (const gchar* name = g_dir_read_name(listing)) {
      if (g_str_has_suffix(name, ".desktop") && !entries.count(name)) {
        entries[name] = dir + "/" + name;
      }
    }
    g_dir_close(listing);
  };
  add_dir(std::string(g_get_user_config_dir()) + "/autostart");
  for (const gchar* const* dir = g_get_system_config_dirs(); *dir; ++dir) {
    add_dir(std::string(*dir) + "/autostart");
  }
  return entries;
}

void Launch(const std::string& path) {
  g_autoptr(GDesktopAppInfo) app =
      g_desktop_app_info_new_from_filename(path.c_str());
  if (!app) {
    // TryExec missing, or not a valid entry.
    syslog(LOG_INFO, "autostart: %s: not startable", path.c_str());
    return;
  }
  g_autoptr(GAppLaunchContext) context = g_app_launch_context_new();
  g_autoptr(GError) error = nullptr;
  if (g_app_info_launch(G_APP_INFO(app), nullptr, context, &error)) {
    syslog(LOG_INFO, "autostart: started %s", path.c_str());
  } else {
    syslog(LOG_WARNING, "autostart: %s: %s", path.c_str(), error->message);
  }
}

gboolean OnDelayed(gpointer data) {
  std::string* path = static_cast<std::string*>(data);
  Launch(*path);
  delete path;
  return G_SOURCE_REMOVE;
}

void StartAll() {
  for (const auto& [name, path] : Entries()) {
    g_autoptr(GKeyFile) entry = g_key_file_new();
    if (!g_key_file_load_from_file(entry, path.c_str(), G_KEY_FILE_NONE,
                                   nullptr)) {
      continue;
    }
    if (g_key_file_get_boolean(entry, kGroup, "Hidden", nullptr) ||
        (g_key_file_has_key(entry, kGroup, "X-GNOME-Autostart-enabled",
                             nullptr) &&
         !g_key_file_get_boolean(entry, kGroup, "X-GNOME-Autostart-enabled",
                                 nullptr)) ||
        !OnThisDesktop(entry) || !ConditionMet(entry) || NotStarted(name)) {
      continue;
    }
    const gint delay = g_key_file_get_integer(entry, kGroup,
                                              "X-GNOME-Autostart-Delay",
                                              nullptr);
    if (delay > 0) {
      g_timeout_add_seconds(static_cast<guint>(delay), OnDelayed,
                            new std::string(path));
    } else {
      Launch(path);
    }
  }
}

gboolean OnSettled(gpointer data) {
  StartAll();
  delete static_cast<Run*>(data);
  return G_SOURCE_REMOVE;
}

gboolean OnWait(gpointer data) {
  Run* run = static_cast<Run*>(data);
  if (!DisplaysUp() && ++run->waited < kDisplayWaitSeconds) {
    return G_SOURCE_CONTINUE;
  }
  if (run->waited >= kDisplayWaitSeconds) {
    syslog(LOG_WARNING, "autostart: no display after %us; starting anyway",
           kDisplayWaitSeconds);
  }
  g_file_set_contents(run->marker.c_str(), "", 0, nullptr);
  g_timeout_add_seconds(kSettleSeconds, OnSettled, run);
  return G_SOURCE_REMOVE;
}

}  // namespace

void AutostartRun(GDBusConnection* session) {
  const gchar* guid = session ? g_dbus_connection_get_guid(session) : nullptr;
  if (!guid) {
    syslog(LOG_WARNING, "autostart: no session bus; nothing started");
    return;
  }
  Run* run = new Run;
  run->marker = std::string(g_get_user_runtime_dir()) +
                "/nickel-autostart-" + guid;
  if (g_file_test(run->marker.c_str(), G_FILE_TEST_EXISTS)) {
    // Already started in this sign-in (nickel-appd was restarted).
    delete run;
    return;
  }
  g_timeout_add_seconds(1, OnWait, run);
}
