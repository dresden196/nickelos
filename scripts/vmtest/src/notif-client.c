/* NickelOS: shows a notification the way an app does and prints what comes
 * back while it stays connected (the server sends its signals only to the
 * app that asked).
 *   notif-client fdo|portal [urgent] [timeout_s]
 * fdo: org.freedesktop.Notifications Notify, as libnotify does for native
 * apps. portal: the portal backend's AddNotification, as xdg-desktop-portal
 * passes on a Flatpak app's.
 * Prints "id N", then each signal ("closed ID REASON", "action ID KEY",
 * "token ID set|empty"; portal: "portal-action APP ID KEY") until it is
 * closed or acted on, or "timeout".
 * Build: see vmtest/notifications-test.sh. */
#include <gio/gio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static GMainLoop *loop;

static void on_fdo(GDBusConnection *bus, const char *sender, const char *path,
                   const char *iface, const char *signal, GVariant *params,
                   gpointer data) {
  guint32 id;
  if (strcmp(signal, "NotificationClosed") == 0) {
    guint32 reason;
    g_variant_get(params, "(uu)", &id, &reason);
    g_print("closed %u %u\n", id, reason);
    g_main_loop_quit(loop);
  } else if (strcmp(signal, "ActionInvoked") == 0) {
    const char *key;
    g_variant_get(params, "(u&s)", &id, &key);
    g_print("action %u %s\n", id, key);
  } else if (strcmp(signal, "ActivationToken") == 0) {
    const char *token;
    g_variant_get(params, "(u&s)", &id, &token);
    g_print("token %u %s\n", id, *token ? "set" : "empty");
  }
}

static void on_portal(GDBusConnection *bus, const char *sender,
                      const char *path, const char *iface, const char *signal,
                      GVariant *params, gpointer data) {
  const char *app, *id, *action;
  g_variant_get(params, "(&s&s&sav)", &app, &id, &action, NULL);
  g_print("portal-action %s %s %s\n", app, id, action);
  g_main_loop_quit(loop);
}

static gboolean on_timeout(gpointer data) {
  g_print("timeout\n");
  g_main_loop_quit(loop);
  return G_SOURCE_REMOVE;
}

int main(int argc, char **argv) {
  if (argc < 2) {
    g_printerr("usage: notif-client fdo|portal [urgent] [timeout_s]\n");
    return 2;
  }
  gboolean urgent = argc > 2 && strcmp(argv[2], "urgent") == 0;
  int timeout = argc > 3 ? atoi(argv[3]) : 60;
  GError *error = NULL;
  GDBusConnection *bus = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &error);
  if (!bus) {
    g_printerr("%s\n", error->message);
    return 1;
  }
  loop = g_main_loop_new(NULL, FALSE);
  setvbuf(stdout, NULL, _IOLBF, 0);

  GVariant *reply;
  if (strcmp(argv[1], "fdo") == 0) {
    g_dbus_connection_signal_subscribe(
        bus, "org.freedesktop.Notifications", "org.freedesktop.Notifications",
        NULL, "/org/freedesktop/Notifications", NULL, G_DBUS_SIGNAL_FLAGS_NONE,
        on_fdo, NULL, NULL);
    const char *actions[] = {"default", "", "open", "Open folder", NULL};
    GVariantBuilder hints;
    g_variant_builder_init(&hints, G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(&hints, "{sv}", "desktop-entry",
                          g_variant_new_string("org.gnome.Loupe"));
    if (urgent)
      g_variant_builder_add(&hints, "{sv}", "urgency", g_variant_new_byte(2));
    reply = g_dbus_connection_call_sync(
        bus, "org.freedesktop.Notifications", "/org/freedesktop/Notifications",
        "org.freedesktop.Notifications", "Notify",
        g_variant_new("(susss^asa{sv}i)", "Image Viewer", 0, "",
                      "Photo saved",
                      "vacation.jpg was saved to <b>Downloads</b> &amp; is "
                      "ready",
                      actions, &hints, urgent ? 0 : 8000),
        G_VARIANT_TYPE("(u)"), G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error);
    if (!reply) {
      g_printerr("%s\n", error->message);
      return 1;
    }
    guint32 id;
    g_variant_get(reply, "(u)", &id);
    g_print("id %u\n", id);
  } else {
    g_dbus_connection_signal_subscribe(
        bus, NULL, "org.freedesktop.impl.portal.Notification", "ActionInvoked",
        "/org/freedesktop/portal/desktop", NULL, G_DBUS_SIGNAL_FLAGS_NONE,
        on_portal, NULL, NULL);
    GVariantBuilder n, buttons, button;
    g_variant_builder_init(&button, G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(&button, "{sv}", "label",
                          g_variant_new_string("Open"));
    g_variant_builder_add(&button, "{sv}", "action",
                          g_variant_new_string("app.open"));
    g_variant_builder_init(&buttons, G_VARIANT_TYPE("aa{sv}"));
    g_variant_builder_add_value(&buttons, g_variant_builder_end(&button));
    g_variant_builder_init(&n, G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(&n, "{sv}", "title",
                          g_variant_new_string("Download finished"));
    g_variant_builder_add(&n, "{sv}", "body",
                          g_variant_new_string("ubuntu.iso is ready"));
    g_variant_builder_add(&n, "{sv}", "priority",
                          g_variant_new_string(urgent ? "urgent" : "normal"));
    g_variant_builder_add(&n, "{sv}", "default-action",
                          g_variant_new_string("app.show"));
    g_variant_builder_add(&n, "{sv}", "buttons",
                          g_variant_builder_end(&buttons));
    reply = g_dbus_connection_call_sync(
        bus, "org.freedesktop.impl.portal.desktop.nickel",
        "/org/freedesktop/portal/desktop",
        "org.freedesktop.impl.portal.Notification", "AddNotification",
        g_variant_new("(ssa{sv})", "org.gnome.Loupe", "dl1", &n), NULL,
        G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error);
    if (!reply) {
      g_printerr("%s\n", error->message);
      return 1;
    }
    g_print("id dl1\n");
  }
  g_variant_unref(reply);
  g_timeout_add_seconds(timeout, on_timeout, NULL);
  g_main_loop_run(loop);
  return 0;
}
