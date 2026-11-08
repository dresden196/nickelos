/* NickelOS: typing and middle-click paste in Linux apps, at the protocol
 * level. A small window that either types through text-input-v3 (what GTK 3
 * and 4, Qt 6 and Electron use for input methods) and prints what ChromeOS's
 * input method sends it, or sets or reads the primary selection. Run inside
 * the session (nickel-session-exec); vmtest/ime-test.sh drives it.
 *   wl-ime-test textinput SECONDS     prints enter/leave/preedit/commit/done
 *                                     lines, then "text=..." (what was typed)
 *   wl-ime-test primary-set TEXT SECONDS
 *                                     selects TEXT (prints "sent" when an app
 *                                     reads it)
 *   wl-ime-test primary-get SECONDS   prints "got=..." (the selection) or
 *                                     "got none"
 *   wl-ime-test clipboard-set URIS SECONDS
 *                                     copies files, as a file manager does:
 *                                     the clipboard holds URIS as
 *                                     text/uri-list (prints "sent")
 * Build: see vmtest/ime-test.sh (protocol code from wayland-scanner). */
#define _GNU_SOURCE
#include <fcntl.h>
#include <poll.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#include <wayland-client.h>

#include "primary-selection-unstable-v1-client.h"
#include "text-input-unstable-v3-client.h"
#include "xdg-shell-client.h"

static struct wl_compositor *compositor;
static struct wl_shm *shm;
static struct wl_seat *seat;
static struct xdg_wm_base *wm_base;
static struct zwp_text_input_manager_v3 *ti_manager;
static struct zwp_primary_selection_device_manager_v1 *ps_manager;

static struct wl_surface *surface;
static struct xdg_surface *xdg_surface;
static struct xdg_toplevel *toplevel;
static int configured;
static uint32_t keyboard_serial;

static const char *mode;
static const char *selection_text;

static void out(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void out(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vprintf(fmt, ap);
  va_end(ap);
  putchar('\n');
  fflush(stdout);
}

/* --- window ------------------------------------------------------------- */

static struct wl_buffer *white_buffer(int width, int height) {
  int stride = width * 4, size = stride * height;
  int fd = memfd_create("wl-ime-test", MFD_CLOEXEC);
  if (fd < 0 || ftruncate(fd, size) < 0) {
    return NULL;
  }
  void *data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  memset(data, 0xff, size);
  munmap(data, size);
  struct wl_shm_pool *pool = wl_shm_create_pool(shm, fd, size);
  struct wl_buffer *buffer = wl_shm_pool_create_buffer(
      pool, 0, width, height, stride, WL_SHM_FORMAT_XRGB8888);
  wl_shm_pool_destroy(pool);
  close(fd);
  return buffer;
}

static void wm_base_ping(void *data, struct xdg_wm_base *base,
                         uint32_t serial) {
  xdg_wm_base_pong(base, serial);
}
static const struct xdg_wm_base_listener wm_base_listener = {wm_base_ping};

static void xdg_surface_configure(void *data, struct xdg_surface *xs,
                                  uint32_t serial) {
  xdg_surface_ack_configure(xs, serial);
  if (!configured) {
    configured = 1;
    wl_surface_attach(surface, white_buffer(320, 200), 0, 0);
    wl_surface_damage(surface, 0, 0, 320, 200);
  }
  wl_surface_commit(surface);
}
static const struct xdg_surface_listener xdg_surface_listener = {
    xdg_surface_configure};

static void toplevel_configure(void *data, struct xdg_toplevel *t, int32_t w,
                               int32_t h, struct wl_array *states) {}
static void toplevel_close(void *data, struct xdg_toplevel *t) {}
static const struct xdg_toplevel_listener toplevel_listener = {
    toplevel_configure, toplevel_close};

/* --- keyboard (for focus and the selection's serial) -------------------- */

static void kb_keymap(void *d, struct wl_keyboard *k, uint32_t format,
                      int32_t fd, uint32_t size) {
  close(fd);
}
static void kb_enter(void *d, struct wl_keyboard *k, uint32_t serial,
                     struct wl_surface *s, struct wl_array *keys) {
  keyboard_serial = serial;
  out("keyboard enter");
}
static void kb_leave(void *d, struct wl_keyboard *k, uint32_t serial,
                     struct wl_surface *s) {
  out("keyboard leave");
}
static void kb_key(void *d, struct wl_keyboard *k, uint32_t serial,
                   uint32_t time, uint32_t key, uint32_t state) {
  keyboard_serial = serial;
}
static void kb_modifiers(void *d, struct wl_keyboard *k, uint32_t serial,
                         uint32_t dep, uint32_t lat, uint32_t lock,
                         uint32_t group) {}
static void kb_repeat(void *d, struct wl_keyboard *k, int32_t rate,
                      int32_t delay) {}
static const struct wl_keyboard_listener keyboard_listener = {
    kb_keymap, kb_enter, kb_leave, kb_key, kb_modifiers, kb_repeat};

/* --- text-input-v3 ------------------------------------------------------ */

static struct zwp_text_input_v3 *text_input;
static char text[4096];  /* what was typed: the field's content */
static char pending_preedit[1024], pending_commit[1024];
static uint32_t pending_delete_before, pending_delete_after;
static int have_commit;

static void send_state(void) {
  size_t len = strlen(text);
  zwp_text_input_v3_set_surrounding_text(text_input, text, len, len);
  zwp_text_input_v3_set_cursor_rectangle(text_input, 10 + len * 8, 10, 1, 20);
  zwp_text_input_v3_commit(text_input);
}

static void ti_enter(void *d, struct zwp_text_input_v3 *ti,
                     struct wl_surface *s) {
  out("enter");
  zwp_text_input_v3_enable(ti);
  zwp_text_input_v3_set_content_type(
      ti, ZWP_TEXT_INPUT_V3_CONTENT_HINT_NONE,
      ZWP_TEXT_INPUT_V3_CONTENT_PURPOSE_NORMAL);
  send_state();
}
static void ti_leave(void *d, struct zwp_text_input_v3 *ti,
                     struct wl_surface *s) {
  out("leave");
  zwp_text_input_v3_disable(ti);
  zwp_text_input_v3_commit(ti);
}
static void ti_preedit(void *d, struct zwp_text_input_v3 *ti, const char *t,
                       int32_t begin, int32_t end) {
  snprintf(pending_preedit, sizeof(pending_preedit), "%s", t ? t : "");
  out("preedit=\"%s\" cursor=%d..%d", t ? t : "", begin, end);
}
static void ti_commit(void *d, struct zwp_text_input_v3 *ti, const char *t) {
  snprintf(pending_commit, sizeof(pending_commit), "%s", t ? t : "");
  have_commit = 1;
  out("commit=\"%s\"", t ? t : "");
}
static void ti_delete(void *d, struct zwp_text_input_v3 *ti, uint32_t before,
                      uint32_t after) {
  pending_delete_before = before;
  pending_delete_after = after;
  out("delete before=%u after=%u", before, after);
}
static void ti_done(void *d, struct zwp_text_input_v3 *ti, uint32_t serial) {
  out("done serial=%u", serial);
  /* Apply as the protocol orders it: delete, then insert the commit; the
   * cursor is at the end of the text. */
  size_t len = strlen(text);
  if (pending_delete_before) {
    size_t n = pending_delete_before > len ? len : pending_delete_before;
    text[len - n] = '\0';
  }
  if (have_commit) {
    strncat(text, pending_commit, sizeof(text) - strlen(text) - 1);
  }
  pending_delete_before = pending_delete_after = 0;
  have_commit = 0;
  pending_commit[0] = '\0';
  send_state();
}
static const struct zwp_text_input_v3_listener text_input_listener = {
    ti_enter, ti_leave, ti_preedit, ti_commit, ti_delete, ti_done};

/* --- primary selection -------------------------------------------------- */

static struct zwp_primary_selection_device_v1 *ps_device;
static int got_selection;

static void source_send(void *d, struct zwp_primary_selection_source_v1 *src,
                        const char *mime, int32_t fd) {
  ssize_t ignored = write(fd, selection_text, strlen(selection_text));
  (void)ignored;
  close(fd);
  out("sent (%s)", mime);
}
static void source_cancelled(void *d,
                             struct zwp_primary_selection_source_v1 *src) {
  out("cancelled");
}
static const struct zwp_primary_selection_source_v1_listener source_listener =
    {source_send, source_cancelled};

static void offer_offer(void *d, struct zwp_primary_selection_offer_v1 *o,
                        const char *mime) {
  out("offered %s", mime);
}
static const struct zwp_primary_selection_offer_v1_listener offer_listener = {
    offer_offer};

static void device_data_offer(void *d,
                              struct zwp_primary_selection_device_v1 *dev,
                              struct zwp_primary_selection_offer_v1 *offer) {
  zwp_primary_selection_offer_v1_add_listener(offer, &offer_listener, NULL);
}

static void device_selection(void *d,
                             struct zwp_primary_selection_device_v1 *dev,
                             struct zwp_primary_selection_offer_v1 *offer) {
  if (strcmp(mode, "primary-get") != 0) {
    return;
  }
  if (!offer) {
    out("got none");
    got_selection = 1;
    return;
  }
  int fds[2];
  if (pipe2(fds, O_CLOEXEC) < 0) {
    return;
  }
  zwp_primary_selection_offer_v1_receive(offer, "text/plain;charset=utf-8",
                                         fds[1]);
  close(fds[1]);
  wl_display_flush((struct wl_display *)d);
  char buf[4096];
  size_t len = 0;
  struct pollfd pfd = {fds[0], POLLIN, 0};
  while (len < sizeof(buf) - 1 && poll(&pfd, 1, 3000) > 0) {
    ssize_t n = read(fds[0], buf + len, sizeof(buf) - 1 - len);
    if (n <= 0) {
      break;
    }
    len += n;
  }
  buf[len] = '\0';
  close(fds[0]);
  out("got=\"%s\"", buf);
  got_selection = 1;
}
static const struct zwp_primary_selection_device_v1_listener device_listener =
    {device_data_offer, device_selection};

/* --- clipboard (wl_data_device) ---------------------------------------- */

static struct wl_data_device_manager *dd_manager;

static void dsource_target(void *d, struct wl_data_source *src,
                           const char *mime) {}
static void dsource_send(void *d, struct wl_data_source *src,
                         const char *mime, int32_t fd) {
  ssize_t ignored = write(fd, selection_text, strlen(selection_text));
  (void)ignored;
  close(fd);
  out("sent (%s)", mime);
}
static void dsource_cancelled(void *d, struct wl_data_source *src) {
  out("cancelled");
}
static void dsource_dnd_drop_performed(void *d, struct wl_data_source *src) {}
static void dsource_dnd_finished(void *d, struct wl_data_source *src) {}
static void dsource_action(void *d, struct wl_data_source *src,
                           uint32_t action) {}
static const struct wl_data_source_listener dsource_listener = {
    dsource_target, dsource_send, dsource_cancelled,
    dsource_dnd_drop_performed, dsource_dnd_finished, dsource_action};

/* --- setup -------------------------------------------------------------- */

static void global(void *data, struct wl_registry *reg, uint32_t name,
                   const char *iface, uint32_t version) {
  if (!strcmp(iface, "wl_compositor")) {
    compositor = wl_registry_bind(reg, name, &wl_compositor_interface,
                                  version < 4 ? version : 4);
  } else if (!strcmp(iface, "wl_shm")) {
    shm = wl_registry_bind(reg, name, &wl_shm_interface, 1);
  } else if (!strcmp(iface, "wl_seat")) {
    seat = wl_registry_bind(reg, name, &wl_seat_interface,
                            version < 5 ? version : 5);
  } else if (!strcmp(iface, "xdg_wm_base")) {
    wm_base = wl_registry_bind(reg, name, &xdg_wm_base_interface, 1);
  } else if (!strcmp(iface, "zwp_text_input_manager_v3")) {
    ti_manager =
        wl_registry_bind(reg, name, &zwp_text_input_manager_v3_interface, 1);
  } else if (!strcmp(iface, "wl_data_device_manager")) {
    dd_manager = wl_registry_bind(reg, name, &wl_data_device_manager_interface,
                                  version < 3 ? version : 3);
  } else if (!strcmp(iface, "zwp_primary_selection_device_manager_v1")) {
    ps_manager = wl_registry_bind(
        reg, name, &zwp_primary_selection_device_manager_v1_interface, 1);
  }
}
static void global_remove(void *data, struct wl_registry *reg,
                          uint32_t name) {}
static const struct wl_registry_listener registry_listener = {global,
                                                              global_remove};

static double now(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return ts.tv_sec + ts.tv_nsec / 1e9;
}

int main(int argc, char **argv) {
  if (argc < 3) {
    fprintf(stderr, "usage: see the comment at the top\n");
    return 2;
  }
  mode = argv[1];
  double seconds = atof(argv[argc - 1]);
  if (!strcmp(mode, "primary-set") || !strcmp(mode, "clipboard-set")) {
    if (argc < 4) {
      return 2;
    }
    selection_text = argv[2];
  }

  struct wl_display *display = wl_display_connect(NULL);
  if (!display) {
    out("no display");
    return 1;
  }
  struct wl_registry *registry = wl_display_get_registry(display);
  wl_registry_add_listener(registry, &registry_listener, NULL);
  wl_display_roundtrip(display);
  if (!compositor || !shm || !seat || !wm_base) {
    out("missing core globals");
    return 1;
  }
  if (!strcmp(mode, "textinput") && !ti_manager) {
    out("no zwp_text_input_manager_v3");
    return 1;
  }
  if (!strncmp(mode, "primary", 7) && !ps_manager) {
    out("no zwp_primary_selection_device_manager_v1");
    return 1;
  }

  xdg_wm_base_add_listener(wm_base, &wm_base_listener, NULL);
  struct wl_keyboard *keyboard = wl_seat_get_keyboard(seat);
  wl_keyboard_add_listener(keyboard, &keyboard_listener, NULL);
  surface = wl_compositor_create_surface(compositor);
  xdg_surface = xdg_wm_base_get_xdg_surface(wm_base, surface);
  xdg_surface_add_listener(xdg_surface, &xdg_surface_listener, NULL);
  toplevel = xdg_surface_get_toplevel(xdg_surface);
  xdg_toplevel_add_listener(toplevel, &toplevel_listener, NULL);
  xdg_toplevel_set_title(toplevel, "wl-ime-test");
  wl_surface_commit(surface);

  struct wl_data_device *data_device = NULL;
  if (!strcmp(mode, "textinput")) {
    text_input = zwp_text_input_manager_v3_get_text_input(ti_manager, seat);
    zwp_text_input_v3_add_listener(text_input, &text_input_listener, NULL);
  } else if (!strcmp(mode, "clipboard-set")) {
    if (!dd_manager) {
      out("no wl_data_device_manager");
      return 1;
    }
    data_device = wl_data_device_manager_get_data_device(dd_manager, seat);
  } else {
    ps_device =
        zwp_primary_selection_device_manager_v1_get_device(ps_manager, seat);
    zwp_primary_selection_device_v1_add_listener(ps_device, &device_listener,
                                                 display);
  }

  int selection_set = 0;
  double end = now() + seconds;
  while (now() < end) {
    if (!strcmp(mode, "primary-set") && !selection_set && keyboard_serial) {
      struct zwp_primary_selection_source_v1 *source =
          zwp_primary_selection_device_manager_v1_create_source(ps_manager);
      zwp_primary_selection_source_v1_add_listener(source, &source_listener,
                                                   NULL);
      zwp_primary_selection_source_v1_offer(source,
                                            "text/plain;charset=utf-8");
      zwp_primary_selection_source_v1_offer(source, "UTF8_STRING");
      zwp_primary_selection_device_v1_set_selection(ps_device, source,
                                                    keyboard_serial);
      selection_set = 1;
      out("selected \"%s\"", selection_text);
    }
    if (!strcmp(mode, "clipboard-set") && !selection_set && keyboard_serial) {
      struct wl_data_source *source =
          wl_data_device_manager_create_data_source(dd_manager);
      wl_data_source_add_listener(source, &dsource_listener, NULL);
      wl_data_source_offer(source, "text/uri-list");
      wl_data_device_set_selection(data_device, source, keyboard_serial);
      selection_set = 1;
      out("copied %s", selection_text);
    }
    if (!strcmp(mode, "primary-get") && got_selection) {
      break;
    }
    wl_display_flush(display);
    struct pollfd pfd = {wl_display_get_fd(display), POLLIN, 0};
    if (poll(&pfd, 1, 100) > 0 && wl_display_dispatch(display) < 0) {
      out("connection lost");
      return 1;
    }
  }
  if (!strcmp(mode, "textinput")) {
    out("text=\"%s\"", text);
  }
  return 0;
}
