// Runs in the test VM as the session user (nickel-session-exec). Calls one
// of the desktop portals the way a host app does, under APP_ID (registered
// with xdg-desktop-portal's host Registry first; "-" for none), and waits
// for the answer (the Request's Response, sent only to the caller):
//   portal-call APP_ID Background REASON AUTOSTART(0|1) COMMAND...
//   portal-call APP_ID Account REASON
//   portal-call APP_ID Email ADDRESS SUBJECT BODY [FILE...]
//   portal-call APP_ID Wallpaper URI PREVIEW(0|1)
//   portal-call APP_ID Launcher NAME ICON.png DESKTOP_FILE_ID EXEC
//   portal-call APP_ID Location
// Prints "asked", then "response N" and the results; for Launcher then
// "installed" once the launcher is installed; for Location then
// "location LATITUDE LONGITUDE ACCURACY" once the first one comes, or
// "timeout". (C: the test image has no PyGObject.)
#include <fcntl.h>
#include <gio/gio.h>
#include <gio/gunixfdlist.h>
#include <stdlib.h>
#include <string.h>

static const char kDest[] = "org.freedesktop.portal.Desktop";
static const char kPath[] = "/org/freedesktop/portal/desktop";
static const char kToken[] = "nickeltest";

static GMainLoop* loop;
static GVariant* response_results;
static guint32 response_code = 99;

static void OnResponse(GDBusConnection* bus, const gchar* sender,
                       const gchar* path, const gchar* iface,
                       const gchar* signal, GVariant* params, gpointer data) {
  g_variant_get(params, "(u@a{sv})", &response_code, &response_results);
  gchar* text = g_variant_print(response_results, TRUE);
  g_print("response %u %s\n", response_code, text);
  g_free(text);
  g_main_loop_quit(loop);
}

static void OnLocation(GDBusConnection* bus, const gchar* sender,
                       const gchar* path, const gchar* iface,
                       const gchar* signal, GVariant* params, gpointer data) {
  g_autoptr(GVariant) location = NULL;
  g_variant_get(params, "(&o@a{sv})", NULL, &location);
  gdouble latitude = 0, longitude = 0, accuracy = 0;
  g_variant_lookup(location, "Latitude", "d", &latitude);
  g_variant_lookup(location, "Longitude", "d", &longitude);
  g_variant_lookup(location, "Accuracy", "d", &accuracy);
  g_print("location %.4f %.4f %.0f\n", latitude, longitude, accuracy);
  *(gboolean*)data = TRUE;
  g_main_loop_quit(loop);
}

static gboolean OnTimeout(gpointer data) {
  g_print("timeout\n");
  g_main_loop_quit(loop);
  return G_SOURCE_REMOVE;
}

static GVariant* Call(GDBusConnection* bus, const char* iface,
                      const char* method, GVariant* args,
                      GUnixFDList* fds) {
  GError* error = NULL;
  GVariant* reply = g_dbus_connection_call_with_unix_fd_list_sync(
      bus, kDest, kPath, iface, method, args, NULL, G_DBUS_CALL_FLAGS_NONE,
      -1, fds, NULL, NULL, &error);
  if (!reply) {
    g_printerr("%s.%s: %s\n", iface, method, error->message);
    exit(1);
  }
  return reply;
}

// Asks, waits for the Response; FALSE on a timeout.
static gboolean Ask(GDBusConnection* bus, const char* iface,
                    const char* method, GVariant* args, GUnixFDList* fds,
                    int timeout) {
  g_variant_unref(Call(bus, iface, method, args, fds));
  g_print("asked\n");
  guint timer = g_timeout_add_seconds(timeout, OnTimeout, NULL);
  g_main_loop_run(loop);
  if (response_code == 99) {
    return FALSE;
  }
  g_source_remove(timer);
  return TRUE;
}

static GVariantBuilder* Options(void) {
  GVariantBuilder* options = g_variant_builder_new(G_VARIANT_TYPE_VARDICT);
  g_variant_builder_add(options, "{sv}", "handle_token",
                        g_variant_new_string(kToken));
  return options;
}

int main(int argc, char** argv) {
  if (argc < 3) {
    g_printerr("usage: see the source\n");
    return 2;
  }
  const char* app_id = argv[1];
  const char* what = argv[2];
  const int timeout = getenv("PORTAL_TIMEOUT") ? atoi(getenv("PORTAL_TIMEOUT"))
                                               : 60;
  GError* error = NULL;
  GDBusConnection* bus = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &error);
  if (!bus) {
    g_printerr("%s\n", error->message);
    return 1;
  }
  loop = g_main_loop_new(NULL, FALSE);
  if (strcmp(app_id, "-") != 0) {
    g_variant_unref(Call(bus, "org.freedesktop.host.portal.Registry",
                         "Register", g_variant_new("(sa{sv})", app_id, NULL),
                         NULL));
  }
  gchar* sender = g_strdup(g_dbus_connection_get_unique_name(bus) + 1);
  g_strdelimit(sender, ".", '_');
  gchar* handle = g_strdup_printf(
      "/org/freedesktop/portal/desktop/request/%s/%s", sender, kToken);
  g_dbus_connection_signal_subscribe(
      bus, kDest, "org.freedesktop.portal.Request", "Response", handle, NULL,
      G_DBUS_SIGNAL_FLAGS_NO_MATCH_RULE, OnResponse, NULL, NULL);

  GVariantBuilder* options = Options();
  gboolean answered = FALSE;
  if (strcmp(what, "Background") == 0 && argc >= 6) {
    g_variant_builder_add(options, "{sv}", "reason",
                          g_variant_new_string(argv[3]));
    g_variant_builder_add(options, "{sv}", "autostart",
                          g_variant_new_boolean(atoi(argv[4])));
    g_variant_builder_add(
        options, "{sv}", "commandline",
        g_variant_new_strv((const gchar* const*)&argv[5], argc - 5));
    answered = Ask(bus, "org.freedesktop.portal.Background",
                   "RequestBackground", g_variant_new("(sa{sv})", "", options),
                   NULL, timeout);
  } else if (strcmp(what, "Account") == 0 && argc >= 4) {
    g_variant_builder_add(options, "{sv}", "reason",
                          g_variant_new_string(argv[3]));
    answered = Ask(bus, "org.freedesktop.portal.Account", "GetUserInformation",
                   g_variant_new("(sa{sv})", "", options), NULL, timeout);
  } else if (strcmp(what, "Email") == 0 && argc >= 6) {
    g_variant_builder_add(options, "{sv}", "address",
                          g_variant_new_string(argv[3]));
    g_variant_builder_add(options, "{sv}", "subject",
                          g_variant_new_string(argv[4]));
    g_variant_builder_add(options, "{sv}", "body",
                          g_variant_new_string(argv[5]));
    GUnixFDList* fds = g_unix_fd_list_new();
    GVariantBuilder handles;
    g_variant_builder_init(&handles, G_VARIANT_TYPE("ah"));
    for (int i = 6; i < argc; i++) {
      int fd = open(argv[i], O_RDONLY | O_CLOEXEC);
      if (fd < 0) {
        g_printerr("cannot open %s\n", argv[i]);
        return 1;
      }
      g_variant_builder_add(&handles, "h",
                            g_unix_fd_list_append(fds, fd, NULL));
    }
    if (argc > 6) {
      g_variant_builder_add(options, "{sv}", "attachment_fds",
                            g_variant_builder_end(&handles));
    } else {
      g_variant_builder_clear(&handles);
    }
    answered = Ask(bus, "org.freedesktop.portal.Email", "ComposeEmail",
                   g_variant_new("(sa{sv})", "", options), fds, timeout);
  } else if (strcmp(what, "Wallpaper") == 0 && argc >= 5) {
    g_variant_builder_add(options, "{sv}", "show-preview",
                          g_variant_new_boolean(atoi(argv[4])));
    g_variant_builder_add(options, "{sv}", "set-on",
                          g_variant_new_string("both"));
    answered = Ask(bus, "org.freedesktop.portal.Wallpaper", "SetWallpaperURI",
                   g_variant_new("(ssa{sv})", "", argv[3], options), NULL,
                   timeout);
  } else if (strcmp(what, "Launcher") == 0 && argc >= 7) {
    gchar* png = NULL;
    gsize length = 0;
    if (!g_file_get_contents(argv[4], &png, &length, &error)) {
      g_printerr("%s\n", error->message);
      return 1;
    }
    GBytes* bytes = g_bytes_new_take(png, length);
    GIcon* icon = g_bytes_icon_new(bytes);
    GVariant* icon_v = g_icon_serialize(icon);
    g_variant_builder_add(options, "{sv}", "launcher_type",
                          g_variant_new_uint32(1));
    answered = Ask(bus, "org.freedesktop.portal.DynamicLauncher",
                   "PrepareInstall",
                   g_variant_new("(ssva{sv})", "", argv[3], icon_v, options),
                   NULL, timeout);
    const gchar* token = NULL;
    if (answered && response_code == 0 &&
        g_variant_lookup(response_results, "token", "&s", &token)) {
      gchar* entry = g_strdup_printf(
          "[Desktop Entry]\nType=Application\nName=%s\nExec=%s\n", argv[3],
          argv[6]);
      g_variant_unref(Call(
          bus, "org.freedesktop.portal.DynamicLauncher", "Install",
          g_variant_new("(sssa{sv})", token, argv[5], entry, NULL), NULL));
      g_print("installed\n");
    }
  } else if (strcmp(what, "Location") == 0) {
    GVariantBuilder session_options;
    g_variant_builder_init(&session_options, G_VARIANT_TYPE_VARDICT);
    g_variant_builder_add(&session_options, "{sv}", "session_handle_token",
                          g_variant_new_string(kToken));
    g_variant_builder_add(&session_options, "{sv}", "accuracy",
                          g_variant_new_uint32(5));  // Exact.
    GVariant* reply = Call(bus, "org.freedesktop.portal.Location",
                           "CreateSession",
                           g_variant_new("(a{sv})", &session_options), NULL);
    const gchar* session = NULL;
    g_variant_get(reply, "(&o)", &session);
    gboolean located = FALSE;
    g_dbus_connection_signal_subscribe(
        bus, kDest, "org.freedesktop.portal.Location", "LocationUpdated",
        kPath, NULL, G_DBUS_SIGNAL_FLAGS_NONE, OnLocation, &located, NULL);
    answered = Ask(bus, "org.freedesktop.portal.Location", "Start",
                   g_variant_new("(osa{sv})", session, "", options), NULL,
                   timeout);
    if (answered && response_code == 0) {
      guint timer = g_timeout_add_seconds(timeout, OnTimeout, NULL);
      g_main_loop_run(loop);
      if (!located) {
        return 1;
      }
      g_source_remove(timer);
    }
  } else {
    g_printerr("unknown or short: %s\n", what);
    return 2;
  }
  return answered ? 0 : 1;
}
