// Distributed under the terms of the GNU General Public License v2
//
// Wallpaper (org.freedesktop.impl.portal.Wallpaper) for
// xdg-desktop-portal-nickel: an app sets a picture as the wallpaper
// (SetWallpaperURI: GNOME's Image Viewer's "Set as Background", wallpaper
// apps). xdg-desktop-portal asks the user first (our Access dialog) and
// remembers the answer for the app, unless the app asks for a preview: then
// it leaves asking to the backend, and ChromeOS's dialog asks here. Chrome
// sets the picture as the user's wallpaper (VmApplicationsService.
// NickelSetWallpaper, Chrome patch 0067), as Wallpaper & style's "My
// images" does: it keeps its own copy, filling the screen. ChromeOS has one
// wallpaper for the desktop and the lock screen (set-on is not asked).

#include "wallpaper.h"

#include <chromeos/dbus/vm_applications/dbus-constants.h>
#include <syslog.h>

#include <string>

#include "access.h"

namespace {

constexpr char kSetMethod[] = "NickelSetWallpaper";
// Portal responses.
constexpr guint32 kSuccess = 0;
constexpr guint32 kCanceled = 1;
constexpr guint32 kFailed = 2;

void Reply(GDBusMethodInvocation* invocation, guint32 response) {
  g_dbus_method_invocation_return_value(invocation,
                                        g_variant_new("(u)", response));
}

}  // namespace

struct Wallpaper {
  GDBusConnection* system = nullptr;
};

namespace {

struct Set {
  Wallpaper* wallpaper;
  GDBusMethodInvocation* invocation;
  std::string app_id;
  std::string path;
};

void OnSet(GObject* source, GAsyncResult* result, gpointer data) {
  Set* set = static_cast<Set*>(data);
  g_autoptr(GError) error = nullptr;
  g_autoptr(GVariant) reply =
      g_dbus_connection_call_finish(G_DBUS_CONNECTION(source), result, &error);
  gboolean done = FALSE;
  if (reply) {
    g_variant_get(reply, "(b)", &done);
  } else {
    syslog(LOG_WARNING, "wallpaper: %s: %s", kSetMethod, error->message);
  }
  syslog(LOG_INFO, "wallpaper: %s set %s: %s", set->app_id.c_str(),
         set->path.c_str(), done ? "done" : "failed");
  Reply(set->invocation, done ? kSuccess : kFailed);
  delete set;
}

void SetIt(Set* set) {
  g_dbus_connection_call(
      set->wallpaper->system, vm_tools::apps::kVmApplicationsServiceName,
      vm_tools::apps::kVmApplicationsServicePath,
      vm_tools::apps::kVmApplicationsServiceInterface, kSetMethod,
      g_variant_new("(s)", set->path.c_str()), G_VARIANT_TYPE("(b)"),
      G_DBUS_CALL_FLAGS_NONE, -1, nullptr, OnSet, set);
}

void OnAnswered(bool allowed, gpointer data) {
  Set* set = static_cast<Set*>(data);
  if (!allowed) {
    Reply(set->invocation, kCanceled);
    delete set;
    return;
  }
  SetIt(set);
}

}  // namespace

Wallpaper* WallpaperNew(GDBusConnection* system) {
  Wallpaper* wallpaper = new Wallpaper;
  wallpaper->system = system;
  return wallpaper;
}

// SetWallpaperURI(o handle, s app_id, s parent_window, s uri,
//   a{sv} options{show-preview, set-on}) -> (u response)
void OnWallpaperCall(GDBusConnection*,
                     const gchar*,
                     const gchar*,
                     const gchar*,
                     const gchar* method_name,
                     GVariant* parameters,
                     GDBusMethodInvocation* invocation,
                     gpointer data) {
  if (g_strcmp0(method_name, "SetWallpaperURI") != 0) {
    g_dbus_method_invocation_return_error(invocation, G_DBUS_ERROR,
                                          G_DBUS_ERROR_UNKNOWN_METHOD,
                                          "unknown method %s", method_name);
    return;
  }
  Wallpaper* wallpaper = static_cast<Wallpaper*>(data);
  const gchar* app_id = nullptr;
  const gchar* uri = nullptr;
  g_autoptr(GVariant) options = nullptr;
  g_variant_get(parameters, "(&o&s&s&s@a{sv})", nullptr, &app_id, nullptr,
                &uri, &options);
  g_autofree gchar* path = g_filename_from_uri(uri, nullptr, nullptr);
  if (!path || !wallpaper->system) {
    // Pictures on the web are not taken: the app has the file.
    syslog(LOG_WARNING, "wallpaper: %s: not a local file: %s", app_id, uri);
    Reply(invocation, kFailed);
    return;
  }
  Set* set = new Set{wallpaper, invocation, app_id, path};
  gboolean preview = FALSE;
  g_variant_lookup(options, "show-preview", "b", &preview);
  if (!preview) {
    // xdg-desktop-portal asked already.
    SetIt(set);
    return;
  }
  const std::string name = AccessAppName(app_id);
  g_autofree gchar* file = g_path_get_basename(path);
  AccessAsk(wallpaper->system, app_id, "Set this picture as your wallpaper?",
            (name.empty() ? std::string("An app") : name) +
                " wants to set “" + file + "” as your wallpaper.",
            "Set wallpaper", "Cancel", OnAnswered, set);
}

GVariant* OnWallpaperProperty(GDBusConnection*,
                              const gchar*,
                              const gchar*,
                              const gchar*,
                              const gchar* property_name,
                              GError** error,
                              gpointer) {
  if (g_strcmp0(property_name, "version") == 0) {
    return g_variant_new_uint32(1);
  }
  g_set_error(error, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_PROPERTY,
              "unknown property %s", property_name);
  return nullptr;
}
