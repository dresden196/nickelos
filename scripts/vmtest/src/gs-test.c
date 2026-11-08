/* NickelOS: an app with global shortcuts (the GlobalShortcuts portal), as
 * push-to-talk in Discord or Mumble, for testing NickelOS's backend
 * (nickel-portal's globalshortcuts.cc, Chrome patch 0046). It binds three
 * shortcuts through xdg-desktop-portal, as apps do:
 *   ptt   "Push to talk"   CTRL+ALT+p
 *   mute  "Toggle mute"    CTRL+ALT+m
 *   lock  "Lock"           LOGO+l (ChromeOS's own: must come back unset)
 * and prints "response N" (0: allowed), "bound ID TRIGGER" for each, then
 * "activated ID MS" and "deactivated ID MS" as they come, "changed N" if the
 * bindings change.
 *   gs-test SECONDS [APP_ID]
 * With APP_ID it first registers as that host app (xdg-desktop-portal's
 * host registry; APP_ID.desktop must exist), so the answer is remembered.
 * Build: vmtest/build-test-client.sh gs-test gio-2.0 */
#include <gio/gio.h>
#include <stdio.h>
#include <stdlib.h>

#define PORTAL_NAME "org.freedesktop.portal.Desktop"
#define PORTAL_PATH "/org/freedesktop/portal/desktop"
#define SHORTCUTS "org.freedesktop.portal.GlobalShortcuts"

static gboolean answered;
static guint32 answer;
static GVariant* answer_results;

static void on_response(GDBusConnection* c,
                        const gchar* sender,
                        const gchar* path,
                        const gchar* iface,
                        const gchar* signal,
                        GVariant* params,
                        gpointer data) {
  g_variant_get(params, "(u@a{sv})", &answer, &answer_results);
  answered = TRUE;
}

/* Calls METHOD, which answers through a Request object, and waits for
 * the answer: the user may take a while to decide. */
static GVariant* request(GDBusConnection* c,
                         const char* method,
                         GVariant* params,
                         const char* token,
                         guint32* response) {
  gchar* sender = g_strdup(g_dbus_connection_get_unique_name(c) + 1);
  for (gchar* p = sender; *p; ++p) {
    if (*p == '.') {
      *p = '_';
    }
  }
  gchar* path = g_strdup_printf("%s/request/%s/%s", PORTAL_PATH, sender,
                                token);
  guint id = g_dbus_connection_signal_subscribe(
      c, PORTAL_NAME, "org.freedesktop.portal.Request", "Response", path,
      NULL, G_DBUS_SIGNAL_FLAGS_NONE, on_response, NULL, NULL);
  answered = FALSE;
  GError* error = NULL;
  GVariant* reply = g_dbus_connection_call_sync(
      c, PORTAL_NAME, PORTAL_PATH, SHORTCUTS, method, params,
      G_VARIANT_TYPE("(o)"), G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error);
  if (!reply) {
    printf("%s: %s\n", method, error->message);
    exit(1);
  }
  g_variant_unref(reply);
  while (!answered) {
    g_main_context_iteration(NULL, TRUE);
  }
  g_dbus_connection_signal_unsubscribe(c, id);
  g_free(path);
  g_free(sender);
  *response = answer;
  return answer_results;
}

static void on_shortcut(GDBusConnection* c,
                        const gchar* sender,
                        const gchar* path,
                        const gchar* iface,
                        const gchar* signal,
                        GVariant* params,
                        gpointer data) {
  if (g_str_equal(signal, "ShortcutsChanged")) {
    GVariant* list = g_variant_get_child_value(params, 1);
    printf("changed %zu\n", g_variant_n_children(list));
    g_variant_unref(list);
  } else {
    const gchar* id = NULL;
    guint64 ms = 0;
    g_variant_get(params, "(&o&st@a{sv})", NULL, &id, &ms, NULL);
    printf("%s %s %llu\n",
           g_str_equal(signal, "Activated") ? "activated" : "deactivated", id,
           (unsigned long long)ms);
  }
  fflush(stdout);
}

static void add_shortcut(GVariantBuilder* list,
                         const char* id,
                         const char* description,
                         const char* trigger) {
  GVariantBuilder props;
  g_variant_builder_init(&props, G_VARIANT_TYPE_VARDICT);
  g_variant_builder_add(&props, "{sv}", "description",
                        g_variant_new_string(description));
  g_variant_builder_add(&props, "{sv}", "preferred_trigger",
                        g_variant_new_string(trigger));
  g_variant_builder_add(list, "(s@a{sv})", id, g_variant_builder_end(&props));
}

static gboolean on_timeout(gpointer loop) {
  g_main_loop_quit(loop);
  return G_SOURCE_REMOVE;
}

int main(int argc, char** argv) {
  double seconds = argc > 1 ? atof(argv[1]) : 60;
  const char* app_id = argc > 2 ? argv[2] : NULL;
  GError* error = NULL;
  GDBusConnection* c = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &error);
  if (!c) {
    printf("no session bus: %s\n", error->message);
    return 1;
  }
  if (app_id) {
    GVariant* reply = g_dbus_connection_call_sync(
        c, PORTAL_NAME, PORTAL_PATH, "org.freedesktop.host.portal.Registry",
        "Register", g_variant_new("(sa{sv})", app_id, NULL), NULL,
        G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error);
    if (!reply) {
      printf("register: %s\n", error->message);
      return 1;
    }
    g_variant_unref(reply);
  }

  GVariantBuilder options;
  g_variant_builder_init(&options, G_VARIANT_TYPE_VARDICT);
  g_variant_builder_add(&options, "{sv}", "handle_token",
                        g_variant_new_string("gs1"));
  g_variant_builder_add(&options, "{sv}", "session_handle_token",
                        g_variant_new_string("gs"));
  guint32 response = 0;
  GVariant* results = request(c, "CreateSession",
                              g_variant_new("(a{sv})", &options), "gs1",
                              &response);
  GVariant* handle = g_variant_lookup_value(results, "session_handle", NULL);
  if (response || !handle) {
    printf("session response %u\n", response);
    return 1;
  }
  const gchar* session = g_variant_get_string(handle, NULL);
  g_dbus_connection_signal_subscribe(c, PORTAL_NAME, SHORTCUTS, NULL,
                                     PORTAL_PATH, NULL,
                                     G_DBUS_SIGNAL_FLAGS_NONE, on_shortcut,
                                     NULL, NULL);

  GVariantBuilder list;
  g_variant_builder_init(&list, G_VARIANT_TYPE("a(sa{sv})"));
  add_shortcut(&list, "ptt", "Push to talk", "CTRL+ALT+p");
  add_shortcut(&list, "mute", "Toggle mute", "CTRL+ALT+m");
  add_shortcut(&list, "lock", "Lock", "LOGO+l");
  g_variant_builder_init(&options, G_VARIANT_TYPE_VARDICT);
  g_variant_builder_add(&options, "{sv}", "handle_token",
                        g_variant_new_string("gs2"));
  results = request(
      c, "BindShortcuts",
      g_variant_new("(o@a(sa{sv})s@a{sv})", session,
                    g_variant_builder_end(&list), "",
                    g_variant_builder_end(&options)),
      "gs2", &response);
  printf("response %u\n", response);
  GVariant* bound = g_variant_lookup_value(results, "shortcuts",
                                           G_VARIANT_TYPE("a(sa{sv})"));
  if (bound) {
    GVariantIter iter;
    g_variant_iter_init(&iter, bound);
    const gchar* id = NULL;
    GVariant* props = NULL;
    while (g_variant_iter_next(&iter, "(&s@a{sv})", &id, &props)) {
      const gchar* trigger = "";
      g_variant_lookup(props, "trigger_description", "&s", &trigger);
      printf("bound %s \"%s\"\n", id, trigger);
      g_variant_unref(props);
    }
  }
  fflush(stdout);
  GMainLoop* loop = g_main_loop_new(NULL, FALSE);
  g_timeout_add((guint)(seconds * 1000), on_timeout, loop);
  g_main_loop_run(loop);
  printf("done\n");
  return 0;
}
