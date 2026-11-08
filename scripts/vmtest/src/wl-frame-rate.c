/* NickelOS: how often the compositor (Exo) shows frames, independent of the
 * GPU: a Wayland window drawn on the CPU (wl_shm) that draws its next frame
 * each time Exo's frame callback says the last one was shown. With nothing
 * else going on that is the display's refresh rate; while another app keeps
 * the GPU busy, it shows whether Chrome's compositing keeps its pace
 * (hw/x-sync-test.sh: Exo must not wait for other apps' frames).
 *   wl-frame-rate SECONDS     prints "fps=N"
 * Build: protocol code from wayland-scanner (xdg-shell), as wl-ime-test. */
#define _GNU_SOURCE
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#include <wayland-client.h>

#include "xdg-shell-client.h"

enum { kWidth = 320, kHeight = 200 };

static struct wl_compositor* compositor;
static struct wl_shm* shm;
static struct xdg_wm_base* wm_base;
static struct wl_surface* surface;
static struct wl_buffer* buffers[2];
static void* pixels[2];
static int configured, frames, next;

static double now(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return ts.tv_sec + ts.tv_nsec / 1e9;
}

static void make_buffers(void) {
  int stride = kWidth * 4, size = stride * kHeight;
  int fd = memfd_create("wl-frame-rate", MFD_CLOEXEC);
  if (fd < 0 || ftruncate(fd, size * 2) < 0)
    exit(1);
  char* data = mmap(NULL, size * 2, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  struct wl_shm_pool* pool = wl_shm_create_pool(shm, fd, size * 2);
  for (int i = 0; i < 2; i++) {
    buffers[i] = wl_shm_pool_create_buffer(pool, i * size, kWidth, kHeight,
                                           stride, WL_SHM_FORMAT_XRGB8888);
    pixels[i] = data + i * size;
  }
  wl_shm_pool_destroy(pool);
  close(fd);
}

static void draw(void);

static void frame_done(void* data, struct wl_callback* callback,
                       uint32_t time) {
  wl_callback_destroy(callback);
  frames++;
  draw();
}
static const struct wl_callback_listener frame_listener = {frame_done};

/* A new picture each frame (a shade that changes), so Exo has something to
 * show. */
static void draw(void) {
  memset(pixels[next], (frames * 7) & 0xff, kWidth * kHeight * 4);
  wl_surface_attach(surface, buffers[next], 0, 0);
  wl_surface_damage(surface, 0, 0, kWidth, kHeight);
  wl_callback_add_listener(wl_surface_frame(surface), &frame_listener, NULL);
  wl_surface_commit(surface);
  next ^= 1;
}

static void wm_base_ping(void* data, struct xdg_wm_base* base,
                         uint32_t serial) {
  xdg_wm_base_pong(base, serial);
}
static const struct xdg_wm_base_listener wm_base_listener = {wm_base_ping};

static void xdg_surface_configure(void* data, struct xdg_surface* xs,
                                  uint32_t serial) {
  xdg_surface_ack_configure(xs, serial);
  if (!configured) {
    configured = 1;
    draw();
  } else {
    wl_surface_commit(surface);
  }
}
static const struct xdg_surface_listener xdg_surface_listener = {
    xdg_surface_configure};

static void toplevel_configure(void* data, struct xdg_toplevel* t, int32_t w,
                               int32_t h, struct wl_array* states) {}
static void toplevel_close(void* data, struct xdg_toplevel* t) {}
static const struct xdg_toplevel_listener toplevel_listener = {
    toplevel_configure, toplevel_close};

static void global(void* data, struct wl_registry* reg, uint32_t name,
                   const char* iface, uint32_t version) {
  if (!strcmp(iface, "wl_compositor"))
    compositor = wl_registry_bind(reg, name, &wl_compositor_interface, 3);
  else if (!strcmp(iface, "wl_shm"))
    shm = wl_registry_bind(reg, name, &wl_shm_interface, 1);
  else if (!strcmp(iface, "xdg_wm_base"))
    wm_base = wl_registry_bind(reg, name, &xdg_wm_base_interface, 1);
}
static void global_remove(void* data, struct wl_registry* reg,
                          uint32_t name) {}
static const struct wl_registry_listener registry_listener = {global,
                                                              global_remove};

int main(int argc, char** argv) {
  double seconds = argc > 1 ? atof(argv[1]) : 5;
  struct wl_display* display = wl_display_connect(NULL);
  if (!display) {
    printf("no display\n");
    return 1;
  }
  struct wl_registry* registry = wl_display_get_registry(display);
  wl_registry_add_listener(registry, &registry_listener, NULL);
  wl_display_roundtrip(display);
  if (!compositor || !shm || !wm_base) {
    printf("missing core globals\n");
    return 1;
  }
  make_buffers();
  xdg_wm_base_add_listener(wm_base, &wm_base_listener, NULL);
  surface = wl_compositor_create_surface(compositor);
  struct xdg_surface* xs = xdg_wm_base_get_xdg_surface(wm_base, surface);
  xdg_surface_add_listener(xs, &xdg_surface_listener, NULL);
  struct xdg_toplevel* toplevel = xdg_surface_get_toplevel(xs);
  xdg_toplevel_add_listener(toplevel, &toplevel_listener, NULL);
  xdg_toplevel_set_title(toplevel, "wl-frame-rate");
  wl_surface_commit(surface);

  /* Count from the first frame shown on. */
  while (!frames && wl_display_dispatch(display) >= 0) {
  }
  int start_frames = frames;
  double start = now(), end = start + seconds;
  while (now() < end && wl_display_dispatch(display) >= 0) {
  }
  printf("fps=%.0f\n", (frames - start_frames) / (now() - start));
  wl_display_disconnect(display);
  return 0;
}
