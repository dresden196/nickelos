// Runs in the test VM as the session user (nickel-session-exec). A remote
// desktop session with the clipboard, as a remote desktop app (RustDesk,
// AnyDesk) has one through xdg-desktop-portal: CreateSession,
// RequestClipboard, SelectDevices (keyboard), Start (ChromeOS asks; the test
// answers), then
//   1. the other computer's selection: SetSelection(text), and on
//      SelectionTransfer, SelectionWrite + TEXT + SelectionWriteDone: ChromeOS's
//      clipboard holds TEXT
//   2. on SelectionOwnerChanged (the backend's watch sees the change),
//      SelectionRead(text): what ChromeOS's clipboard holds, until it is TEXT
//      (the first change can be what the clipboard held already)
//   portal-rd-clipboard APP_ID TEXT
// Prints "started clipboard_enabled=…", "transfer …", "owner changed …" and
// "read: …" for each change; "timeout" if TEXT never comes.
#include <gio/gio.h>
#include <gio/gunixfdlist.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char kDest[] = "org.freedesktop.portal.Desktop";
static const char kPath[] = "/org/freedesktop/portal/desktop";
static const char kRemote[] = "org.freedesktop.portal.RemoteDesktop";
static const char kClipboard[] = "org.freedesktop.portal.Clipboard";
static const char kText[] = "text/plain;charset=utf-8";

static GDBusConnection* bus;
static GMainLoop* loop;
static GVariant* results;
static guint32 response = 99;
static gchar* session;
static const char* text;
static int step;

static gboolean OnTimeout(gpointer data) {
  g_print("timeout\n");
  exit(1);
}

static void OnResponse(GDBusConnection* c, const gchar* s, const gchar* p,
                       const gchar* i, const gchar* n, GVariant* params,
                       gpointer data) {
  g_variant_get(params, "(u@a{sv})", &response, &results);
  g_main_loop_quit(loop);
}

static GVariant* Call(const char* iface, const char* method, GVariant* args,
                      GUnixFDList** out_fds) {
  GError* error = NULL;
  GVariant* reply = g_dbus_connection_call_with_unix_fd_list_sync(
      bus, kDest, kPath, iface, method, args, NULL, G_DBUS_CALL_FLAGS_NONE, -1,
      NULL, out_fds, NULL, &error);
  if (!reply) {
    g_print("%s.%s: %s\n", iface, method, error->message);
    exit(1);
  }
  return reply;
}

// A Request call: waits for its Response.
static void Request(const char* iface, const char* method, GVariant* args,
                    const char* token) {
  gchar* sender = g_strdup(g_dbus_connection_get_unique_name(bus) + 1);
  g_strdelimit(sender, ".", '_');
  gchar* handle = g_strdup_printf(
      "/org/freedesktop/portal/desktop/request/%s/%s", sender, token);
  guint id = g_dbus_connection_signal_subscribe(
      bus, kDest, "org.freedesktop.portal.Request", "Response", handle, NULL,
      G_DBUS_SIGNAL_FLAGS_NO_MATCH_RULE, OnResponse, NULL, NULL);
  response = 99;
  g_variant_unref(Call(iface, method, args, NULL));
  g_main_loop_run(loop);
  g_dbus_connection_signal_unsubscribe(bus, id);
  if (response != 0) {
    g_print("%s: response %u\n", method, response);
    exit(1);
  }
}

static void OnClipboardSignal(GDBusConnection* c, const gchar* s,
                              const gchar* p, const gchar* i,
                              const gchar* signal, GVariant* params,
                              gpointer data) {
  if (strcmp(signal, "SelectionTransfer") == 0 && step == 1) {
    const gchar* mime = NULL;
    guint32 serial = 0;
    g_variant_get(params, "(&o&su)", NULL, &mime, &serial);
    g_print("transfer %s %u\n", mime, serial);
    GUnixFDList* fds = NULL;
    GVariant* reply = Call(kClipboard, "SelectionWrite",
                           g_variant_new("(ou)", session, serial), &fds);
    gint32 index = 0;
    g_variant_get(reply, "(h)", &index);
    // The list's own copy too: the backend reads to the end of the pipe.
    int fd = g_unix_fd_list_get(fds, index, NULL);
    g_object_unref(fds);
    g_variant_unref(reply);
    gboolean ok = write(fd, text, strlen(text)) == (ssize_t)strlen(text);
    close(fd);
    g_variant_unref(Call(kClipboard, "SelectionWriteDone",
                         g_variant_new("(oub)", session, serial, ok), NULL));
    step = 2;
  } else if (strcmp(signal, "SelectionOwnerChanged") == 0 && step == 2) {
    GVariant* options = NULL;
    g_variant_get(params, "(&o@a{sv})", NULL, &options);
    gchar* printed = g_variant_print(options, FALSE);
    g_print("owner changed %s\n", printed);
    g_free(printed);
    GUnixFDList* fds = NULL;
    GVariant* reply = Call(kClipboard, "SelectionRead",
                           g_variant_new("(os)", session, kText), &fds);
    gint32 index = 0;
    g_variant_get(reply, "(h)", &index);
    int fd = g_unix_fd_list_get(fds, index, NULL);
    g_object_unref(fds);
    g_variant_unref(reply);
    g_variant_unref(options);
    GString* got = g_string_new(NULL);
    char buffer[4096];
    ssize_t n;
    while ((n = read(fd, buffer, sizeof(buffer))) > 0) {
      g_string_append_len(got, buffer, n);
    }
    close(fd);
    g_print("read: %s\n", got->str);
    if (strcmp(got->str, text) == 0) {
      g_main_loop_quit(loop);
    }
    g_string_free(got, TRUE);
  }
}

int main(int argc, char** argv) {
  if (argc < 3) {
    g_printerr("usage: %s APP_ID TEXT\n", argv[0]);
    return 2;
  }
  text = argv[2];
  GError* error = NULL;
  bus = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &error);
  loop = g_main_loop_new(NULL, FALSE);
  g_variant_unref(Call("org.freedesktop.host.portal.Registry", "Register",
                       g_variant_new("(sa{sv})", argv[1], NULL), NULL));
  g_timeout_add_seconds(90, OnTimeout, NULL);

  GVariantBuilder options;
  g_variant_builder_init(&options, G_VARIANT_TYPE_VARDICT);
  g_variant_builder_add(&options, "{sv}", "handle_token",
                        g_variant_new_string("t1"));
  g_variant_builder_add(&options, "{sv}", "session_handle_token",
                        g_variant_new_string("s1"));
  Request(kRemote, "CreateSession", g_variant_new("(a{sv})", &options), "t1");
  g_variant_lookup(results, "session_handle", "s", &session);

  g_dbus_connection_signal_subscribe(bus, kDest, kClipboard, NULL, kPath, NULL,
                                     G_DBUS_SIGNAL_FLAGS_NONE,
                                     OnClipboardSignal, NULL, NULL);
  g_variant_unref(Call(kClipboard, "RequestClipboard",
                       g_variant_new("(oa{sv})", session, NULL), NULL));

  g_variant_builder_init(&options, G_VARIANT_TYPE_VARDICT);
  g_variant_builder_add(&options, "{sv}", "handle_token",
                        g_variant_new_string("t2"));
  g_variant_builder_add(&options, "{sv}", "types", g_variant_new_uint32(1));
  Request(kRemote, "SelectDevices",
          g_variant_new("(oa{sv})", session, &options), "t2");

  g_variant_builder_init(&options, G_VARIANT_TYPE_VARDICT);
  g_variant_builder_add(&options, "{sv}", "handle_token",
                        g_variant_new_string("t3"));
  Request(kRemote, "Start", g_variant_new("(osa{sv})", session, "", &options),
          "t3");
  gboolean enabled = FALSE;
  g_variant_lookup(results, "clipboard_enabled", "b", &enabled);
  g_print("started clipboard_enabled=%s\n", enabled ? "true" : "false");
  if (!enabled) {
    return 1;
  }

  step = 1;
  const char* types[] = {kText, "text/plain", NULL};
  g_variant_builder_init(&options, G_VARIANT_TYPE_VARDICT);
  g_variant_builder_add(&options, "{sv}", "mime_types",
                        g_variant_new_strv(types, -1));
  g_variant_unref(Call(kClipboard, "SetSelection",
                       g_variant_new("(oa{sv})", session, &options), NULL));
  g_main_loop_run(loop);
  return 0;
}
