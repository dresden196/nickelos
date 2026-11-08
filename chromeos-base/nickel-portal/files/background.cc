// Distributed under the terms of the GNU General Public License v2
//
// Background (org.freedesktop.impl.portal.Background) for
// xdg-desktop-portal-nickel: with it, xdg-desktop-portal offers the
// Background portal, which apps (Flatpaks above all) ask to run in the
// background and to start at sign-in (RequestBackground). xdg-desktop-portal
// decides itself (its "background" permission; our Access dialog when the
// permission is "ask"; a first request is allowed) and writes the autostart
// entry, ~/.config/autostart/<app id>.desktop, which nickel-appd starts at
// sign-in.
//
// GetAppState: xdg-desktop-portal watches the apps running without a
// window that never asked, to warn the user about them or stop them.
// NickelOS reports no apps, so none is watched: an app that keeps running
// with its window closed (a tray icon on the shelf) is left alone, as on
// Windows and macOS. NotifyBackground is then never asked; it would answer
// "allow". EnableAutostart: older xdg-desktop-portal had the backend write
// the entry; it writes it itself now, so this one answers that it didn't.

#include "background.h"

#include <syslog.h>

namespace {

// NotifyBackground's result: allow.
constexpr guint32 kAllow = 1;

}  // namespace

struct Background {};

Background* BackgroundNew() {
  return new Background;
}

void OnBackgroundCall(GDBusConnection*,
                      const gchar*,
                      const gchar*,
                      const gchar*,
                      const gchar* method_name,
                      GVariant* parameters,
                      GDBusMethodInvocation* invocation,
                      gpointer) {
  if (g_strcmp0(method_name, "GetAppState") == 0) {
    g_dbus_method_invocation_return_value(
        invocation,
        g_variant_new("(@a{sv})", g_variant_new_array(G_VARIANT_TYPE("{sv}"),
                                                      nullptr, 0)));
  } else if (g_strcmp0(method_name, "NotifyBackground") == 0) {
    const gchar* app_id = nullptr;
    g_variant_get(parameters, "(&o&s&s)", nullptr, &app_id, nullptr);
    syslog(LOG_INFO, "background: %s runs in the background: allowed", app_id);
    GVariantBuilder results;
    g_variant_builder_init(&results, G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(&results, "{sv}", "result",
                          g_variant_new_uint32(kAllow));
    g_dbus_method_invocation_return_value(
        invocation, g_variant_new("(ua{sv})", 0u, &results));
  } else if (g_strcmp0(method_name, "EnableAutostart") == 0) {
    g_dbus_method_invocation_return_value(invocation,
                                          g_variant_new("(b)", FALSE));
  } else {
    g_dbus_method_invocation_return_error(invocation, G_DBUS_ERROR,
                                          G_DBUS_ERROR_UNKNOWN_METHOD,
                                          "unknown method %s", method_name);
  }
}
