/* NickelOS: a Linux app with a tray icon, as Discord, Steam or Telegram have
 * one (StatusNotifierItem with a com.canonical.dbusmenu menu), for testing
 * that ChromeOS's shelf stands in for the tray (hw/tray-test.sh): it
 * registers with org.kde.StatusNotifierWatcher and prints what reaches it:
 * "activate" (a click on its shelf icon), "token N" (an xdg-activation
 * token of N bytes), "menu ID" (a click on a menu entry). Its menu: Show
 * Window (1), a separator, Mute (3, checked), Status > Online (5) / Away (6),
 * a disabled heading (7), Quit (8, which ends it). Run inside the session
 * (nickel-session-exec); its desktop file's Id matches its item Id,
 * nickel-sni-test.
 *   sni-test SECONDS [attention]    "attention": Status NeedsAttention
 * Build: vmtest/build-test-client.sh sni-test gio-2.0. */
#include <gio/gio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char kItemXml[] =
    "<node>"
    "  <interface name='org.kde.StatusNotifierItem'>"
    "    <property name='Category' type='s' access='read'/>"
    "    <property name='Id' type='s' access='read'/>"
    "    <property name='Title' type='s' access='read'/>"
    "    <property name='Status' type='s' access='read'/>"
    "    <property name='IconName' type='s' access='read'/>"
    "    <property name='ItemIsMenu' type='b' access='read'/>"
    "    <property name='Menu' type='o' access='read'/>"
    "    <method name='Activate'>"
    "      <arg type='i' direction='in'/><arg type='i' direction='in'/>"
    "    </method>"
    "    <method name='SecondaryActivate'>"
    "      <arg type='i' direction='in'/><arg type='i' direction='in'/>"
    "    </method>"
    "    <method name='ContextMenu'>"
    "      <arg type='i' direction='in'/><arg type='i' direction='in'/>"
    "    </method>"
    "    <method name='Scroll'>"
    "      <arg type='i' direction='in'/><arg type='s' direction='in'/>"
    "    </method>"
    "    <method name='ProvideXdgActivationToken'>"
    "      <arg type='s' direction='in'/>"
    "    </method>"
    "    <signal name='NewStatus'><arg type='s'/></signal>"
    "  </interface>"
    "  <interface name='com.canonical.dbusmenu'>"
    "    <property name='Version' type='u' access='read'/>"
    "    <property name='Status' type='s' access='read'/>"
    "    <method name='GetLayout'>"
    "      <arg type='i' direction='in'/><arg type='i' direction='in'/>"
    "      <arg type='as' direction='in'/>"
    "      <arg type='u' direction='out'/>"
    "      <arg type='(ia{sv}av)' direction='out'/>"
    "    </method>"
    "    <method name='Event'>"
    "      <arg type='i' direction='in'/><arg type='s' direction='in'/>"
    "      <arg type='v' direction='in'/><arg type='u' direction='in'/>"
    "    </method>"
    "    <method name='AboutToShow'>"
    "      <arg type='i' direction='in'/><arg type='b' direction='out'/>"
    "    </method>"
    "    <signal name='LayoutUpdated'>"
    "      <arg type='u'/><arg type='i'/>"
    "    </signal>"
    "  </interface>"
    "</node>";

static GMainLoop* loop;
static const char* status = "Active";

static void say(const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vprintf(fmt, ap);
  va_end(ap);
  putchar('\n');
  fflush(stdout);
}

static GVariant* menu_entry(int id, const char* label, GVariantBuilder* props,
                            GVariant* children) {
  if (!props) {
    GVariantBuilder empty;
    g_variant_builder_init(&empty, G_VARIANT_TYPE("a{sv}"));
    props = &empty;
    g_variant_builder_add(props, "{sv}", "label",
                          g_variant_new_string(label));
    return g_variant_new("(i@a{sv}@av)", id, g_variant_builder_end(props),
                         children ? children : g_variant_new("av", NULL));
  }
  if (label) {
    g_variant_builder_add(props, "{sv}", "label", g_variant_new_string(label));
  }
  return g_variant_new("(i@a{sv}@av)", id, g_variant_builder_end(props),
                       children ? children : g_variant_new("av", NULL));
}

static GVariant* layout(void) {
  GVariantBuilder sep, mute, status_menu, heading, root_children,
      status_children;
  g_variant_builder_init(&sep, G_VARIANT_TYPE("a{sv}"));
  g_variant_builder_add(&sep, "{sv}", "type", g_variant_new_string("separator"));
  g_variant_builder_init(&mute, G_VARIANT_TYPE("a{sv}"));
  g_variant_builder_add(&mute, "{sv}", "toggle-type",
                        g_variant_new_string("checkmark"));
  g_variant_builder_add(&mute, "{sv}", "toggle-state", g_variant_new_int32(1));
  g_variant_builder_init(&status_menu, G_VARIANT_TYPE("a{sv}"));
  g_variant_builder_add(&status_menu, "{sv}", "children-display",
                        g_variant_new_string("submenu"));
  g_variant_builder_init(&heading, G_VARIANT_TYPE("a{sv}"));
  g_variant_builder_add(&heading, "{sv}", "enabled",
                        g_variant_new_boolean(FALSE));

  g_variant_builder_init(&status_children, G_VARIANT_TYPE("av"));
  g_variant_builder_add(&status_children, "v",
                        menu_entry(5, "_Online", NULL, NULL));
  g_variant_builder_add(&status_children, "v",
                        menu_entry(6, "_Away", NULL, NULL));

  g_variant_builder_init(&root_children, G_VARIANT_TYPE("av"));
  g_variant_builder_add(&root_children, "v",
                        menu_entry(1, "_Show Window", NULL, NULL));
  g_variant_builder_add(&root_children, "v", menu_entry(2, NULL, &sep, NULL));
  g_variant_builder_add(&root_children, "v",
                        menu_entry(3, "_Mute", &mute, NULL));
  g_variant_builder_add(
      &root_children, "v",
      menu_entry(4, "S_tatus", &status_menu,
                 g_variant_builder_end(&status_children)));
  g_variant_builder_add(&root_children, "v",
                        menu_entry(7, "Signed in as tester", &heading, NULL));
  g_variant_builder_add(&root_children, "v",
                        menu_entry(8, "_Quit", NULL, NULL));

  GVariantBuilder root;
  g_variant_builder_init(&root, G_VARIANT_TYPE("a{sv}"));
  g_variant_builder_add(&root, "{sv}", "children-display",
                        g_variant_new_string("submenu"));
  return menu_entry(0, NULL, &root, g_variant_builder_end(&root_children));
}

static void on_call(GDBusConnection* c, const gchar* sender, const gchar* path,
                    const gchar* interface, const gchar* method,
                    GVariant* params, GDBusMethodInvocation* invocation,
                    gpointer data) {
  if (!strcmp(method, "Activate")) {
    say("activate");
  } else if (!strcmp(method, "ProvideXdgActivationToken")) {
    const char* token = NULL;
    g_variant_get(params, "(&s)", &token);
    say("token %zu", strlen(token));
  } else if (!strcmp(method, "GetLayout")) {
    g_dbus_method_invocation_return_value(
        invocation, g_variant_new("(u@(ia{sv}av))", 1, layout()));
    return;
  } else if (!strcmp(method, "AboutToShow")) {
    g_dbus_method_invocation_return_value(invocation,
                                          g_variant_new("(b)", FALSE));
    return;
  } else if (!strcmp(method, "Event")) {
    gint32 id = 0;
    const char* event = NULL;
    g_variant_get(params, "(i&svu)", &id, &event, NULL, NULL);
    say("menu %d %s", id, event);
    if (id == 8) {
      g_main_loop_quit(loop);
    }
  }
  g_dbus_method_invocation_return_value(invocation, NULL);
}

static GVariant* on_property(GDBusConnection* c, const gchar* sender,
                             const gchar* path, const gchar* interface,
                             const gchar* property, GError** error,
                             gpointer data) {
  if (!strcmp(interface, "com.canonical.dbusmenu")) {
    if (!strcmp(property, "Version")) {
      return g_variant_new_uint32(3);
    }
    return g_variant_new_string("normal");
  }
  if (!strcmp(property, "Category")) {
    return g_variant_new_string("ApplicationStatus");
  }
  if (!strcmp(property, "Id")) {
    return g_variant_new_string("nickel-sni-test");
  }
  if (!strcmp(property, "Title")) {
    return g_variant_new_string("SNI Test");
  }
  if (!strcmp(property, "Status")) {
    return g_variant_new_string(status);
  }
  if (!strcmp(property, "IconName")) {
    return g_variant_new_string("applications-system");
  }
  if (!strcmp(property, "ItemIsMenu")) {
    return g_variant_new_boolean(FALSE);
  }
  if (!strcmp(property, "Menu")) {
    return g_variant_new_object_path("/MenuBar");
  }
  return NULL;
}

static gboolean on_timeout(gpointer data) {
  g_main_loop_quit(loop);
  return G_SOURCE_REMOVE;
}

int main(int argc, char** argv) {
  double seconds = argc > 1 ? atof(argv[1]) : 30;
  if (argc > 2 && !strcmp(argv[2], "attention")) {
    status = "NeedsAttention";
  }
  GError* error = NULL;
  GDBusConnection* bus = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &error);
  if (!bus) {
    say("no session bus: %s", error->message);
    return 1;
  }
  GDBusNodeInfo* node = g_dbus_node_info_new_for_xml(kItemXml, &error);
  static const GDBusInterfaceVTable vtable = {on_call, on_property, NULL};
  g_dbus_connection_register_object(bus, "/StatusNotifierItem",
                                    node->interfaces[0], &vtable, NULL, NULL,
                                    NULL);
  g_dbus_connection_register_object(bus, "/MenuBar", node->interfaces[1],
                                    &vtable, NULL, NULL, NULL);
  GVariant* reply = g_dbus_connection_call_sync(
      bus, "org.kde.StatusNotifierWatcher", "/StatusNotifierWatcher",
      "org.kde.StatusNotifierWatcher", "RegisterStatusNotifierItem",
      g_variant_new("(s)", "/StatusNotifierItem"), NULL,
      G_DBUS_CALL_FLAGS_NONE, 5000, NULL, &error);
  if (!reply) {
    say("no tray: %s", error->message);
    return 1;
  }
  g_variant_unref(reply);
  say("registered");
  loop = g_main_loop_new(NULL, FALSE);
  g_timeout_add((guint)(seconds * 1000), on_timeout, NULL);
  g_main_loop_run(loop);
  say("done");
  return 0;
}
