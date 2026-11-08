/* NickelOS: controls the device the way a remote desktop app (AnyDesk,
 * RustDesk) does: the RemoteDesktop portal (CreateSession, SelectDevices,
 * ScreenCast's SelectSources on the same session, Start), then pointer and
 * keyboard events. Start waits for the user: ChromeOS's "share your screen"
 * picker, then "Allow remote control?". Then it clicks at the X Y given
 * (default: the launcher button, 24 776), types the text given (default
 * "files") as keysyms and presses Return. Build: see
 * vmtest/remote-desktop-test.sh. */
#include <gio/gio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define PORTAL "org.freedesktop.portal.Desktop"
#define PORTAL_PATH "/org/freedesktop/portal/desktop"
#define REMOTE "org.freedesktop.portal.RemoteDesktop"
#define SCREENCAST "org.freedesktop.portal.ScreenCast"
#define BTN_LEFT 0x110
#define XK_Return 0xff0d

static GDBusConnection *bus;
static GMainLoop *loop;
static guint32 response_code;
static GVariant *response_results;

static void on_response(GDBusConnection *c, const gchar *s, const gchar *p,
                        const gchar *i, const gchar *sig, GVariant *params,
                        gpointer data) {
  g_variant_get(params, "(u@a{sv})", &response_code, &response_results);
  g_main_loop_quit(loop);
}

static gboolean call(const char *iface, const char *method, GVariant *args,
                     const char *token) {
  char *sender = g_strdup(g_dbus_connection_get_unique_name(bus) + 1);
  for (char *c = sender; *c; ++c)
    if (*c == '.') *c = '_';
  char *handle = g_strdup_printf(PORTAL_PATH "/request/%s/%s", sender, token);
  guint sub = g_dbus_connection_signal_subscribe(
      bus, PORTAL, "org.freedesktop.portal.Request", "Response", handle, NULL,
      G_DBUS_SIGNAL_FLAGS_NO_MATCH_RULE, on_response, NULL, NULL);
  GError *error = NULL;
  GVariant *reply = g_dbus_connection_call_sync(
      bus, PORTAL, PORTAL_PATH, iface, method, args, NULL,
      G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error);
  if (!reply) {
    printf("%s: %s\n", method, error->message);
    return FALSE;
  }
  g_variant_unref(reply);
  g_main_loop_run(loop);
  g_dbus_connection_signal_unsubscribe(bus, sub);
  printf("%s: response %u\n", method, response_code);
  fflush(stdout);
  g_free(handle);
  g_free(sender);
  return response_code == 0;
}

static void notify(const char *method, GVariant *args) {
  GError *error = NULL;
  GVariant *reply = g_dbus_connection_call_sync(
      bus, PORTAL, PORTAL_PATH, REMOTE, method, args, NULL,
      G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error);
  if (!reply) {
    printf("%s: %s\n", method, error->message);
    return;
  }
  g_variant_unref(reply);
}

static GVariant *opts(const char *token) {
  GVariantBuilder o;
  g_variant_builder_init(&o, G_VARIANT_TYPE_VARDICT);
  if (token)
    g_variant_builder_add(&o, "{sv}", "handle_token",
                          g_variant_new_string(token));
  return g_variant_builder_end(&o);
}

int main(int argc, char **argv) {
  double x = argc > 2 ? atof(argv[1]) : 24, y = argc > 2 ? atof(argv[2]) : 776;
  const char *text = argc > 3 ? argv[3] : "files";
  loop = g_main_loop_new(NULL, FALSE);
  bus = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, NULL);

  GVariantBuilder o;
  g_variant_builder_init(&o, G_VARIANT_TYPE_VARDICT);
  g_variant_builder_add(&o, "{sv}", "handle_token", g_variant_new_string("r1"));
  g_variant_builder_add(&o, "{sv}", "session_handle_token",
                        g_variant_new_string("rs1"));
  if (!call(REMOTE, "CreateSession", g_variant_new("(a{sv})", &o), "r1"))
    return 1;
  const char *session = NULL;
  g_variant_lookup(response_results, "session_handle", "&s", &session);
  session = g_strdup(session);

  g_variant_builder_init(&o, G_VARIANT_TYPE_VARDICT);
  g_variant_builder_add(&o, "{sv}", "handle_token", g_variant_new_string("r2"));
  g_variant_builder_add(&o, "{sv}", "types", g_variant_new_uint32(3));
  if (!call(REMOTE, "SelectDevices", g_variant_new("(oa{sv})", session, &o),
            "r2"))
    return 1;

  g_variant_builder_init(&o, G_VARIANT_TYPE_VARDICT);
  g_variant_builder_add(&o, "{sv}", "handle_token", g_variant_new_string("r3"));
  g_variant_builder_add(&o, "{sv}", "types", g_variant_new_uint32(1));
  if (!call(SCREENCAST, "SelectSources", g_variant_new("(oa{sv})", session, &o),
            "r3"))
    return 1;

  if (!call(REMOTE, "Start",
            g_variant_new("(os@a{sv})", session, "", opts("r4")), "r4"))
    return 1;
  guint32 devices = 0, node = 0;
  g_variant_lookup(response_results, "devices", "u", &devices);
  GVariant *streams = g_variant_lookup_value(response_results, "streams",
                                             G_VARIANT_TYPE("a(ua{sv})"));
  if (streams && g_variant_n_children(streams))
    g_variant_get_child(streams, 0, "(u@a{sv})", &node, NULL);
  printf("devices %u, stream node %u\n", devices, node);
  fflush(stdout);

  /* Click, then type and press Return. */
  notify("NotifyPointerMotionAbsolute",
         g_variant_new("(o@a{sv}udd)", session, opts(NULL), node, x, y));
  usleep(300000);
  notify("NotifyPointerButton",
         g_variant_new("(o@a{sv}iu)", session, opts(NULL), BTN_LEFT, 1));
  notify("NotifyPointerButton",
         g_variant_new("(o@a{sv}iu)", session, opts(NULL), BTN_LEFT, 0));
  sleep(3);
  for (const char *c = text; *c; ++c) {
    notify("NotifyKeyboardKeysym",
           g_variant_new("(o@a{sv}iu)", session, opts(NULL), *c, 1));
    notify("NotifyKeyboardKeysym",
           g_variant_new("(o@a{sv}iu)", session, opts(NULL), *c, 0));
    usleep(100000);
  }
  sleep(2);
  notify("NotifyKeyboardKeysym",
         g_variant_new("(o@a{sv}iu)", session, opts(NULL), XK_Return, 1));
  notify("NotifyKeyboardKeysym",
         g_variant_new("(o@a{sv}iu)", session, opts(NULL), XK_Return, 0));
  printf("sent: click at %.0f,%.0f, typed \"%s\", Return\n", x, y, text);
  sleep(5);
  return 0;
}
