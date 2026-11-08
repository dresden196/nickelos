// Runs in the test VM as the session user (nickel-session-exec). Asks the
// desktop portal for a file dialog, a screenshot or a picked color the way an
// app does and waits for the answer (the Request's Response signal, which the
// portal sends only to the app that asked, and only while it is still
// connected).
//   portal-client OpenFile|SaveFile TITLE [current_name] [timeout_s]
//   portal-client Screenshot|PickColor - - [timeout_s]
// Prints "response N" and the chosen URIs (uri, color), or "timeout". (C, not Python:
// the test image has no PyGObject.)
#include <gio/gio.h>
#include <stdlib.h>
#include <string.h>

static void OnResponse(GDBusConnection* bus, const gchar* sender,
                       const gchar* path, const gchar* iface,
                       const gchar* signal, GVariant* params, gpointer loop) {
  guint32 response;
  GVariant* results;
  g_variant_get(params, "(u@a{sv})", &response, &results);
  g_print("response %u\n", response);
  const gchar** uris = NULL;
  if (g_variant_lookup(results, "uris", "^a&s", &uris)) {
    for (int i = 0; uris[i]; i++) {
      g_print("uri %s\n", uris[i]);
    }
    g_free(uris);
  }
  const gchar* uri = NULL;
  if (g_variant_lookup(results, "uri", "&s", &uri)) {
    g_print("uri %s\n", uri);
  }
  gdouble r, g, b;
  if (g_variant_lookup(results, "color", "(ddd)", &r, &g, &b)) {
    g_print("color %.3f %.3f %.3f\n", r, g, b);
  }
  g_variant_unref(results);
  g_main_loop_quit(loop);
}

static gboolean OnTimeout(gpointer loop) {
  g_print("timeout\n");
  g_main_loop_quit(loop);
  return G_SOURCE_REMOVE;
}

int main(int argc, char** argv) {
  if (argc < 3) {
    g_printerr("usage: %s OpenFile|SaveFile TITLE [current_name] [timeout_s]\n"
               "       %s Screenshot|PickColor - - [timeout_s]\n",
               argv[0], argv[0]);
    return 2;
  }
  const char* method = argv[1];
  const char* title = argv[2];
  const char* name = argc > 3 && argv[3][0] ? argv[3] : NULL;
  int timeout = argc > 4 ? atoi(argv[4]) : 120;

  GError* error = NULL;
  GDBusConnection* bus = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &error);
  if (!bus) {
    g_printerr("%s\n", error->message);
    return 1;
  }
  const char* token = "nickeltest";
  gchar* sender = g_strdup(g_dbus_connection_get_unique_name(bus) + 1);
  g_strdelimit(sender, ".", '_');
  gchar* handle = g_strdup_printf("/org/freedesktop/portal/desktop/request/%s/%s",
                                  sender, token);
  GMainLoop* loop = g_main_loop_new(NULL, FALSE);
  g_dbus_connection_signal_subscribe(
      bus, "org.freedesktop.portal.Desktop", "org.freedesktop.portal.Request",
      "Response", handle, NULL, G_DBUS_SIGNAL_FLAGS_NO_MATCH_RULE, OnResponse,
      loop, NULL);

  GVariantBuilder options;
  g_variant_builder_init(&options, G_VARIANT_TYPE_VARDICT);
  g_variant_builder_add(&options, "{sv}", "handle_token",
                        g_variant_new_string(token));
  gboolean shot = strcmp(method, "Screenshot") == 0 ||
                  strcmp(method, "PickColor") == 0;
  if (name && !shot) {
    g_variant_builder_add(&options, "{sv}", "current_name",
                          g_variant_new_string(name));
  }
  if (strcmp(method, "Screenshot") == 0) {
    g_variant_builder_add(&options, "{sv}", "interactive",
                          g_variant_new_boolean(TRUE));
  }
  GVariant* reply = g_dbus_connection_call_sync(
      bus, "org.freedesktop.portal.Desktop", "/org/freedesktop/portal/desktop",
      shot ? "org.freedesktop.portal.Screenshot"
           : "org.freedesktop.portal.FileChooser",
      method,
      shot ? g_variant_new("(sa{sv})", "", &options)
           : g_variant_new("(ssa{sv})", "", title, &options),
      G_VARIANT_TYPE("(o)"), G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error);
  if (!reply) {
    g_printerr("%s\n", error->message);
    return 1;
  }
  g_variant_unref(reply);
  g_print("asked\n");

  g_timeout_add_seconds(timeout, OnTimeout, loop);
  g_main_loop_run(loop);
  return 0;
}
