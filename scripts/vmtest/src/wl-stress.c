/* NickelOS: a Linux app must never take the desktop down. Each scenario here
 * is something a buggy (or careless) Wayland client can do with the standard
 * protocols Exo shows it: odd values, odd orders, too much of something. A
 * scenario runs on its own connection and ends with "ok", the protocol error
 * Exo answered with, or a lost connection; vmtest/exo-stress-test.sh runs
 * each one and checks Chrome is still the same process afterwards.
 *   wl-stress --list          the scenarios, one per line
 *   wl-stress NAME            run one
 * Build: see vmtest/exo-stress-test.sh (protocol code from wayland-scanner). */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <wayland-client.h>

#include "content-type-v1-client.h"
#include "cursor-shape-v1-client.h"
#include "fractional-scale-v1-client.h"
#include "idle-inhibit-unstable-v1-client.h"
#include "input-timestamps-unstable-v1-client.h"
#include "keyboard-shortcuts-inhibit-unstable-v1-client.h"
#include "linux-dmabuf-unstable-v1-client.h"
#include "pointer-constraints-unstable-v1-client.h"
#include "pointer-gestures-unstable-v1-client.h"
#include "presentation-time-client.h"
#include "relative-pointer-unstable-v1-client.h"
#include "single-pixel-buffer-v1-client.h"
#include "text-input-unstable-v1-client.h"
#include "viewporter-client.h"
#include "xdg-activation-v1-client.h"
#include "xdg-decoration-unstable-v1-client.h"
#include "xdg-output-unstable-v1-client.h"
#include "xdg-shell-client.h"

/* ---- connection and globals ---------------------------------------------- */

struct ctx {
  struct wl_display *display;
  struct wl_registry *registry;
  struct wl_compositor *compositor;
  struct wl_subcompositor *subcompositor;
  struct wl_shm *shm;
  struct wl_seat *seat;
  struct wl_output *output;
  struct wl_shell *wl_shell;
  struct wl_data_device_manager *ddm;
  struct xdg_wm_base *wm_base;
  struct wp_viewporter *viewporter;
  struct wp_presentation *presentation;
  struct zwp_linux_dmabuf_v1 *dmabuf;
  struct zwp_text_input_manager_v1 *text_input;
  struct zwp_relative_pointer_manager_v1 *relative_pointer;
  struct zwp_pointer_constraints_v1 *constraints;
  struct zwp_idle_inhibit_manager_v1 *idle_inhibit;
  struct zwp_keyboard_shortcuts_inhibit_manager_v1 *shortcuts_inhibit;
  struct zxdg_output_manager_v1 *xdg_output;
  struct zxdg_decoration_manager_v1 *decoration;
  struct zwp_pointer_gestures_v1 *gestures;
  struct zwp_input_timestamps_manager_v1 *timestamps;
  struct xdg_activation_v1 *activation;
  struct wp_fractional_scale_manager_v1 *fractional_scale;
  struct wp_content_type_manager_v1 *content_type;
  struct wp_single_pixel_buffer_manager_v1 *single_pixel;
  struct wp_cursor_shape_manager_v1 *cursor_shape;
  struct wl_pointer *pointer;
  struct wl_keyboard *keyboard;
  uint32_t keyboard_serial; /* from the last keyboard enter */
  uint32_t pointer_serial;  /* from the last pointer enter */
  struct wl_data_offer *selection; /* the last selection offer */
};

static struct ctx c;

static void *bind_global(struct wl_registry *registry, uint32_t name,
                  const struct wl_interface *interface, uint32_t version) {
  uint32_t v = version < (uint32_t)interface->version
                   ? version
                   : (uint32_t)interface->version;
  return wl_registry_bind(registry, name, interface, v);
}

static void registry_global(void *data, struct wl_registry *registry,
                            uint32_t name, const char *interface,
                            uint32_t version) {
#define BIND(field, iface)                                     \
  if (strcmp(interface, iface.name) == 0 && !c.field) {        \
    c.field = bind_global(registry, name, &iface, version);           \
    return;                                                    \
  }
  BIND(compositor, wl_compositor_interface);
  BIND(subcompositor, wl_subcompositor_interface);
  BIND(shm, wl_shm_interface);
  BIND(seat, wl_seat_interface);
  BIND(output, wl_output_interface);
  BIND(wl_shell, wl_shell_interface);
  BIND(ddm, wl_data_device_manager_interface);
  BIND(wm_base, xdg_wm_base_interface);
  BIND(viewporter, wp_viewporter_interface);
  BIND(presentation, wp_presentation_interface);
  BIND(dmabuf, zwp_linux_dmabuf_v1_interface);
  BIND(text_input, zwp_text_input_manager_v1_interface);
  BIND(relative_pointer, zwp_relative_pointer_manager_v1_interface);
  BIND(constraints, zwp_pointer_constraints_v1_interface);
  BIND(idle_inhibit, zwp_idle_inhibit_manager_v1_interface);
  BIND(shortcuts_inhibit, zwp_keyboard_shortcuts_inhibit_manager_v1_interface);
  BIND(xdg_output, zxdg_output_manager_v1_interface);
  BIND(decoration, zxdg_decoration_manager_v1_interface);
  BIND(gestures, zwp_pointer_gestures_v1_interface);
  BIND(timestamps, zwp_input_timestamps_manager_v1_interface);
  BIND(activation, xdg_activation_v1_interface);
  BIND(fractional_scale, wp_fractional_scale_manager_v1_interface);
  BIND(content_type, wp_content_type_manager_v1_interface);
  BIND(single_pixel, wp_single_pixel_buffer_manager_v1_interface);
  BIND(cursor_shape, wp_cursor_shape_manager_v1_interface);
#undef BIND
}

static void registry_global_remove(void *data, struct wl_registry *registry,
                                   uint32_t name) {}

static const struct wl_registry_listener registry_listener = {
    registry_global, registry_global_remove};

static void wm_base_ping(void *data, struct xdg_wm_base *wm_base,
                         uint32_t serial) {
  xdg_wm_base_pong(wm_base, serial);
}

static const struct xdg_wm_base_listener wm_base_listener = {wm_base_ping};

/* Input: only the serials matter (and the keymap fd must be closed). */
static void kb_keymap(void *d, struct wl_keyboard *k, uint32_t format, int fd,
                      uint32_t size) {
  close(fd);
}
static void kb_enter(void *d, struct wl_keyboard *k, uint32_t serial,
                     struct wl_surface *s, struct wl_array *keys) {
  c.keyboard_serial = serial;
}
static void kb_leave(void *d, struct wl_keyboard *k, uint32_t serial,
                     struct wl_surface *s) {}
static void kb_key(void *d, struct wl_keyboard *k, uint32_t serial,
                   uint32_t time, uint32_t key, uint32_t state) {}
static void kb_modifiers(void *d, struct wl_keyboard *k, uint32_t serial,
                         uint32_t dep, uint32_t lat, uint32_t lock,
                         uint32_t group) {}
static void kb_repeat(void *d, struct wl_keyboard *k, int32_t rate,
                      int32_t delay) {}
static const struct wl_keyboard_listener keyboard_listener = {
    kb_keymap, kb_enter, kb_leave, kb_key, kb_modifiers, kb_repeat};

static void p_enter(void *d, struct wl_pointer *p, uint32_t serial,
                    struct wl_surface *s, wl_fixed_t x, wl_fixed_t y) {
  c.pointer_serial = serial;
}
static void p_leave(void *d, struct wl_pointer *p, uint32_t serial,
                    struct wl_surface *s) {}
static void p_motion(void *d, struct wl_pointer *p, uint32_t time,
                     wl_fixed_t x, wl_fixed_t y) {}
static void p_button(void *d, struct wl_pointer *p, uint32_t serial,
                     uint32_t time, uint32_t button, uint32_t state) {}
static void p_axis(void *d, struct wl_pointer *p, uint32_t time,
                   uint32_t axis, wl_fixed_t value) {}
static void p_frame(void *d, struct wl_pointer *p) {}
static void p_axis_source(void *d, struct wl_pointer *p, uint32_t source) {}
static void p_axis_stop(void *d, struct wl_pointer *p, uint32_t time,
                        uint32_t axis) {}
static void p_axis_discrete(void *d, struct wl_pointer *p, uint32_t axis,
                            int32_t discrete) {}
static void p_axis_value120(void *d, struct wl_pointer *p, uint32_t axis,
                            int32_t value120) {}
/* Designated: newer libwayland adds members (axis_relative_direction). */
static const struct wl_pointer_listener pointer_listener = {
    .enter = p_enter,
    .leave = p_leave,
    .motion = p_motion,
    .button = p_button,
    .axis = p_axis,
    .frame = p_frame,
    .axis_source = p_axis_source,
    .axis_stop = p_axis_stop,
    .axis_discrete = p_axis_discrete,
    .axis_value120 = p_axis_value120,
};

static void connect_display(void) {
  c.display = wl_display_connect(NULL);
  if (!c.display) {
    perror("wl_display_connect");
    exit(2);
  }
  c.registry = wl_display_get_registry(c.display);
  wl_registry_add_listener(c.registry, &registry_listener, NULL);
  wl_display_roundtrip(c.display);
  if (c.wm_base)
    xdg_wm_base_add_listener(c.wm_base, &wm_base_listener, NULL);
  if (c.seat) {
    c.pointer = wl_seat_get_pointer(c.seat);
    wl_pointer_add_listener(c.pointer, &pointer_listener, NULL);
    c.keyboard = wl_seat_get_keyboard(c.seat);
    wl_keyboard_add_listener(c.keyboard, &keyboard_listener, NULL);
  }
  wl_display_roundtrip(c.display);
}

/* How the scenario ended, for the harness. */
static int finish(void) {
  wl_display_roundtrip(c.display);
  wl_display_roundtrip(c.display);
  int err = wl_display_get_error(c.display);
  if (err == EPROTO) {
    const struct wl_interface *iface = NULL;
    uint32_t id = 0;
    uint32_t code = wl_display_get_protocol_error(c.display, &iface, &id);
    printf("protocol error %u on %s\n", code, iface ? iface->name : "?");
  } else if (err) {
    printf("connection lost: %s\n", strerror(err));
  } else {
    printf("ok\n");
  }
  return 0;
}

/* ---- helpers ------------------------------------------------------------- */

/* Floods wait for Exo now and then: libwayland's 4 KiB send buffer would
 * otherwise overflow and end the client, not test the server. For the same
 * reason no string here is near 4 KiB (a message's limit). */
static void pace(void) {
  static unsigned n;
  if (++n % 32 == 0)
    wl_display_roundtrip(c.display);
}

#define BIG_STRING 3800
static const char *big_string(void) {
  static char s[BIG_STRING + 1];
  if (!s[0])
    memset(s, 'x', BIG_STRING);
  return s;
}

/* A memfd of `size` bytes, filled grey. */
static int shm_file(size_t size) {
  int fd = memfd_create("wl-stress", MFD_CLOEXEC | MFD_ALLOW_SEALING);
  if (fd < 0 || ftruncate(fd, size) < 0) {
    perror("memfd");
    exit(2);
  }
  void *p = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (p != MAP_FAILED) {
    memset(p, 0x80, size);
    munmap(p, size);
  }
  return fd;
}

/* The same in a plain file in /dev/shm, which cannot be sealed (some clients
 * use one: older libwayland-cursor, hand-rolled shm code). */
static int unsealable_file(size_t size) {
  int fd = open("/dev/shm", O_TMPFILE | O_RDWR | O_CLOEXEC, 0600);
  if (fd < 0 || ftruncate(fd, size) < 0) {
    perror("O_TMPFILE");
    exit(2);
  }
  return fd;
}

static int use_unsealable;

/* A w x h ARGB buffer; *fd_out keeps its file open when asked for. */
static struct wl_buffer *shm_buffer(int w, int h, int *fd_out) {
  size_t stride = (size_t)w * 4, size = stride * h;
  int fd = use_unsealable ? unsealable_file(size) : shm_file(size);
  struct wl_shm_pool *pool = wl_shm_create_pool(c.shm, fd, size);
  struct wl_buffer *b = wl_shm_pool_create_buffer(pool, 0, w, h, stride,
                                                  WL_SHM_FORMAT_ARGB8888);
  wl_shm_pool_destroy(pool);
  if (fd_out)
    *fd_out = fd;
  else
    close(fd);
  return b;
}

struct win {
  struct wl_surface *surface;
  struct xdg_surface *xdg_surface;
  struct xdg_toplevel *toplevel;
  struct xdg_popup *popup;
  int configured;
  uint32_t last_serial;
  int auto_ack;
};

static void xdg_surface_configure(void *data, struct xdg_surface *xs,
                                  uint32_t serial) {
  struct win *w = data;
  w->configured = 1;
  w->last_serial = serial;
  if (w->auto_ack)
    xdg_surface_ack_configure(xs, serial);
}
static const struct xdg_surface_listener xdg_surface_listener = {
    xdg_surface_configure};

static void toplevel_configure(void *d, struct xdg_toplevel *t, int32_t w,
                               int32_t h, struct wl_array *states) {}
static void toplevel_close(void *d, struct xdg_toplevel *t) {}
static void toplevel_bounds(void *d, struct xdg_toplevel *t, int32_t w,
                            int32_t h) {}
static void toplevel_caps(void *d, struct xdg_toplevel *t,
                          struct wl_array *caps) {}
static const struct xdg_toplevel_listener toplevel_listener = {
    toplevel_configure, toplevel_close, toplevel_bounds, toplevel_caps};

static void popup_configure(void *d, struct xdg_popup *p, int32_t x,
                            int32_t y, int32_t w, int32_t h) {}
static void popup_done(void *d, struct xdg_popup *p) {}
static void popup_repositioned(void *d, struct xdg_popup *p, uint32_t t) {}
static const struct xdg_popup_listener popup_listener = {
    popup_configure, popup_done, popup_repositioned};

static void wait_configured(struct win *w) {
  for (int i = 0; i < 50 && !w->configured; i++)
    if (wl_display_roundtrip(c.display) < 0)
      return;
}

/* A mapped, focused toplevel showing a w x h buffer. */
static struct win *toplevel(int w, int h) {
  struct win *win = calloc(1, sizeof(*win));
  win->auto_ack = 1;
  win->surface = wl_compositor_create_surface(c.compositor);
  win->xdg_surface = xdg_wm_base_get_xdg_surface(c.wm_base, win->surface);
  xdg_surface_add_listener(win->xdg_surface, &xdg_surface_listener, win);
  win->toplevel = xdg_surface_get_toplevel(win->xdg_surface);
  xdg_toplevel_add_listener(win->toplevel, &toplevel_listener, win);
  xdg_toplevel_set_title(win->toplevel, "wl-stress");
  xdg_toplevel_set_app_id(win->toplevel, "wl-stress");
  wl_surface_commit(win->surface);
  wait_configured(win);
  wl_surface_attach(win->surface, shm_buffer(w, h, NULL), 0, 0);
  wl_surface_damage(win->surface, 0, 0, w, h);
  wl_surface_commit(win->surface);
  wl_display_roundtrip(c.display);
  wl_display_roundtrip(c.display);
  return win;
}

static struct xdg_positioner *positioner(int w, int h) {
  struct xdg_positioner *p = xdg_wm_base_create_positioner(c.wm_base);
  xdg_positioner_set_size(p, w, h);
  xdg_positioner_set_anchor_rect(p, 0, 0, 10, 10);
  return p;
}

static struct win *popup(struct win *parent, struct xdg_positioner *pos,
                         int w, int h) {
  struct win *win = calloc(1, sizeof(*win));
  win->auto_ack = 1;
  win->surface = wl_compositor_create_surface(c.compositor);
  win->xdg_surface = xdg_wm_base_get_xdg_surface(c.wm_base, win->surface);
  xdg_surface_add_listener(win->xdg_surface, &xdg_surface_listener, win);
  win->popup = xdg_surface_get_popup(
      win->xdg_surface, parent ? parent->xdg_surface : NULL, pos);
  xdg_popup_add_listener(win->popup, &popup_listener, win);
  wl_surface_commit(win->surface);
  wait_configured(win);
  wl_surface_attach(win->surface, shm_buffer(w, h, NULL), 0, 0);
  wl_surface_commit(win->surface);
  wl_display_roundtrip(c.display);
  return win;
}

static struct wl_surface *subsurface_of(struct wl_surface *parent, int w,
                                        int h, struct wl_subsurface **out) {
  struct wl_surface *s = wl_compositor_create_surface(c.compositor);
  struct wl_subsurface *sub =
      wl_subcompositor_get_subsurface(c.subcompositor, s, parent);
  wl_subsurface_set_position(sub, 5, 5);
  wl_surface_attach(s, shm_buffer(w, h, NULL), 0, 0);
  wl_surface_commit(s);
  if (out)
    *out = sub;
  return s;
}

/* ---- scenarios: wl_shm --------------------------------------------------- */

static void shm_pool_bigger_than_file(void) {
  int fd = shm_file(4096);
  struct wl_shm_pool *pool = wl_shm_create_pool(c.shm, fd, 1 << 24);
  struct wl_buffer *b =
      wl_shm_pool_create_buffer(pool, 0, 2048, 2048, 8192,
                                WL_SHM_FORMAT_ARGB8888);
  struct win *w = toplevel(64, 64);
  wl_surface_attach(w->surface, b, 0, 0);
  wl_surface_damage(w->surface, 0, 0, INT32_MAX, INT32_MAX);
  wl_surface_commit(w->surface);
}

static void shm_pool_zero_size(void) {
  int fd = shm_file(4096);
  struct wl_shm_pool *pool = wl_shm_create_pool(c.shm, fd, 0);
  wl_shm_pool_create_buffer(pool, 0, 1, 1, 4, WL_SHM_FORMAT_ARGB8888);
}

static void shm_pool_shrink(void) {
  int fd = shm_file(65536);
  struct wl_shm_pool *pool = wl_shm_create_pool(c.shm, fd, 65536);
  wl_shm_pool_resize(pool, 4096);
  wl_shm_pool_create_buffer(pool, 0, 64, 64, 256, WL_SHM_FORMAT_ARGB8888);
}

static void shm_pool_grow_past_file(void) {
  int fd = shm_file(4096);
  struct wl_shm_pool *pool = wl_shm_create_pool(c.shm, fd, 4096);
  wl_shm_pool_resize(pool, 1 << 24);
  struct wl_buffer *b = wl_shm_pool_create_buffer(pool, 0, 2048, 2048, 8192,
                                                  WL_SHM_FORMAT_ARGB8888);
  struct win *w = toplevel(64, 64);
  wl_surface_attach(w->surface, b, 0, 0);
  wl_surface_damage(w->surface, 0, 0, 2048, 2048);
  wl_surface_commit(w->surface);
}

static void shm_buffer_offset_past_end(void) {
  int fd = shm_file(65536);
  struct wl_shm_pool *pool = wl_shm_create_pool(c.shm, fd, 65536);
  struct wl_buffer *b = wl_shm_pool_create_buffer(pool, 60000, 64, 64, 256,
                                                  WL_SHM_FORMAT_ARGB8888);
  struct win *w = toplevel(64, 64);
  wl_surface_attach(w->surface, b, 0, 0);
  wl_surface_damage(w->surface, 0, 0, 64, 64);
  wl_surface_commit(w->surface);
}

static void shm_buffer_huge_dimensions(void) {
  int fd = shm_file(65536);
  struct wl_shm_pool *pool = wl_shm_create_pool(c.shm, fd, 65536);
  struct wl_buffer *b = wl_shm_pool_create_buffer(
      pool, 0, INT32_MAX / 4, INT32_MAX / 4, INT32_MAX & ~3,
      WL_SHM_FORMAT_ARGB8888);
  struct win *w = toplevel(64, 64);
  wl_surface_attach(w->surface, b, 0, 0);
  wl_surface_damage(w->surface, 0, 0, INT32_MAX, INT32_MAX);
  wl_surface_commit(w->surface);
}

static void shm_buffer_negative_values(void) {
  int fd = shm_file(65536);
  struct wl_shm_pool *pool = wl_shm_create_pool(c.shm, fd, 65536);
  struct wl_buffer *b = wl_shm_pool_create_buffer(pool, 0, -64, -64, -256,
                                                  WL_SHM_FORMAT_ARGB8888);
  struct win *w = toplevel(64, 64);
  wl_surface_attach(w->surface, b, 0, 0);
  wl_surface_commit(w->surface);
}

static void shm_buffer_zero_stride(void) {
  int fd = shm_file(65536);
  struct wl_shm_pool *pool = wl_shm_create_pool(c.shm, fd, 65536);
  struct wl_buffer *b = wl_shm_pool_create_buffer(pool, 0, 64, 64, 0,
                                                  WL_SHM_FORMAT_ARGB8888);
  struct win *w = toplevel(64, 64);
  wl_surface_attach(w->surface, b, 0, 0);
  wl_surface_commit(w->surface);
}

static void shm_buffer_bad_format(void) {
  int fd = shm_file(65536);
  struct wl_shm_pool *pool = wl_shm_create_pool(c.shm, fd, 65536);
  wl_shm_pool_create_buffer(pool, 0, 64, 64, 256, 0x34325258 /* XR24 fourcc */
                                                      ^ 0xffff);
}

static void shm_pool_destroyed_before_buffer(void) {
  int fd = shm_file(65536);
  struct wl_shm_pool *pool = wl_shm_create_pool(c.shm, fd, 65536);
  struct wl_buffer *b = wl_shm_pool_create_buffer(pool, 0, 64, 64, 256,
                                                  WL_SHM_FORMAT_ARGB8888);
  wl_shm_pool_destroy(pool);
  close(fd);
  struct win *w = toplevel(64, 64);
  for (int i = 0; i < 50; i++) {
    wl_surface_attach(w->surface, b, 0, 0);
    wl_surface_damage(w->surface, 0, 0, 64, 64);
    wl_surface_commit(w->surface);
  }
  wl_buffer_destroy(b);
  wl_surface_commit(w->surface);
}

/* The file shrinks under a buffer that is on screen and then redrawn. A
 * memfd is sealed by Exo, so its truncation fails here. */
static void shm_file_truncated_while_shown(void) {
  int fd;
  struct win *w = toplevel(64, 64);
  struct wl_buffer *b = shm_buffer(256, 256, &fd);
  wl_surface_attach(w->surface, b, 0, 0);
  wl_surface_damage(w->surface, 0, 0, 256, 256);
  wl_surface_commit(w->surface);
  wl_display_roundtrip(c.display);
  if (ftruncate(fd, 0) < 0)
    fprintf(stderr, "ftruncate: %s\n", strerror(errno));
  for (int i = 0; i < 5; i++) {
    wl_surface_attach(w->surface, b, 0, 0);
    wl_surface_damage(w->surface, 0, 0, 256, 256);
    wl_surface_commit(w->surface);
    wl_display_roundtrip(c.display);
    usleep(100000);
  }
}

/* The same with the cursor, which the browser itself reads. The pool must
 * reach Exo before the file shrinks. */
static void shm_file_truncated_cursor(void) {
  int fd;
  struct win *w = toplevel(200, 200);
  struct wl_surface *cur = wl_compositor_create_surface(c.compositor);
  struct wl_buffer *b = shm_buffer(32, 32, &fd);
  wl_display_roundtrip(c.display);
  if (ftruncate(fd, 0) < 0)
    fprintf(stderr, "ftruncate: %s\n", strerror(errno));
  wl_surface_attach(cur, b, 0, 0);
  wl_surface_damage(cur, 0, 0, 32, 32);
  wl_surface_commit(cur);
  wl_pointer_set_cursor(c.pointer, c.pointer_serial, cur, 0, 0);
  wl_display_roundtrip(c.display);
  (void)w;
}

static void unsealable_file_truncated_while_shown(void) {
  use_unsealable = 1;
  shm_file_truncated_while_shown();
}

static void unsealable_file_truncated_cursor(void) {
  use_unsealable = 1;
  shm_file_truncated_cursor();
}

/* ---- scenarios: wl_surface, wl_region ------------------------------------ */

static void surface_scale_huge(void) {
  struct win *w = toplevel(64, 64);
  wl_surface_set_buffer_scale(w->surface, INT32_MAX);
  wl_surface_commit(w->surface);
}

static void cursor_scale_huge(void) {
  struct win *w = toplevel(200, 200);
  struct wl_surface *cur = wl_compositor_create_surface(c.compositor);
  wl_surface_attach(cur, shm_buffer(32, 32, NULL), 0, 0);
  wl_surface_set_buffer_scale(cur, 1 << 30);
  wl_surface_commit(cur);
  wl_pointer_set_cursor(c.pointer, c.pointer_serial, cur, 0, 0);
  (void)w;
}

static void cursor_scale_64(void) {
  struct win *w = toplevel(200, 200);
  struct wl_surface *cur = wl_compositor_create_surface(c.compositor);
  wl_surface_attach(cur, shm_buffer(32, 32, NULL), 0, 0);
  wl_surface_set_buffer_scale(cur, 64);
  wl_surface_commit(cur);
  wl_pointer_set_cursor(c.pointer, c.pointer_serial, cur, INT32_MAX,
                        INT32_MIN);
  (void)w;
}

static void cursor_big_buffer(void) {
  struct win *w = toplevel(200, 200);
  struct wl_surface *cur = wl_compositor_create_surface(c.compositor);
  wl_surface_attach(cur, shm_buffer(4096, 4096, NULL), 0, 0);
  wl_surface_commit(cur);
  wl_pointer_set_cursor(c.pointer, c.pointer_serial, cur, 2048, 2048);
  (void)w;
}

static void cursor_surface_with_role(void) {
  struct win *w = toplevel(200, 200);
  wl_pointer_set_cursor(c.pointer, c.pointer_serial, w->surface, 0, 0);
}

static void cursor_surface_destroyed(void) {
  struct win *w = toplevel(200, 200);
  struct wl_surface *cur = wl_compositor_create_surface(c.compositor);
  wl_surface_attach(cur, shm_buffer(32, 32, NULL), 0, 0);
  wl_surface_commit(cur);
  wl_pointer_set_cursor(c.pointer, c.pointer_serial, cur, 0, 0);
  wl_display_roundtrip(c.display);
  wl_surface_destroy(cur);
  wl_display_roundtrip(c.display);
  wl_pointer_release(c.pointer);
  (void)w;
}

static void surface_transform_invalid(void) {
  struct win *w = toplevel(64, 64);
  wl_surface_set_buffer_transform(w->surface, 99);
  wl_surface_commit(w->surface);
}

static void surface_transform_all_odd_size(void) {
  struct win *w = toplevel(64, 64);
  for (int t = 0; t < 8; t++) {
    wl_surface_set_buffer_transform(w->surface, t);
    wl_surface_set_buffer_scale(w->surface, 3);
    wl_surface_attach(w->surface, shm_buffer(7, 13, NULL), 0, 0);
    wl_surface_commit(w->surface);
    wl_display_roundtrip(c.display);
  }
}

static void surface_damage_extreme(void) {
  struct win *w = toplevel(64, 64);
  wl_surface_damage(w->surface, INT32_MIN, INT32_MIN, INT32_MAX, INT32_MAX);
  wl_surface_damage(w->surface, INT32_MAX, INT32_MAX, INT32_MAX, INT32_MAX);
  wl_surface_damage(w->surface, 0, 0, -5, -5);
  if (wl_surface_get_version(w->surface) >= 4) {
    wl_surface_damage_buffer(w->surface, INT32_MIN, INT32_MIN, INT32_MAX,
                             INT32_MAX);
    wl_surface_damage_buffer(w->surface, 0, 0, -1, INT32_MIN);
  }
  wl_surface_commit(w->surface);
}

static void surface_attach_offset_extreme(void) {
  struct win *w = toplevel(64, 64);
  if (wl_surface_get_version(w->surface) >= 5) {
    wl_surface_attach(w->surface, shm_buffer(64, 64, NULL), 0, 0);
    wl_surface_offset(w->surface, INT32_MAX, INT32_MIN);
    wl_surface_commit(w->surface);
    wl_surface_offset(w->surface, INT32_MIN, INT32_MAX);
    wl_surface_commit(w->surface);
  } else {
    wl_surface_attach(w->surface, shm_buffer(64, 64, NULL), INT32_MAX,
                      INT32_MIN);
    wl_surface_commit(w->surface);
    wl_surface_attach(w->surface, shm_buffer(64, 64, NULL), INT32_MIN,
                      INT32_MAX);
    wl_surface_commit(w->surface);
  }
}

static void surface_frame_flood(void) {
  struct win *w = toplevel(64, 64);
  for (int i = 0; i < 20000; i++) {
    wl_surface_frame(w->surface);
    if (i % 100 == 0)
      wl_surface_commit(w->surface);
    pace();
  }
  wl_surface_commit(w->surface);
}

static void surface_commit_before_configure(void) {
  struct wl_surface *s = wl_compositor_create_surface(c.compositor);
  struct xdg_surface *xs = xdg_wm_base_get_xdg_surface(c.wm_base, s);
  xdg_surface_get_toplevel(xs);
  wl_surface_attach(s, shm_buffer(64, 64, NULL), 0, 0);
  wl_surface_commit(s);
}

static void surface_destroyed_before_role(void) {
  struct win *w = toplevel(64, 64);
  wl_surface_destroy(w->surface);
  xdg_toplevel_set_title(w->toplevel, "after");
  xdg_toplevel_set_maximized(w->toplevel);
  xdg_surface_set_window_geometry(w->xdg_surface, 0, 0, 10, 10);
  xdg_toplevel_destroy(w->toplevel);
  xdg_surface_destroy(w->xdg_surface);
}

static void region_many_rectangles(void) {
  struct win *w = toplevel(400, 400);
  struct wl_region *r = wl_compositor_create_region(c.compositor);
  for (int i = 0; i < 3000; i++) {
    wl_region_add(r, (i * 7) % 400, (i * 13) % 400, 1 + i % 3, 1 + i % 5);
    if (i % 2)
      wl_region_subtract(r, (i * 11) % 400, (i * 5) % 400, 2, 2);
    pace();
  }
  wl_region_add(r, INT32_MIN, INT32_MIN, INT32_MAX, INT32_MAX);
  wl_region_subtract(r, INT32_MAX - 1, INT32_MAX - 1, INT32_MAX, INT32_MAX);
  wl_surface_set_input_region(w->surface, r);
  wl_surface_set_opaque_region(w->surface, r);
  wl_surface_commit(w->surface);
  wl_region_destroy(r);
}

/* ---- scenarios: wl_subsurface -------------------------------------------- */

static void subsurface_deep_nesting(void) {
  struct win *w = toplevel(64, 64);
  struct wl_surface *parent = w->surface;
  for (int i = 0; i < 300; i++) {
    struct wl_subsurface *sub;
    parent = subsurface_of(parent, 8, 8, &sub);
    wl_subsurface_set_desync(sub);
  }
  wl_surface_commit(w->surface);
}

static void subsurface_cycle(void) {
  struct win *w = toplevel(64, 64);
  struct wl_surface *a = subsurface_of(w->surface, 8, 8, NULL);
  struct wl_surface *b = subsurface_of(a, 8, 8, NULL);
  wl_subcompositor_get_subsurface(c.subcompositor, a, b);
  wl_surface_commit(w->surface);
}

static void subsurface_of_itself(void) {
  struct win *w = toplevel(64, 64);
  wl_subcompositor_get_subsurface(c.subcompositor, w->surface, w->surface);
}

static void subsurface_place_non_sibling(void) {
  struct win *w = toplevel(64, 64);
  struct wl_subsurface *sub;
  struct wl_surface *a = subsurface_of(w->surface, 8, 8, &sub);
  struct wl_surface *b = subsurface_of(a, 8, 8, NULL);
  wl_subsurface_place_above(sub, b);
  wl_surface_commit(w->surface);
}

static void subsurface_parent_destroyed(void) {
  struct win *w = toplevel(64, 64);
  struct wl_subsurface *sub, *sub2;
  struct wl_surface *a = subsurface_of(w->surface, 8, 8, &sub);
  struct wl_surface *b = subsurface_of(a, 8, 8, &sub2);
  wl_surface_commit(w->surface);
  wl_display_roundtrip(c.display);
  wl_surface_destroy(a);
  wl_subsurface_set_position(sub2, 100, 100);
  wl_subsurface_place_below(sub2, b);
  wl_subsurface_set_sync(sub2);
  wl_surface_commit(b);
  wl_subsurface_set_position(sub, 3, 3);
  wl_surface_commit(w->surface);
  xdg_toplevel_destroy(w->toplevel);
  xdg_surface_destroy(w->xdg_surface);
  wl_surface_destroy(w->surface);
  wl_surface_commit(b);
}

static void subsurface_sync_storm(void) {
  struct win *w = toplevel(64, 64);
  struct wl_subsurface *sub;
  struct wl_surface *a = subsurface_of(w->surface, 8, 8, &sub);
  struct wl_subsurface *sub2;
  struct wl_surface *b = subsurface_of(a, 8, 8, &sub2);
  for (int i = 0; i < 2000; i++) {
    if (i % 2) {
      wl_subsurface_set_sync(sub);
      wl_subsurface_set_desync(sub2);
    } else {
      wl_subsurface_set_desync(sub);
      wl_subsurface_set_sync(sub2);
    }
    wl_subsurface_place_above(sub2, a);
    wl_subsurface_set_position(sub, i % 50, i % 30);
    wl_surface_commit(b);
    wl_surface_commit(a);
    if (i % 3 == 0)
      wl_surface_commit(w->surface);
    pace();
  }
}

static void subsurface_of_popup_and_cursor(void) {
  struct win *w = toplevel(200, 200);
  struct win *p = popup(w, positioner(50, 50), 50, 50);
  subsurface_of(p->surface, 8, 8, NULL);
  struct wl_surface *cur = wl_compositor_create_surface(c.compositor);
  wl_surface_attach(cur, shm_buffer(16, 16, NULL), 0, 0);
  wl_surface_commit(cur);
  subsurface_of(cur, 8, 8, NULL);
  wl_surface_commit(cur);
  wl_pointer_set_cursor(c.pointer, c.pointer_serial, cur, 0, 0);
  wl_surface_commit(p->surface);
}

/* ---- scenarios: xdg_shell ------------------------------------------------ */

static void xdg_toplevel_twice(void) {
  struct win *w = toplevel(64, 64);
  xdg_surface_get_toplevel(w->xdg_surface);
}

static void xdg_geometry_extreme(void) {
  struct win *w = toplevel(64, 64);
  xdg_surface_set_window_geometry(w->xdg_surface, INT32_MIN, INT32_MIN,
                                  INT32_MAX, INT32_MAX);
  wl_surface_commit(w->surface);
  wl_display_roundtrip(c.display);
  xdg_surface_set_window_geometry(w->xdg_surface, INT32_MAX, INT32_MAX, 1, 1);
  wl_surface_commit(w->surface);
  wl_display_roundtrip(c.display);
  xdg_surface_set_window_geometry(w->xdg_surface, 0, 0, 0, 0);
  wl_surface_commit(w->surface);
}

static void xdg_min_max_size(void) {
  struct win *w = toplevel(64, 64);
  xdg_toplevel_set_min_size(w->toplevel, INT32_MAX, INT32_MAX);
  xdg_toplevel_set_max_size(w->toplevel, 1, 1);
  wl_surface_commit(w->surface);
  wl_display_roundtrip(c.display);
  xdg_toplevel_set_maximized(w->toplevel);
  wl_surface_commit(w->surface);
  wl_display_roundtrip(c.display);
  xdg_toplevel_set_min_size(w->toplevel, -5, -5);
  xdg_toplevel_set_max_size(w->toplevel, -1, -1);
  wl_surface_commit(w->surface);
}

static void xdg_parent_cycle(void) {
  struct win *a = toplevel(64, 64);
  struct win *b = toplevel(64, 64);
  xdg_toplevel_set_parent(a->toplevel, b->toplevel);
  xdg_toplevel_set_parent(b->toplevel, a->toplevel);
  wl_surface_commit(a->surface);
  wl_surface_commit(b->surface);
}

static void xdg_parent_self(void) {
  struct win *a = toplevel(64, 64);
  xdg_toplevel_set_parent(a->toplevel, a->toplevel);
  wl_surface_commit(a->surface);
}

static void xdg_parent_destroyed(void) {
  struct win *a = toplevel(64, 64);
  struct win *b = toplevel(64, 64);
  xdg_toplevel_set_parent(a->toplevel, b->toplevel);
  wl_surface_commit(a->surface);
  wl_display_roundtrip(c.display);
  xdg_toplevel_destroy(b->toplevel);
  xdg_surface_destroy(b->xdg_surface);
  wl_surface_destroy(b->surface);
  wl_display_roundtrip(c.display);
  xdg_toplevel_set_maximized(a->toplevel);
  wl_surface_commit(a->surface);
  struct win *c2 = toplevel(32, 32);
  xdg_toplevel_set_parent(c2->toplevel, a->toplevel);
  wl_surface_commit(c2->surface);
}

static void xdg_state_storm(void) {
  struct win *w = toplevel(64, 64);
  for (int i = 0; i < 300; i++) {
    switch (i % 6) {
      case 0: xdg_toplevel_set_maximized(w->toplevel); break;
      case 1: xdg_toplevel_set_fullscreen(w->toplevel, NULL); break;
      case 2: xdg_toplevel_unset_fullscreen(w->toplevel); break;
      case 3: xdg_toplevel_set_minimized(w->toplevel); break;
      case 4: xdg_toplevel_unset_maximized(w->toplevel); break;
      case 5: xdg_toplevel_set_fullscreen(w->toplevel, c.output); break;
    }
    wl_surface_attach(w->surface, shm_buffer(64 + i % 17, 64 + i % 23, NULL),
                      0, 0);
    wl_surface_commit(w->surface);
    if (i % 10 == 0)
      wl_display_roundtrip(c.display);
  }
}

static void xdg_move_resize_menu_bogus(void) {
  struct win *w = toplevel(64, 64);
  xdg_toplevel_move(w->toplevel, c.seat, 0xdeadbeef);
  xdg_toplevel_resize(w->toplevel, c.seat, c.pointer_serial, 0xffff);
  xdg_toplevel_resize(w->toplevel, c.seat, 0, 15);
  xdg_toplevel_show_window_menu(w->toplevel, c.seat, 0xffffffff, INT32_MIN,
                                INT32_MAX);
  wl_surface_commit(w->surface);
}

static void xdg_ack_bogus_serial(void) {
  struct win *w = toplevel(64, 64);
  xdg_surface_ack_configure(w->xdg_surface, 0xfffffff0);
  wl_surface_commit(w->surface);
}

static void xdg_destroy_out_of_order(void) {
  struct win *w = toplevel(64, 64);
  xdg_surface_destroy(w->xdg_surface);
  wl_surface_commit(w->surface);
}

static void xdg_wm_base_destroyed_with_surfaces(void) {
  toplevel(64, 64);
  xdg_wm_base_destroy(c.wm_base);
}

static void xdg_popup_null_parent(void) {
  toplevel(64, 64);
  popup(NULL, positioner(50, 50), 50, 50);
}

static void xdg_popup_chain(void) {
  struct win *w = toplevel(300, 300);
  struct win *parent = w;
  for (int i = 0; i < 40; i++) {
    struct win *p = popup(parent, positioner(30, 30), 30, 30);
    xdg_popup_grab(p->popup, c.seat, c.pointer_serial + i);
    parent = p;
  }
  wl_surface_commit(w->surface);
}

static void xdg_popup_parent_destroyed(void) {
  struct win *w = toplevel(300, 300);
  struct win *p = popup(w, positioner(50, 50), 50, 50);
  struct win *p2 = popup(p, positioner(20, 20), 20, 20);
  xdg_toplevel_destroy(w->toplevel);
  xdg_surface_destroy(w->xdg_surface);
  wl_surface_destroy(w->surface);
  wl_surface_attach(p2->surface, shm_buffer(20, 20, NULL), 0, 0);
  wl_surface_commit(p2->surface);
  wl_surface_commit(p->surface);
}

static void xdg_popup_destroyed_out_of_order(void) {
  struct win *w = toplevel(300, 300);
  struct win *p = popup(w, positioner(50, 50), 50, 50);
  popup(p, positioner(20, 20), 20, 20);
  xdg_popup_destroy(p->popup);
}

static void xdg_positioner_extreme(void) {
  struct win *w = toplevel(300, 300);
  struct xdg_positioner *pos = xdg_wm_base_create_positioner(c.wm_base);
  xdg_positioner_set_size(pos, INT32_MAX, INT32_MAX);
  xdg_positioner_set_anchor_rect(pos, INT32_MAX - 1, INT32_MIN, INT32_MAX,
                                 INT32_MAX);
  xdg_positioner_set_offset(pos, INT32_MIN, INT32_MAX);
  xdg_positioner_set_anchor(pos, 0xffffffff);
  xdg_positioner_set_gravity(pos, 0x7fffffff);
  xdg_positioner_set_constraint_adjustment(pos, 0xffffffff);
  if (xdg_positioner_get_version(pos) >= 3) {
    xdg_positioner_set_reactive(pos);
    xdg_positioner_set_parent_size(pos, -1, INT32_MAX);
    xdg_positioner_set_parent_configure(pos, 0xffffffff);
  }
  struct win *p = popup(w, pos, 16, 16);
  if (xdg_popup_get_version(p->popup) >= 3) {
    for (int i = 0; i < 100; i++)
      xdg_popup_reposition(p->popup, pos, i);
  }
  wl_surface_commit(p->surface);
}

static void xdg_positioner_incomplete(void) {
  struct win *w = toplevel(300, 300);
  struct xdg_positioner *pos = xdg_wm_base_create_positioner(c.wm_base);
  popup(w, pos, 16, 16);
}

static void xdg_popup_of_unmapped_parent(void) {
  struct wl_surface *s = wl_compositor_create_surface(c.compositor);
  struct xdg_surface *xs = xdg_wm_base_get_xdg_surface(c.wm_base, s);
  struct win parent = {.surface = s, .xdg_surface = xs};
  xdg_surface_get_toplevel(xs);
  popup(&parent, positioner(10, 10), 10, 10);
}

static void wl_shell_odd(void) {
  if (!c.wl_shell) {
    printf("skipped (no wl_shell)\n");
    exit(0);
  }
  struct wl_surface *s = wl_compositor_create_surface(c.compositor);
  struct wl_shell_surface *ss = wl_shell_get_shell_surface(c.wl_shell, s);
  wl_shell_surface_set_toplevel(ss);
  wl_surface_attach(s, shm_buffer(64, 64, NULL), 0, 0);
  wl_surface_commit(s);
  wl_display_roundtrip(c.display);
  wl_shell_surface_set_transient(ss, s, INT32_MIN, INT32_MAX, 0xffffffff);
  wl_shell_surface_set_fullscreen(ss, 0xffffffff, 0xffffffff, NULL);
  wl_shell_surface_set_popup(ss, c.seat, 0xdeadbeef, s, 5, 5, 0);
  wl_shell_surface_set_maximized(ss, c.output);
  wl_shell_surface_move(ss, c.seat, 0);
  wl_shell_surface_resize(ss, c.seat, 0, 0xffff);
  wl_shell_surface_set_title(ss, "");
  wl_shell_surface_set_class(ss, "");
  wl_surface_commit(s);
}

/* ---- scenarios: decoration, output, activation and the like -------------- */

static void decoration_odd(void) {
  struct win *w = toplevel(64, 64);
  struct zxdg_toplevel_decoration_v1 *d =
      zxdg_decoration_manager_v1_get_toplevel_decoration(c.decoration,
                                                         w->toplevel);
  zxdg_toplevel_decoration_v1_set_mode(d, 0xffff);
  wl_surface_commit(w->surface);
}

static void decoration_twice(void) {
  struct win *w = toplevel(64, 64);
  zxdg_decoration_manager_v1_get_toplevel_decoration(c.decoration,
                                                     w->toplevel);
  zxdg_decoration_manager_v1_get_toplevel_decoration(c.decoration,
                                                     w->toplevel);
}

static void decoration_after_toplevel(void) {
  struct win *w = toplevel(64, 64);
  struct zxdg_toplevel_decoration_v1 *d =
      zxdg_decoration_manager_v1_get_toplevel_decoration(c.decoration,
                                                         w->toplevel);
  xdg_toplevel_destroy(w->toplevel);
  zxdg_toplevel_decoration_v1_set_mode(d, 2);
  zxdg_toplevel_decoration_v1_unset_mode(d);
}

static void viewport_odd_values(void) {
  struct win *w = toplevel(64, 64);
  struct wp_viewport *vp = wp_viewporter_get_viewport(c.viewporter,
                                                      w->surface);
  wp_viewport_set_destination(vp, INT32_MAX, INT32_MAX);
  wl_surface_commit(w->surface);
  wl_display_roundtrip(c.display);
  wp_viewport_set_destination(vp, 1, 1);
  wp_viewport_set_source(vp, wl_fixed_from_int(0), wl_fixed_from_int(0),
                         1, 1);
  wl_surface_commit(w->surface);
  wl_display_roundtrip(c.display);
  wp_viewport_set_source(vp, wl_fixed_from_double(63.99),
                         wl_fixed_from_double(63.99), 1, 1);
  wp_viewport_set_destination(vp, -1, -1);
  wl_surface_commit(w->surface);
}

static void viewport_source_outside_buffer(void) {
  struct win *w = toplevel(64, 64);
  struct wp_viewport *vp = wp_viewporter_get_viewport(c.viewporter,
                                                      w->surface);
  wp_viewport_set_source(vp, wl_fixed_from_int(60), wl_fixed_from_int(60),
                         wl_fixed_from_int(100), wl_fixed_from_int(100));
  wl_surface_commit(w->surface);
}

static void viewport_bad_values(void) {
  struct win *w = toplevel(64, 64);
  struct wp_viewport *vp = wp_viewporter_get_viewport(c.viewporter,
                                                      w->surface);
  wp_viewport_set_source(vp, wl_fixed_from_int(-5), 0, 0, 0);
}

static void viewport_after_surface(void) {
  struct win *w = toplevel(64, 64);
  struct wp_viewport *vp = wp_viewporter_get_viewport(c.viewporter,
                                                      w->surface);
  xdg_toplevel_destroy(w->toplevel);
  xdg_surface_destroy(w->xdg_surface);
  wl_surface_destroy(w->surface);
  wp_viewport_set_destination(vp, 10, 10);
}

static void viewport_twice(void) {
  struct win *w = toplevel(64, 64);
  wp_viewporter_get_viewport(c.viewporter, w->surface);
  wp_viewporter_get_viewport(c.viewporter, w->surface);
}

static void single_pixel_scaled(void) {
  struct win *w = toplevel(64, 64);
  struct wl_buffer *b = wp_single_pixel_buffer_manager_v1_create_u32_rgba_buffer(
      c.single_pixel, 0xffffffff, 0, 0xffffffff, 0x7fffffff);
  struct wp_viewport *vp = wp_viewporter_get_viewport(c.viewporter,
                                                      w->surface);
  wl_surface_attach(w->surface, b, 0, 0);
  wl_surface_commit(w->surface);
  wl_display_roundtrip(c.display);
  wp_viewport_set_destination(vp, 8000, 8000);
  wl_surface_set_buffer_scale(w->surface, 2);
  wl_surface_commit(w->surface);
  wl_display_roundtrip(c.display);
  struct wl_surface *cur = wl_compositor_create_surface(c.compositor);
  wl_surface_attach(cur, b, 0, 0);
  wl_surface_commit(cur);
  wl_pointer_set_cursor(c.pointer, c.pointer_serial, cur, 0, 0);
}

static void presentation_flood(void) {
  struct win *w = toplevel(64, 64);
  for (int i = 0; i < 10000; i++) {
    wp_presentation_feedback(c.presentation, w->surface);
    if (i % 50 == 0) {
      wl_surface_damage(w->surface, 0, 0, 1, 1);
      wl_surface_commit(w->surface);
    }
    pace();
  }
  wl_surface_commit(w->surface);
  xdg_toplevel_destroy(w->toplevel);
  xdg_surface_destroy(w->xdg_surface);
  wl_surface_destroy(w->surface);
}

static void fractional_scale_odd(void) {
  struct win *w = toplevel(64, 64);
  struct wp_fractional_scale_v1 *f =
      wp_fractional_scale_manager_v1_get_fractional_scale(c.fractional_scale,
                                                          w->surface);
  wl_surface_destroy(w->surface);
  wp_fractional_scale_v1_destroy(f);
  struct win *w2 = toplevel(64, 64);
  wp_fractional_scale_manager_v1_get_fractional_scale(c.fractional_scale,
                                                      w2->surface);
  wp_fractional_scale_manager_v1_get_fractional_scale(c.fractional_scale,
                                                      w2->surface);
}

static void content_type_odd(void) {
  struct win *w = toplevel(64, 64);
  struct wp_content_type_v1 *ct =
      wp_content_type_manager_v1_get_surface_content_type(c.content_type,
                                                          w->surface);
  wp_content_type_v1_set_content_type(ct, 0xffffffff);
  wl_surface_commit(w->surface);
  wl_display_roundtrip(c.display);
  wl_surface_destroy(w->surface);
  wp_content_type_v1_set_content_type(ct, 2);
  wp_content_type_v1_destroy(ct);
}

static void content_type_twice(void) {
  struct win *w = toplevel(64, 64);
  wp_content_type_manager_v1_get_surface_content_type(c.content_type,
                                                      w->surface);
  wp_content_type_manager_v1_get_surface_content_type(c.content_type,
                                                      w->surface);
}

static void activation_odd(void) {
  struct win *w = toplevel(64, 64);
  struct win *w2 = toplevel(64, 64);
  xdg_activation_v1_activate(c.activation, "", w->surface);
  xdg_activation_v1_activate(c.activation, "not-a-token", w->surface);
  const char *big = big_string();
  xdg_activation_v1_activate(c.activation, big, w2->surface);
  struct xdg_activation_token_v1 *t =
      xdg_activation_v1_get_activation_token(c.activation);
  xdg_activation_token_v1_set_serial(t, 0xdeadbeef, c.seat);
  xdg_activation_token_v1_set_app_id(t, big);
  xdg_activation_token_v1_set_surface(t, w->surface);
  xdg_activation_token_v1_commit(t);
  wl_display_roundtrip(c.display);
  wl_surface_destroy(w->surface);
  xdg_activation_v1_activate(c.activation, "x", w2->surface);
  struct xdg_activation_token_v1 *t2 =
      xdg_activation_v1_get_activation_token(c.activation);
  xdg_activation_token_v1_commit(t2);
  xdg_activation_token_v1_commit(t2);
}

static void idle_inhibit_odd(void) {
  struct win *w = toplevel(64, 64);
  for (int i = 0; i < 1000; i++) {
    zwp_idle_inhibit_manager_v1_create_inhibitor(c.idle_inhibit, w->surface);
    pace();
  }
  wl_display_roundtrip(c.display);
  xdg_toplevel_destroy(w->toplevel);
  xdg_surface_destroy(w->xdg_surface);
  wl_surface_destroy(w->surface);
}

static void shortcuts_inhibit_odd(void) {
  struct win *w = toplevel(64, 64);
  zwp_keyboard_shortcuts_inhibit_manager_v1_inhibit_shortcuts(
      c.shortcuts_inhibit, w->surface, c.seat);
  wl_display_roundtrip(c.display);
  wl_surface_destroy(w->surface);
  wl_display_roundtrip(c.display);
  struct win *w2 = toplevel(64, 64);
  zwp_keyboard_shortcuts_inhibit_manager_v1_inhibit_shortcuts(
      c.shortcuts_inhibit, w2->surface, c.seat);
  zwp_keyboard_shortcuts_inhibit_manager_v1_inhibit_shortcuts(
      c.shortcuts_inhibit, w2->surface, c.seat);
}

static void xdg_output_odd(void) {
  for (int i = 0; i < 200; i++)
    zxdg_output_manager_v1_get_xdg_output(c.xdg_output, c.output);
  wl_display_roundtrip(c.display);
  if (wl_output_get_version(c.output) >= 3)
    wl_output_release(c.output);
  else
    wl_proxy_destroy((struct wl_proxy *)c.output);
}

static void seat_objects_odd(void) {
  for (int i = 0; i < 500; i++) {
    struct wl_pointer *p = wl_seat_get_pointer(c.seat);
    struct wl_keyboard *k = wl_seat_get_keyboard(c.seat);
    struct wl_touch *t = wl_seat_get_touch(c.seat);
    if (c.timestamps) {
      zwp_input_timestamps_manager_v1_get_pointer_timestamps(c.timestamps, p);
      zwp_input_timestamps_manager_v1_get_keyboard_timestamps(c.timestamps,
                                                               k);
      zwp_input_timestamps_manager_v1_get_touch_timestamps(c.timestamps, t);
    }
    if (c.gestures) {
      zwp_pointer_gestures_v1_get_swipe_gesture(c.gestures, p);
      zwp_pointer_gestures_v1_get_pinch_gesture(c.gestures, p);
      if (zwp_pointer_gestures_v1_get_version(c.gestures) >= 3)
        zwp_pointer_gestures_v1_get_hold_gesture(c.gestures, p);
    }
    if (c.relative_pointer)
      zwp_relative_pointer_manager_v1_get_relative_pointer(c.relative_pointer,
                                                           p);
    if (i % 2) {
      wl_pointer_release(p);
      wl_keyboard_release(k);
      wl_touch_release(t);
    }
    pace();
  }
  wl_display_roundtrip(c.display);
  wl_seat_release(c.seat);
}

/* ---- scenarios: cursor shapes ------------------------------------------- */

static void need_cursor_shape(void) {
  if (!c.cursor_shape) {
    printf("skipped (no wp_cursor_shape_manager_v1)\n");
    exit(0);
  }
}

static void cursor_shape_all(void) {
  need_cursor_shape();
  struct win *w = toplevel(200, 200);
  struct wp_cursor_shape_device_v1 *d =
      wp_cursor_shape_manager_v1_get_pointer(c.cursor_shape, c.pointer);
  uint32_t last = wp_cursor_shape_device_v1_get_version(d) >= 2 ? 36 : 34;
  for (uint32_t shape = 1; shape <= last; shape++) {
    wp_cursor_shape_device_v1_set_shape(d, c.pointer_serial, shape);
    pace();
  }
  wp_cursor_shape_device_v1_set_shape(d, 0xdeadbeef, 1);
  wp_cursor_shape_device_v1_destroy(d);
  (void)w;
}

static void cursor_shape_invalid(void) {
  need_cursor_shape();
  struct wp_cursor_shape_device_v1 *d =
      wp_cursor_shape_manager_v1_get_pointer(c.cursor_shape, c.pointer);
  wp_cursor_shape_device_v1_set_shape(d, c.pointer_serial, 999);
}

static void cursor_shape_pointer_gone(void) {
  need_cursor_shape();
  struct win *w = toplevel(200, 200);
  struct wp_cursor_shape_device_v1 *d =
      wp_cursor_shape_manager_v1_get_pointer(c.cursor_shape, c.pointer);
  wp_cursor_shape_device_v1_set_shape(d, c.pointer_serial, 4);
  wl_display_roundtrip(c.display);
  wl_pointer_release(c.pointer);
  wl_display_roundtrip(c.display);
  wp_cursor_shape_device_v1_set_shape(d, c.pointer_serial, 9);
  wl_display_roundtrip(c.display);
  wp_cursor_shape_device_v1_destroy(d);
  for (int i = 0; i < 200; i++) {
    struct wl_pointer *p = wl_seat_get_pointer(c.seat);
    struct wp_cursor_shape_device_v1 *d2 =
        wp_cursor_shape_manager_v1_get_pointer(c.cursor_shape, p);
    if (i % 2)
      wl_pointer_release(p);
    wp_cursor_shape_device_v1_set_shape(d2, 0, 1 + i % 34);
    if (i % 3)
      wp_cursor_shape_device_v1_destroy(d2);
    pace();
  }
  wp_cursor_shape_manager_v1_destroy(c.cursor_shape);
  (void)w;
}

/* ---- scenarios: pointer constraints -------------------------------------- */

static void pointer_lock_odd(void) {
  struct win *w = toplevel(200, 200);
  struct wl_region *r = wl_compositor_create_region(c.compositor);
  wl_region_add(r, INT32_MIN, INT32_MIN, INT32_MAX, INT32_MAX);
  struct zwp_locked_pointer_v1 *l = zwp_pointer_constraints_v1_lock_pointer(
      c.constraints, w->surface, c.pointer, r, 0xffff);
  zwp_locked_pointer_v1_set_cursor_position_hint(l, INT32_MAX, INT32_MIN);
  wl_region_destroy(r);
  zwp_locked_pointer_v1_set_region(l, NULL);
  wl_surface_commit(w->surface);
  wl_display_roundtrip(c.display);
  xdg_toplevel_destroy(w->toplevel);
  xdg_surface_destroy(w->xdg_surface);
  wl_surface_destroy(w->surface);
  wl_display_roundtrip(c.display);
  zwp_locked_pointer_v1_set_cursor_position_hint(l, 0, 0);
  zwp_locked_pointer_v1_destroy(l);
}

static void pointer_lock_twice(void) {
  struct win *w = toplevel(200, 200);
  zwp_pointer_constraints_v1_lock_pointer(c.constraints, w->surface,
                                          c.pointer, NULL, 2);
  zwp_pointer_constraints_v1_confine_pointer(c.constraints, w->surface,
                                             c.pointer, NULL, 2);
}

static void pointer_lock_unmapped(void) {
  struct wl_surface *s = wl_compositor_create_surface(c.compositor);
  struct zwp_locked_pointer_v1 *l = zwp_pointer_constraints_v1_lock_pointer(
      c.constraints, s, c.pointer, NULL, 1);
  wl_surface_commit(s);
  wl_display_roundtrip(c.display);
  wl_pointer_release(c.pointer);
  wl_display_roundtrip(c.display);
  zwp_locked_pointer_v1_destroy(l);
  wl_surface_destroy(s);
}

/* ---- scenarios: text input ----------------------------------------------- */

static void text_input_odd(void) {
  struct win *w = toplevel(200, 200);
  struct zwp_text_input_v1 *ti =
      zwp_text_input_manager_v1_create_text_input(c.text_input);
  zwp_text_input_v1_activate(ti, c.seat, w->surface);
  wl_display_roundtrip(c.display);
  const char *text = big_string();
  for (int i = 0; i < 200; i++) {
    zwp_text_input_v1_set_surrounding_text(ti, i % 2 ? text : "abc",
                                           0xffffffff - i, 0x80000000 + i);
    zwp_text_input_v1_set_content_type(ti, 0xffffffff, 0xffffffff);
    zwp_text_input_v1_set_cursor_rectangle(ti, INT32_MIN, INT32_MAX,
                                           INT32_MAX, INT32_MIN);
    zwp_text_input_v1_set_preferred_language(ti, i % 2 ? text : "");
    zwp_text_input_v1_commit_state(ti, 0xffffffff - i);
    zwp_text_input_v1_invoke_action(ti, 0xffffffff, 0xffffffff);
    zwp_text_input_v1_show_input_panel(ti);
    zwp_text_input_v1_reset(ti);
    wl_display_roundtrip(c.display);
  }
  zwp_text_input_v1_hide_input_panel(ti);
  zwp_text_input_v1_deactivate(ti, c.seat);
}

static void text_input_invalid_utf8(void) {
  struct win *w = toplevel(200, 200);
  struct zwp_text_input_v1 *ti =
      zwp_text_input_manager_v1_create_text_input(c.text_input);
  zwp_text_input_v1_activate(ti, c.seat, w->surface);
  wl_display_roundtrip(c.display);
  const char *bad[] = {"\xff\xfe\xfd", "a\xc3", "\xe2\x82", "\xf0\x9f\x98",
                       "\xed\xa0\x80", "\xc0\xaf", "\xf4\x90\x80\x80"};
  for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
    zwp_text_input_v1_set_surrounding_text(ti, bad[i], 1, 2);
    zwp_text_input_v1_set_preferred_language(ti, bad[i]);
    zwp_text_input_v1_commit_state(ti, i);
    wl_display_roundtrip(c.display);
  }
}

static void text_input_surface_destroyed(void) {
  struct win *w = toplevel(200, 200);
  struct zwp_text_input_v1 *ti =
      zwp_text_input_manager_v1_create_text_input(c.text_input);
  zwp_text_input_v1_activate(ti, c.seat, w->surface);
  zwp_text_input_v1_show_input_panel(ti);
  wl_display_roundtrip(c.display);
  xdg_toplevel_destroy(w->toplevel);
  xdg_surface_destroy(w->xdg_surface);
  wl_surface_destroy(w->surface);
  zwp_text_input_v1_set_surrounding_text(ti, "after", 0, 0);
  zwp_text_input_v1_commit_state(ti, 1);
  wl_display_roundtrip(c.display);
  struct win *w2 = toplevel(100, 100);
  zwp_text_input_v1_activate(ti, c.seat, w2->surface);
  zwp_text_input_v1_activate(ti, c.seat, w2->surface);
  wl_seat_release(c.seat);
  zwp_text_input_v1_deactivate(ti, c.seat);
}

/* ---- scenarios: data device ---------------------------------------------- */

static void offer_selection(void *data, struct wl_data_device *dd,
                            struct wl_data_offer *offer) {
  c.selection = offer;
}
static void dd_data_offer(void *data, struct wl_data_device *dd,
                          struct wl_data_offer *offer) {}
static void dd_enter(void *data, struct wl_data_device *dd, uint32_t serial,
                     struct wl_surface *s, wl_fixed_t x, wl_fixed_t y,
                     struct wl_data_offer *offer) {}
static void dd_leave(void *data, struct wl_data_device *dd) {}
static void dd_motion(void *data, struct wl_data_device *dd, uint32_t time,
                      wl_fixed_t x, wl_fixed_t y) {}
static void dd_drop(void *data, struct wl_data_device *dd) {}
static const struct wl_data_device_listener data_device_listener = {
    dd_data_offer, dd_enter, dd_leave, dd_motion, dd_drop, offer_selection};

static void source_target(void *d, struct wl_data_source *s, const char *m) {}
static void source_send(void *d, struct wl_data_source *s, const char *m,
                        int fd) {
  /* Answers with garbage of an odd length, then closes. */
  char junk[777];
  memset(junk, 0xfe, sizeof(junk));
  if (write(fd, junk, sizeof(junk)) < 0) {
  }
  close(fd);
}
static void source_cancelled(void *d, struct wl_data_source *s) {}
static void source_dnd_drop_performed(void *d, struct wl_data_source *s) {}
static void source_dnd_finished(void *d, struct wl_data_source *s) {}
static void source_action(void *d, struct wl_data_source *s, uint32_t a) {}
static const struct wl_data_source_listener source_listener = {
    source_target,          source_send,       source_cancelled,
    source_dnd_drop_performed, source_dnd_finished, source_action};

static struct wl_data_device *data_device(void) {
  struct wl_data_device *dd =
      wl_data_device_manager_get_data_device(c.ddm, c.seat);
  wl_data_device_add_listener(dd, &data_device_listener, NULL);
  return dd;
}

static struct wl_data_source *source(int types) {
  struct wl_data_source *s = wl_data_device_manager_create_data_source(c.ddm);
  wl_data_source_add_listener(s, &source_listener, NULL);
  wl_data_source_offer(s, "text/plain;charset=utf-8");
  for (int i = 0; i < types; i++) {
    char mime[64];
    snprintf(mime, sizeof(mime), "application/x-wl-stress-%d", i);
    wl_data_source_offer(s, mime);
    pace();
  }
  return s;
}

/* Our own selection comes back as an offer: then odd actions on it. */
static void data_offer_bad_actions(void) {
  struct win *w = toplevel(200, 200);
  struct wl_data_device *dd = data_device();
  wl_data_device_set_selection(dd, source(0), c.keyboard_serial);
  wl_display_roundtrip(c.display);
  wl_display_roundtrip(c.display);
  if (!c.selection) {
    printf("skipped (no selection offer)\n");
    exit(0);
  }
  wl_data_offer_set_actions(c.selection, 0xff, 0x6);
  (void)w;
}

static void data_offer_receive_odd(void) {
  struct win *w = toplevel(200, 200);
  struct wl_data_device *dd = data_device();
  wl_data_device_set_selection(dd, source(0), c.keyboard_serial);
  wl_display_roundtrip(c.display);
  wl_display_roundtrip(c.display);
  if (!c.selection) {
    printf("skipped (no selection offer)\n");
    exit(0);
  }
  int p[2];
  for (int i = 0; i < 50; i++) {
    if (pipe(p) < 0)
      break;
    close(p[0]); /* nobody reads */
    wl_data_offer_receive(c.selection,
                          i % 2 ? "text/plain;charset=utf-8" : "nope/none",
                          p[1]);
    close(p[1]);
  }
  wl_data_offer_accept(c.selection, 0xdeadbeef, "nope/none");
  wl_data_offer_finish(c.selection);
  wl_data_offer_destroy(c.selection);
  (void)w;
}

static void data_source_many_types(void) {
  struct win *w = toplevel(200, 200);
  struct wl_data_device *dd = data_device();
  for (int i = 0; i < 20; i++)
    wl_data_device_set_selection(dd, source(5000), c.keyboard_serial);
  wl_data_device_set_selection(dd, NULL, c.keyboard_serial);
  (void)w;
}

static void data_source_reused(void) {
  struct win *w = toplevel(200, 200);
  struct wl_data_device *dd = data_device();
  struct wl_data_source *s = source(1);
  wl_data_device_set_selection(dd, s, c.keyboard_serial);
  wl_data_device_set_selection(dd, s, c.keyboard_serial);
  wl_data_source_destroy(s);
  wl_data_device_set_selection(dd, source(1), 0);
  (void)w;
}

static void drag_without_source(void) {
  struct win *w = toplevel(200, 200);
  struct wl_data_device *dd = data_device();
  wl_data_device_start_drag(dd, NULL, w->surface, NULL, 12345);
  wl_data_device_start_drag(dd, NULL, w->surface, NULL, c.pointer_serial);
  wl_data_device_start_drag(dd, NULL, w->surface, NULL, c.keyboard_serial);
}

static void drag_odd(void) {
  struct win *w = toplevel(200, 200);
  struct wl_data_device *dd = data_device();
  struct wl_data_source *s = source(3);
  wl_data_source_set_actions(s, 0xffffffff);
  struct wl_surface *icon = wl_compositor_create_surface(c.compositor);
  wl_surface_attach(icon, shm_buffer(4096, 4096, NULL), INT32_MAX, 0);
  wl_surface_commit(icon);
  wl_data_device_start_drag(dd, s, w->surface, icon, c.pointer_serial);
  wl_data_device_start_drag(dd, s, w->surface, icon, c.keyboard_serial);
  wl_data_device_start_drag(dd, source(1), w->surface, w->surface, 7);
}

static void drag_origin_gone(void) {
  struct win *w = toplevel(200, 200);
  struct wl_data_device *dd = data_device();
  wl_data_device_start_drag(dd, source(2), w->surface, NULL,
                            c.pointer_serial);
  xdg_toplevel_destroy(w->toplevel);
  xdg_surface_destroy(w->xdg_surface);
  wl_surface_destroy(w->surface);
  wl_display_roundtrip(c.display);
  wl_data_device_release(dd);
}

/* ---- scenarios: linux-dmabuf --------------------------------------------- */

static void params_created(void *d, struct zwp_linux_buffer_params_v1 *p,
                           struct wl_buffer *b) {}
static void params_failed(void *d, struct zwp_linux_buffer_params_v1 *p) {}
static const struct zwp_linux_buffer_params_v1_listener params_listener = {
    params_created, params_failed};

/* A memfd posing as a dmabuf. */
static void dmabuf_not_a_dmabuf(void) {
  struct win *w = toplevel(64, 64);
  struct zwp_linux_buffer_params_v1 *p =
      zwp_linux_dmabuf_v1_create_params(c.dmabuf);
  zwp_linux_buffer_params_v1_add(p, shm_file(64 * 64 * 4), 0, 0, 256, 0, 0);
  struct wl_buffer *b = zwp_linux_buffer_params_v1_create_immed(
      p, 64, 64, 0x34325241 /* AR24 */, 0);
  wl_surface_attach(w->surface, b, 0, 0);
  wl_surface_damage(w->surface, 0, 0, 64, 64);
  wl_surface_commit(w->surface);
}

static void dmabuf_async_not_a_dmabuf(void) {
  struct zwp_linux_buffer_params_v1 *p =
      zwp_linux_dmabuf_v1_create_params(c.dmabuf);
  zwp_linux_buffer_params_v1_add_listener(p, &params_listener, NULL);
  zwp_linux_buffer_params_v1_add(p, shm_file(64 * 64 * 4), 0, 0, 256, 0, 0);
  zwp_linux_buffer_params_v1_create(p, 64, 64, 0x34325241, 0);
}

static void dmabuf_bad_params(void) {
  struct zwp_linux_buffer_params_v1 *p =
      zwp_linux_dmabuf_v1_create_params(c.dmabuf);
  zwp_linux_buffer_params_v1_add(p, shm_file(4096), 3, 0xffffffff,
                                 0xffffffff, 0xffffffff, 0xffffffff);
  zwp_linux_buffer_params_v1_create_immed(p, INT32_MAX, INT32_MAX,
                                          0xdeadbeef, 0xffffffff);
}

static void dmabuf_plane_twice(void) {
  struct zwp_linux_buffer_params_v1 *p =
      zwp_linux_dmabuf_v1_create_params(c.dmabuf);
  zwp_linux_buffer_params_v1_add(p, shm_file(4096), 0, 0, 256, 0, 0);
  zwp_linux_buffer_params_v1_add(p, shm_file(4096), 0, 0, 256, 0, 0);
}

static void dmabuf_huge(void) {
  struct win *w = toplevel(64, 64);
  struct zwp_linux_buffer_params_v1 *p =
      zwp_linux_dmabuf_v1_create_params(c.dmabuf);
  zwp_linux_buffer_params_v1_add(p, shm_file(4096), 0, 0, 0x7ffffffc, 0, 0);
  struct wl_buffer *b = zwp_linux_buffer_params_v1_create_immed(
      p, 0x7fffffff / 4, 0x7fff, 0x34325241, 0);
  wl_surface_attach(w->surface, b, 0, 0);
  wl_surface_commit(w->surface);
}

static void dmabuf_params_reused(void) {
  struct zwp_linux_buffer_params_v1 *p =
      zwp_linux_dmabuf_v1_create_params(c.dmabuf);
  zwp_linux_buffer_params_v1_add(p, shm_file(64 * 64 * 4), 0, 0, 256, 0, 0);
  zwp_linux_buffer_params_v1_create_immed(p, 64, 64, 0x34325241, 0);
  zwp_linux_buffer_params_v1_create_immed(p, 64, 64, 0x34325241, 0);
}

/* ---- the list ------------------------------------------------------------ */

struct scenario {
  const char *name;
  void (*run)(void);
};

#define S(f) {#f, f}
static const struct scenario scenarios[] = {
    S(shm_pool_bigger_than_file),
    S(shm_pool_zero_size),
    S(shm_pool_shrink),
    S(shm_pool_grow_past_file),
    S(shm_buffer_offset_past_end),
    S(shm_buffer_huge_dimensions),
    S(shm_buffer_negative_values),
    S(shm_buffer_zero_stride),
    S(shm_buffer_bad_format),
    S(shm_pool_destroyed_before_buffer),
    S(surface_scale_huge),
    S(cursor_scale_huge),
    S(cursor_scale_64),
    S(cursor_big_buffer),
    S(cursor_surface_with_role),
    S(cursor_surface_destroyed),
    S(surface_transform_invalid),
    S(surface_transform_all_odd_size),
    S(surface_damage_extreme),
    S(surface_attach_offset_extreme),
    S(surface_frame_flood),
    S(surface_commit_before_configure),
    S(surface_destroyed_before_role),
    S(region_many_rectangles),
    S(subsurface_deep_nesting),
    S(subsurface_cycle),
    S(subsurface_of_itself),
    S(subsurface_place_non_sibling),
    S(subsurface_parent_destroyed),
    S(subsurface_sync_storm),
    S(subsurface_of_popup_and_cursor),
    S(xdg_toplevel_twice),
    S(xdg_geometry_extreme),
    S(xdg_min_max_size),
    S(xdg_parent_cycle),
    S(xdg_parent_self),
    S(xdg_parent_destroyed),
    S(xdg_state_storm),
    S(xdg_move_resize_menu_bogus),
    S(xdg_ack_bogus_serial),
    S(xdg_destroy_out_of_order),
    S(xdg_wm_base_destroyed_with_surfaces),
    S(xdg_popup_null_parent),
    S(xdg_popup_chain),
    S(xdg_popup_parent_destroyed),
    S(xdg_popup_destroyed_out_of_order),
    S(xdg_positioner_extreme),
    S(xdg_positioner_incomplete),
    S(xdg_popup_of_unmapped_parent),
    S(wl_shell_odd),
    S(decoration_odd),
    S(decoration_twice),
    S(decoration_after_toplevel),
    S(viewport_odd_values),
    S(viewport_source_outside_buffer),
    S(viewport_bad_values),
    S(viewport_after_surface),
    S(viewport_twice),
    S(single_pixel_scaled),
    S(presentation_flood),
    S(fractional_scale_odd),
    S(content_type_odd),
    S(content_type_twice),
    S(activation_odd),
    S(idle_inhibit_odd),
    S(shortcuts_inhibit_odd),
    S(xdg_output_odd),
    S(seat_objects_odd),
    S(cursor_shape_all),
    S(cursor_shape_invalid),
    S(cursor_shape_pointer_gone),
    S(pointer_lock_odd),
    S(pointer_lock_twice),
    S(pointer_lock_unmapped),
    S(text_input_odd),
    S(text_input_invalid_utf8),
    S(text_input_surface_destroyed),
    S(data_offer_bad_actions),
    S(data_offer_receive_odd),
    S(data_source_many_types),
    S(data_source_reused),
    S(drag_without_source),
    S(drag_odd),
    S(drag_origin_gone),
    S(dmabuf_not_a_dmabuf),
    S(dmabuf_async_not_a_dmabuf),
    S(dmabuf_bad_params),
    S(dmabuf_plane_twice),
    S(dmabuf_huge),
    S(dmabuf_params_reused),
    /* Last: these may take the GPU process down (see exo-stress-test.sh). */
    S(shm_file_truncated_while_shown),
    S(shm_file_truncated_cursor),
    S(unsealable_file_truncated_while_shown),
    S(unsealable_file_truncated_cursor),
};
#undef S

int main(int argc, char **argv) {
  const size_t n = sizeof(scenarios) / sizeof(scenarios[0]);
  if (argc == 2 && strcmp(argv[1], "--list") == 0) {
    for (size_t i = 0; i < n; i++)
      printf("%s\n", scenarios[i].name);
    return 0;
  }
  if (argc != 2) {
    fprintf(stderr, "usage: wl-stress --list | NAME\n");
    return 2;
  }
  for (size_t i = 0; i < n; i++) {
    if (strcmp(argv[1], scenarios[i].name) == 0) {
      connect_display();
      scenarios[i].run();
      return finish();
    }
  }
  fprintf(stderr, "no scenario %s\n", argv[1]);
  return 2;
}
