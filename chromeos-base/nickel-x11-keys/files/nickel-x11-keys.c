// Distributed under the terms of the GNU General Public License v2
//
// nickel-x11-keys: shortcut keys for X11 apps while other windows have the
// focus. Under Xwayland an X11 app sees keys only while an X11 window has
// the focus, so X11 apps that watch the keyboard themselves (the official
// Discord client's push-to-talk) stop hearing them once a ChromeOS or
// Wayland window is in front. Chrome (patch 0047, NickelX11Keys) passes on
// what cannot be text: the modifier keys, keys pressed with Ctrl, Alt or
// Search, and the function keys, unless the user turned that off in
// Settings > Apps.
//
// It connects to Chrome with VmApplicationsService.NickelX11KeysConnect and
// one end of a socket pair; Chrome writes "d EVDEV" when such a key goes
// down and "u EVDEV" when it comes up. While no X11 window has the focus
// (sommelier then sets the X focus to none), it sets the focus to the root
// window and presses the key there with XTest: X11 apps see it as they
// would on an X11 desktop (raw XInput events, XRecord, the keyboard state,
// passive grabs on the root window), and no X11 window gets it as typing.
// A key it pressed it always releases. While an X11 window has the focus,
// that window gets its keys from sommelier as always and nothing is
// pressed here.
//
// Run by the nickel-x11-keys job as the session user, with DISPLAY set; it
// exits when the X server or Chrome goes, and the job starts it again.

#include <gio/gio.h>
#include <gio/gunixfdlist.h>
#include <glib-unix.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <syslog.h>
#include <unistd.h>
#include <xcb/xcb.h>
#include <xcb/xtest.h>

#define CHROME_NAME "org.chromium.VmApplicationsService"
#define CHROME_PATH "/org/chromium/VmApplicationsService"
#define CHROME_INTERFACE "org.chromium.VmApplicationsService"

// evdev key codes go up to KEY_MAX (0x2ff); X keycodes are evdev + 8, at
// most 255.
#define MAX_KEY 248

static xcb_connection_t* conn;
static xcb_window_t root;
static GMainLoop* loop;
static gboolean pressed[MAX_KEY];

static void log_msg(const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vsyslog(LOG_INFO, fmt, ap);
  va_end(ap);
}

// Whether an X11 window has the focus: then it gets its keys itself.
static gboolean x11_window_focused(void) {
  xcb_get_input_focus_reply_t* focus =
      xcb_get_input_focus_reply(conn, xcb_get_input_focus(conn), NULL);
  gboolean window = focus && focus->focus != XCB_NONE &&
                    focus->focus != XCB_INPUT_FOCUS_POINTER_ROOT &&
                    focus->focus != root;
  free(focus);
  return window;
}

static void press(unsigned key) {
  if (x11_window_focused()) {
    return;
  }
  // Focus none drops key events before passive grabs see them; the root
  // window is no app's.
  xcb_set_input_focus(conn, XCB_INPUT_FOCUS_NONE, root, XCB_CURRENT_TIME);
  xcb_test_fake_input(conn, XCB_KEY_PRESS, key + 8, XCB_CURRENT_TIME, root, 0,
                      0, 0);
  xcb_flush(conn);
  pressed[key] = TRUE;
}

static void release(unsigned key) {
  if (!pressed[key]) {
    return;
  }
  xcb_test_fake_input(conn, XCB_KEY_RELEASE, key + 8, XCB_CURRENT_TIME, root,
                      0, 0, 0);
  xcb_flush(conn);
  pressed[key] = FALSE;
}

static void release_all(void) {
  for (unsigned key = 0; key < MAX_KEY; ++key) {
    release(key);
  }
}

static gboolean on_chrome(gint fd, GIOCondition condition, gpointer data) {
  char message[32];
  ssize_t n = (condition & G_IO_IN)
                  ? recv(fd, message, sizeof(message) - 1, MSG_DONTWAIT)
                  : 0;
  if (n < 0) {
    return G_SOURCE_CONTINUE;
  }
  if (n == 0) {
    log_msg("Chrome closed the connection");
    release_all();
    g_main_loop_quit(loop);
    return G_SOURCE_REMOVE;
  }
  message[n] = '\0';
  char kind = 0;
  unsigned key = 0;
  if (sscanf(message, "%c %u", &kind, &key) != 2 || key >= MAX_KEY) {
    return G_SOURCE_CONTINUE;
  }
  if (kind == 'd') {
    press(key);
  } else if (kind == 'u') {
    release(key);
  }
  return G_SOURCE_CONTINUE;
}

static gboolean on_x_events(gint fd, GIOCondition condition, gpointer data) {
  xcb_generic_event_t* event;
  while ((event = xcb_poll_for_event(conn))) {
    free(event);
  }
  if (xcb_connection_has_error(conn)) {
    log_msg("the X display went");
    g_main_loop_quit(loop);
    return G_SOURCE_REMOVE;
  }
  return G_SOURCE_CONTINUE;
}

// Hands Chrome its end of the socket; FALSE if Chrome is not there (yet).
static gboolean connect_chrome(GDBusConnection* bus, int fd) {
  g_autoptr(GError) error = NULL;
  g_autoptr(GUnixFDList) fds = g_unix_fd_list_new();
  g_unix_fd_list_append(fds, fd, NULL);
  g_autoptr(GVariant) reply = g_dbus_connection_call_with_unix_fd_list_sync(
      bus, CHROME_NAME, CHROME_PATH, CHROME_INTERFACE, "NickelX11KeysConnect",
      g_variant_new("(h)", 0), NULL, G_DBUS_CALL_FLAGS_NONE, 10000, fds, NULL,
      NULL, &error);
  if (!reply) {
    log_msg("NickelX11KeysConnect: %s", error->message);
  }
  return reply != NULL;
}

int main(void) {
  openlog("nickel-x11-keys", LOG_PID, LOG_DAEMON);
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
  root = it.data->root;
  xcb_test_get_version_reply_t* version = xcb_test_get_version_reply(
      conn, xcb_test_get_version(conn, 2, 2), NULL);
  if (!version) {
    log_msg("the X server has no XTEST");
    return 1;
  }
  free(version);

  g_autoptr(GError) error = NULL;
  GDBusConnection* bus = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, &error);
  if (!bus) {
    log_msg("no system bus: %s", error->message);
    return 1;
  }
  int pair[2];
  if (socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, pair) < 0) {
    log_msg("socketpair: %m");
    return 1;
  }
  for (int tries = 0; !connect_chrome(bus, pair[1]); ++tries) {
    if (tries == 30) {
      return 1;
    }
    g_usleep(2 * G_USEC_PER_SEC);
  }
  close(pair[1]);
  log_msg("forwarding shortcut keys to X11 apps on %s", getenv("DISPLAY"));

  loop = g_main_loop_new(NULL, FALSE);
  g_unix_fd_add(pair[0], G_IO_IN | G_IO_HUP | G_IO_ERR, on_chrome, NULL);
  g_unix_fd_add(xcb_get_file_descriptor(conn), G_IO_IN, on_x_events, NULL);
  g_main_loop_run(loop);
  return 0;
}
