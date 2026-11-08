// Distributed under the terms of the GNU General Public License v2
//
// nickel-xembed-tray: old-style X11 tray icons (XEmbed, the freedesktop
// System Tray Protocol), as Wine programs and Java apps have them, made
// StatusNotifierItems for nickel-appd's tray, so that they reach ChromeOS's
// shelf like the others (nickel-appd tray.cc, Chrome patch 0045).
//
// It owns _NET_SYSTEM_TRAY_S<screen> on the session's X display. An icon
// that docks is reparented into the tray's own window, which is never
// mapped: sommelier makes every mapped X window a ChromeOS window, and the
// shelf shows the app's own icon, so the icon's picture is never needed.
// Each icon becomes an item on a D-Bus connection of its own (the
// specification removes an item when its connection goes): Id and Title
// from the icon window's WM_CLASS and name, and X-NickelOS-Pid, the app's
// process (_NET_WM_PID), by which nickel-appd finds the app. Activate (a
// click on the app's shelf icon) is a left click on the icon; the item's
// menu has one entry, the app's own tray menu, which a right click opens.
// Clicks are sent to the icon window as X button events.
//
// Run by the nickel-xembed-tray job as the session user, with DISPLAY set; it
// exits when the X server goes (sommelier exits when Chrome restarts) and the
// job starts it again.

#include <gio/gio.h>
#include <glib-unix.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>
#include <xcb/xcb.h>

#define SYSTEM_TRAY_REQUEST_DOCK 0
#define XEMBED_EMBEDDED_NOTIFY 0

static const char kItemXml[] =
    "<node>"
    "  <interface name='org.kde.StatusNotifierItem'>"
    "    <property name='Category' type='s' access='read'/>"
    "    <property name='Id' type='s' access='read'/>"
    "    <property name='Title' type='s' access='read'/>"
    "    <property name='Status' type='s' access='read'/>"
    "    <property name='ItemIsMenu' type='b' access='read'/>"
    "    <property name='Menu' type='o' access='read'/>"
    "    <property name='X-NickelOS-Pid' type='u' access='read'/>"
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
    "  </interface>"
    "</node>";

// The one menu entry.
#define MENU_TRAY_MENU 1

struct Icon {
  xcb_window_t window;
  guint32 pid;
  char* id;
  char* title;
  GDBusConnection* bus;
  guint registrations[2];
  struct Icon* next;
};

static xcb_connection_t* conn;
static xcb_screen_t* screen;
static xcb_window_t tray_window;
static xcb_atom_t opcode_atom, xembed_atom, pid_atom, net_name_atom,
    utf8_atom, xembed_info_atom;
static GDBusNodeInfo* node;
static struct Icon* icons;
static GMainLoop* loop;

static void log_msg(const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vsyslog(LOG_INFO, fmt, ap);
  va_end(ap);
}

static xcb_atom_t intern(const char* name) {
  xcb_intern_atom_reply_t* reply = xcb_intern_atom_reply(
      conn, xcb_intern_atom(conn, 0, strlen(name), name), NULL);
  xcb_atom_t atom = reply ? reply->atom : XCB_ATOM_NONE;
  free(reply);
  return atom;
}

// A property's value as a string (NULL if unset).
static char* get_string(xcb_window_t window, xcb_atom_t property,
                        xcb_atom_t type) {
  xcb_get_property_reply_t* reply = xcb_get_property_reply(
      conn, xcb_get_property(conn, 0, window, property, type, 0, 256), NULL);
  char* value = NULL;
  if (reply && xcb_get_property_value_length(reply) > 0) {
    value = g_strndup(xcb_get_property_value(reply),
                      xcb_get_property_value_length(reply));
  }
  free(reply);
  return value;
}

static guint32 get_cardinal(xcb_window_t window, xcb_atom_t property) {
  xcb_get_property_reply_t* reply = xcb_get_property_reply(
      conn,
      xcb_get_property(conn, 0, window, property, XCB_ATOM_CARDINAL, 0, 1),
      NULL);
  guint32 value = 0;
  if (reply && xcb_get_property_value_length(reply) >= 4) {
    value = *(guint32*)xcb_get_property_value(reply);
  }
  free(reply);
  return value;
}

// A click on the icon, as the app gets it from a tray.
static void click(xcb_window_t window, int button) {
  xcb_button_press_event_t event;
  memset(&event, 0, sizeof(event));
  event.response_type = XCB_BUTTON_PRESS;
  event.detail = button;
  event.time = XCB_CURRENT_TIME;
  event.root = screen->root;
  event.event = window;
  event.child = XCB_NONE;
  event.event_x = 8;
  event.event_y = 8;
  event.same_screen = 1;
  xcb_send_event(conn, 0, window, XCB_EVENT_MASK_BUTTON_PRESS,
                 (const char*)&event);
  event.response_type = XCB_BUTTON_RELEASE;
  event.state = button == 1   ? XCB_BUTTON_MASK_1
                : button == 2 ? XCB_BUTTON_MASK_2
                              : XCB_BUTTON_MASK_3;
  xcb_send_event(conn, 0, window, XCB_EVENT_MASK_BUTTON_RELEASE,
                 (const char*)&event);
  xcb_flush(conn);
}

// --- The item -------------------------------------------------------------

static void on_call(GDBusConnection* bus, const gchar* sender,
                    const gchar* path, const gchar* interface,
                    const gchar* method, GVariant* params,
                    GDBusMethodInvocation* invocation, gpointer data) {
  struct Icon* icon = data;
  if (!strcmp(method, "Activate")) {
    click(icon->window, 1);
  } else if (!strcmp(method, "SecondaryActivate")) {
    click(icon->window, 2);
  } else if (!strcmp(method, "ContextMenu")) {
    click(icon->window, 3);
  } else if (!strcmp(method, "GetLayout")) {
    GVariantBuilder entry, root, children;
    g_variant_builder_init(&entry, G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(&entry, "{sv}", "label",
                          g_variant_new_string("Tray menu"));
    g_variant_builder_init(&children, G_VARIANT_TYPE("av"));
    g_variant_builder_add(
        &children, "v",
        g_variant_new("(i@a{sv}@av)", MENU_TRAY_MENU,
                      g_variant_builder_end(&entry),
                      g_variant_new("av", NULL)));
    g_variant_builder_init(&root, G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(&root, "{sv}", "children-display",
                          g_variant_new_string("submenu"));
    g_dbus_method_invocation_return_value(
        invocation,
        g_variant_new("(u(i@a{sv}@av))", 1, 0, g_variant_builder_end(&root),
                      g_variant_builder_end(&children)));
    return;
  } else if (!strcmp(method, "AboutToShow")) {
    g_dbus_method_invocation_return_value(invocation,
                                          g_variant_new("(b)", FALSE));
    return;
  } else if (!strcmp(method, "Event")) {
    gint32 id = 0;
    const char* event = NULL;
    g_variant_get(params, "(i&svu)", &id, &event, NULL, NULL);
    if (id == MENU_TRAY_MENU && !strcmp(event, "clicked")) {
      click(icon->window, 3);
    }
  }
  g_dbus_method_invocation_return_value(invocation, NULL);
}

static GVariant* on_property(GDBusConnection* bus, const gchar* sender,
                             const gchar* path, const gchar* interface,
                             const gchar* property, GError** error,
                             gpointer data) {
  struct Icon* icon = data;
  if (!strcmp(interface, "com.canonical.dbusmenu")) {
    return !strcmp(property, "Version") ? g_variant_new_uint32(3)
                                        : g_variant_new_string("normal");
  }
  if (!strcmp(property, "Category")) {
    return g_variant_new_string("ApplicationStatus");
  }
  if (!strcmp(property, "Id")) {
    return g_variant_new_string(icon->id ? icon->id : "");
  }
  if (!strcmp(property, "Title")) {
    return g_variant_new_string(icon->title ? icon->title : "");
  }
  if (!strcmp(property, "Status")) {
    return g_variant_new_string("Active");
  }
  if (!strcmp(property, "ItemIsMenu")) {
    return g_variant_new_boolean(FALSE);
  }
  if (!strcmp(property, "Menu")) {
    return g_variant_new_object_path("/MenuBar");
  }
  if (!strcmp(property, "X-NickelOS-Pid")) {
    return g_variant_new_uint32(icon->pid);
  }
  return NULL;
}

static void export_icon(struct Icon* icon) {
  GError* error = NULL;
  gchar* address =
      g_dbus_address_get_for_bus_sync(G_BUS_TYPE_SESSION, NULL, &error);
  if (address) {
    icon->bus = g_dbus_connection_new_for_address_sync(
        address,
        G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT |
            G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION,
        NULL, NULL, &error);
    g_free(address);
  }
  if (!icon->bus) {
    log_msg("no session bus for an icon: %s", error->message);
    g_error_free(error);
    return;
  }
  static const GDBusInterfaceVTable vtable = {on_call, on_property, NULL};
  icon->registrations[0] = g_dbus_connection_register_object(
      icon->bus, "/StatusNotifierItem", node->interfaces[0], &vtable, icon,
      NULL, NULL);
  icon->registrations[1] = g_dbus_connection_register_object(
      icon->bus, "/MenuBar", node->interfaces[1], &vtable, icon, NULL, NULL);
  g_dbus_connection_call(icon->bus, "org.kde.StatusNotifierWatcher",
                         "/StatusNotifierWatcher",
                         "org.kde.StatusNotifierWatcher",
                         "RegisterStatusNotifierItem",
                         g_variant_new("(s)", "/StatusNotifierItem"), NULL,
                         G_DBUS_CALL_FLAGS_NONE, -1, NULL, NULL, NULL);
}

// --- Docking --------------------------------------------------------------------

static void dock(xcb_window_t window) {
  for (struct Icon* icon = icons; icon; icon = icon->next) {
    if (icon->window == window) {
      return;
    }
  }
  struct Icon* icon = g_new0(struct Icon, 1);
  icon->window = window;
  icon->pid = get_cardinal(window, pid_atom);
  // WM_CLASS is "instance\0class\0": the class names the program.
  char* wm_class = get_string(window, XCB_ATOM_WM_CLASS, XCB_ATOM_STRING);
  if (wm_class) {
    xcb_get_property_reply_t* reply = xcb_get_property_reply(
        conn,
        xcb_get_property(conn, 0, window, XCB_ATOM_WM_CLASS, XCB_ATOM_STRING,
                         0, 256),
        NULL);
    int length = reply ? xcb_get_property_value_length(reply) : 0;
    const char* value = reply ? xcb_get_property_value(reply) : "";
    size_t first = strnlen(value, length);
    icon->id = first + 1 < (size_t)length
                   ? g_strndup(value + first + 1, length - first - 1)
                   : g_strdup(wm_class);
    free(reply);
    g_free(wm_class);
  }
  icon->title = get_string(window, net_name_atom, utf8_atom);
  if (!icon->title) {
    icon->title = get_string(window, XCB_ATOM_WM_NAME, XCB_ATOM_STRING);
  }
  icon->next = icons;
  icons = icon;

  uint32_t mask = XCB_EVENT_MASK_STRUCTURE_NOTIFY;
  xcb_change_window_attributes(conn, window, XCB_CW_EVENT_MASK, &mask);
  // Into the tray's window, which is never mapped: the icon is never shown.
  xcb_reparent_window(conn, window, tray_window, 0, 0);
  xcb_map_window(conn, window);
  xcb_client_message_event_t notify;
  memset(&notify, 0, sizeof(notify));
  notify.response_type = XCB_CLIENT_MESSAGE;
  notify.format = 32;
  notify.window = window;
  notify.type = xembed_atom;
  notify.data.data32[0] = XCB_CURRENT_TIME;
  notify.data.data32[1] = XEMBED_EMBEDDED_NOTIFY;
  notify.data.data32[3] = tray_window;
  notify.data.data32[4] = 0;
  xcb_send_event(conn, 0, window, XCB_EVENT_MASK_NO_EVENT,
                 (const char*)&notify);
  xcb_flush(conn);
  log_msg("docked 0x%x: %s, pid %u", window, icon->id ? icon->id : "?",
          icon->pid);
  export_icon(icon);
}

static void undock(xcb_window_t window) {
  for (struct Icon** link = &icons; *link; link = &(*link)->next) {
    struct Icon* icon = *link;
    if (icon->window != window) {
      continue;
    }
    *link = icon->next;
    if (icon->bus) {
      // Its connection going is what removes the item.
      g_dbus_connection_close_sync(icon->bus, NULL, NULL);
      g_object_unref(icon->bus);
    }
    log_msg("undocked 0x%x", window);
    g_free(icon->id);
    g_free(icon->title);
    g_free(icon);
    return;
  }
}

static gboolean on_x_events(gint fd, GIOCondition condition, gpointer data) {
  xcb_generic_event_t* event;
  while ((event = xcb_poll_for_event(conn))) {
    switch (event->response_type & ~0x80) {
      case XCB_CLIENT_MESSAGE: {
        xcb_client_message_event_t* message = (void*)event;
        if (message->type == opcode_atom &&
            message->data.data32[1] == SYSTEM_TRAY_REQUEST_DOCK) {
          dock(message->data.data32[2]);
        }
        break;
      }
      case XCB_DESTROY_NOTIFY:
        undock(((xcb_destroy_notify_event_t*)event)->window);
        break;
      case XCB_REPARENT_NOTIFY: {
        xcb_reparent_notify_event_t* reparent = (void*)event;
        if (reparent->parent != tray_window) {
          undock(reparent->window);
        }
        break;
      }
      case XCB_SELECTION_CLEAR:
        log_msg("another tray took over");
        g_main_loop_quit(loop);
        break;
    }
    free(event);
  }
  if (xcb_connection_has_error(conn)) {
    log_msg("the X display went");
    g_main_loop_quit(loop);
    return G_SOURCE_REMOVE;
  }
  return G_SOURCE_CONTINUE;
}

int main(void) {
  openlog("nickel-xembed-tray", LOG_PID, LOG_DAEMON);
  // The job starts with nickel-x11, before Xwayland is up: wait for it.
  int screen_number = 0;
  for (int tries = 0;; ++tries) {
    conn = xcb_connect(NULL, &screen_number);
    if (!xcb_connection_has_error(conn)) {
      break;
    }
    xcb_disconnect(conn);
    if (tries == 120) {
      log_msg("cannot open the X display");
      return 1;
    }
    g_usleep(500 * 1000);
  }
  xcb_screen_iterator_t it = xcb_setup_roots_iterator(xcb_get_setup(conn));
  for (int i = 0; i < screen_number; ++i) {
    xcb_screen_next(&it);
  }
  screen = it.data;
  node = g_dbus_node_info_new_for_xml(kItemXml, NULL);

  char selection_name[32];
  snprintf(selection_name, sizeof(selection_name), "_NET_SYSTEM_TRAY_S%d",
           screen_number);
  xcb_atom_t selection = intern(selection_name);
  opcode_atom = intern("_NET_SYSTEM_TRAY_OPCODE");
  xembed_atom = intern("_XEMBED");
  xembed_info_atom = intern("_XEMBED_INFO");
  pid_atom = intern("_NET_WM_PID");
  net_name_atom = intern("_NET_WM_NAME");
  utf8_atom = intern("UTF8_STRING");
  xcb_atom_t orientation = intern("_NET_SYSTEM_TRAY_ORIENTATION");
  xcb_atom_t visual = intern("_NET_SYSTEM_TRAY_VISUAL");
  xcb_atom_t manager = intern("MANAGER");

  // Never mapped: neither it nor the icons in it become ChromeOS windows.
  tray_window = xcb_generate_id(conn);
  uint32_t mask = XCB_EVENT_MASK_STRUCTURE_NOTIFY |
                  XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY;
  xcb_create_window(conn, XCB_COPY_FROM_PARENT, tray_window, screen->root, 0,
                    0, 1, 1, 0, XCB_WINDOW_CLASS_INPUT_OUTPUT,
                    screen->root_visual, XCB_CW_EVENT_MASK, &mask);
  uint32_t horizontal = 0;
  xcb_change_property(conn, XCB_PROP_MODE_REPLACE, tray_window, orientation,
                      XCB_ATOM_CARDINAL, 32, 1, &horizontal);
  xcb_change_property(conn, XCB_PROP_MODE_REPLACE, tray_window, visual,
                      XCB_ATOM_VISUALID, 32, 1, &screen->root_visual);
  xcb_set_selection_owner(conn, tray_window, selection, XCB_CURRENT_TIME);
  xcb_get_selection_owner_reply_t* owner = xcb_get_selection_owner_reply(
      conn, xcb_get_selection_owner(conn, selection), NULL);
  if (!owner || owner->owner != tray_window) {
    log_msg("another tray owns %s", selection_name);
    return 1;
  }
  free(owner);
  // Icons waiting for a tray dock on this.
  xcb_client_message_event_t announce;
  memset(&announce, 0, sizeof(announce));
  announce.response_type = XCB_CLIENT_MESSAGE;
  announce.format = 32;
  announce.window = screen->root;
  announce.type = manager;
  announce.data.data32[0] = XCB_CURRENT_TIME;
  announce.data.data32[1] = selection;
  announce.data.data32[2] = tray_window;
  xcb_send_event(conn, 0, screen->root, XCB_EVENT_MASK_STRUCTURE_NOTIFY,
                 (const char*)&announce);
  xcb_flush(conn);
  log_msg("tray %s on %s", selection_name, getenv("DISPLAY"));

  loop = g_main_loop_new(NULL, FALSE);
  g_unix_fd_add(xcb_get_file_descriptor(conn), G_IO_IN, on_x_events, NULL);
  on_x_events(0, G_IO_IN, NULL);
  g_main_loop_run(loop);
  return 0;
}
