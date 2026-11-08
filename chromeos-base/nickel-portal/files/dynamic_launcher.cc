// Distributed under the terms of the GNU General Public License v2
//
// DynamicLauncher (org.freedesktop.impl.portal.DynamicLauncher) for
// xdg-desktop-portal-nickel: an app adds a launcher of its own to the
// user's apps: a browser installing a website as a web app (GNOME Web,
// Chromium's and Chrome's Flatpaks), Bottles a Windows program, a game
// launcher a game. PrepareInstall asks the user in ChromeOS's dialog
// (access.cc's AccessAsk); if they add it, xdg-desktop-portal writes the
// .desktop file and its icon (~/.local/share/xdg-desktop-portal/applications
// and icons, linked into ~/.local/share/applications), where nickel-appd
// finds it and puts it in ChromeOS's launcher. The name and icon are the
// app's (the dialog can't edit them).
//
// RequestInstallToken, adding without asking, is for nothing on NickelOS:
// every app asks (as GNOME has it for all but its own software store).

#include "dynamic_launcher.h"

#include <syslog.h>

#include <string>

#include "access.h"

namespace {

// Portal responses.
constexpr guint32 kSuccess = 0;
constexpr guint32 kCanceled = 1;
constexpr guint32 kOther = 2;
// Launcher types: an application, a web app.
constexpr guint32 kApplication = 1;
constexpr guint32 kWebApp = 2;

struct Ask {
  GDBusMethodInvocation* invocation;
  std::string app_id;
  std::string name;
  GVariant* icon;
};

void OnAnswered(bool allowed, gpointer data) {
  Ask* ask = static_cast<Ask*>(data);
  syslog(LOG_INFO, "launcher: %s: \"%s\" %s", ask->app_id.c_str(),
         ask->name.c_str(), allowed ? "added" : "not added");
  GVariantBuilder results;
  g_variant_builder_init(&results, G_VARIANT_TYPE("a{sv}"));
  if (allowed) {
    g_variant_builder_add(&results, "{sv}", "name",
                          g_variant_new_string(ask->name.c_str()));
    g_variant_builder_add(&results, "{sv}", "icon",
                          g_variant_new_variant(ask->icon));
  }
  g_dbus_method_invocation_return_value(
      ask->invocation,
      g_variant_new("(ua{sv})", allowed ? kSuccess : kCanceled, &results));
  g_variant_unref(ask->icon);
  delete ask;
}

}  // namespace

struct DynamicLauncher {
  GDBusConnection* system = nullptr;
};

DynamicLauncher* DynamicLauncherNew(GDBusConnection* system) {
  DynamicLauncher* launcher = new DynamicLauncher;
  launcher->system = system;
  return launcher;
}

// PrepareInstall(o handle, s app_id, s parent_window, s name, v icon_v,
//   a{sv} options{launcher_type, target, ...}) -> (u response, a{sv}
//   results{name, icon})
// RequestInstallToken(s app_id, a{sv} options) -> (u response)
void OnDynamicLauncherCall(GDBusConnection*,
                           const gchar*,
                           const gchar*,
                           const gchar*,
                           const gchar* method_name,
                           GVariant* parameters,
                           GDBusMethodInvocation* invocation,
                           gpointer data) {
  DynamicLauncher* launcher = static_cast<DynamicLauncher*>(data);
  if (g_strcmp0(method_name, "RequestInstallToken") == 0) {
    g_dbus_method_invocation_return_value(invocation,
                                          g_variant_new("(u)", kOther));
    return;
  }
  if (g_strcmp0(method_name, "PrepareInstall") != 0) {
    g_dbus_method_invocation_return_error(invocation, G_DBUS_ERROR,
                                          G_DBUS_ERROR_UNKNOWN_METHOD,
                                          "unknown method %s", method_name);
    return;
  }
  const gchar* app_id = nullptr;
  const gchar* name = nullptr;
  GVariant* icon = nullptr;
  g_autoptr(GVariant) options = nullptr;
  g_variant_get(parameters, "(&o&s&s&sv@a{sv})", nullptr, &app_id, nullptr,
                &name, &icon, &options);
  guint32 type = kApplication;
  g_variant_lookup(options, "launcher_type", "u", &type);
  const gchar* target = nullptr;
  g_variant_lookup(options, "target", "&s", &target);

  const std::string app = AccessAppName(app_id);
  const std::string who = app.empty() ? "An app" : app;
  std::string text;
  if (type == kWebApp && target && *target) {
    text = who + " wants to add the web app “" + name + "” (" + target +
           ") to your apps.";
  } else {
    text = who + " wants to add “" + std::string(name) + "” to your apps.";
  }
  text += " It will be in the launcher like your other apps.";
  AccessAsk(launcher->system, app_id,
            "Add “" + std::string(name) + "” to your apps?", text, "Add",
            "Cancel", OnAnswered, new Ask{invocation, app_id, name, icon});
}

GVariant* OnDynamicLauncherProperty(GDBusConnection*,
                                    const gchar*,
                                    const gchar*,
                                    const gchar*,
                                    const gchar* property_name,
                                    GError** error,
                                    gpointer) {
  if (g_strcmp0(property_name, "SupportedLauncherTypes") == 0) {
    return g_variant_new_uint32(kApplication | kWebApp);
  }
  if (g_strcmp0(property_name, "version") == 0) {
    return g_variant_new_uint32(1);
  }
  g_set_error(error, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_PROPERTY,
              "unknown property %s", property_name);
  return nullptr;
}
