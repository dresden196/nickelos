/* Distributed under the terms of the GNU General Public License v2
 *
 * NickelOS: preloaded into Waydroid's composer service (LD_PRELOAD), replaces
 * the wl_display_roundtrip that Waydroid's hwcomposer.waydroid.so calls
 * through its PLT. Works for Waydroid's own hwcomposer (Intel, AMD) and
 * waydroid-nvidia's: both link libwayland in; its functions are found in the
 * caller's library.
 *
 * The hang: window::create, run by SurfaceFlinger's present call with the
 * HWC2On1Adapter's (recursive) state lock held, round-trips on the default
 * queue until the new window's first configure. The hwcomposer's Wayland
 * thread dispatches that queue too. libwayland's roundtrip checks its "done"
 * flag only between dispatches and waits in poll without a timeout: if the
 * Wayland thread runs the sync callback (and the configure) between the check
 * and the next poll, the caller sleeps until the compositor sends something
 * else. Nothing comes: SurfaceFlinger waits on the present, the Wayland
 * thread soon waits on the lock (do_hotplug -> hwc1Invalidate),
 * system_server waits on SurfaceFlinger, and Android's watchdog kills it.
 *
 * This keeps the roundtrip's semantics (a sync on the default queue, which
 * the caller dispatches itself, so events that need the recursive lock run
 * in the thread that holds it) but never waits more than 20 ms without
 * looking at "done" again. Only libwayland calls every version has. */
typedef unsigned int uint32_t;
typedef struct {
  const char *dli_fname;
  void *dli_fbase;
  const char *dli_sname;
  void *dli_saddr;
} Dl_info;
extern int dladdr(const void *addr, Dl_info *info);
extern void *dlopen(const char *filename, int flags);
extern void *dlsym(void *handle, const char *symbol);
#define RTLD_NOW 2
#define RTLD_NOLOAD 4

struct pollfd {
  int fd;
  short events;
  short revents;
};
extern int poll(struct pollfd *fds, unsigned long nfds, int timeout);
extern int *__errno(void);
#define POLLIN 0x1
#define EINTR 4
#define EAGAIN 11

struct wl_display;
struct wl_proxy;
struct wl_interface;
struct wl_message {
  const char *name;
  const char *signature;
  const struct wl_interface **types;
};
struct wl_interface {
  const char *name;
  int version;
  int method_count;
  const struct wl_message *methods;
  int event_count;
  const struct wl_message *events;
};
#define WL_DISPLAY_SYNC 0

static struct wl_proxy *(*marshal_constructor_versioned)(
    struct wl_proxy *, uint32_t, const struct wl_interface *, uint32_t, ...);
static int (*add_listener)(struct wl_proxy *, void (**)(void), void *);
static void (*proxy_destroy)(struct wl_proxy *);
static uint32_t (*get_version)(struct wl_proxy *);
static int (*prepare_read)(struct wl_display *);
static int (*get_fd)(struct wl_display *);
static int (*display_flush)(struct wl_display *);
static int (*read_events)(struct wl_display *);
static void (*cancel_read)(struct wl_display *);
static int (*dispatch_pending)(struct wl_display *);

static void sync_done(void *data, struct wl_proxy *callback, uint32_t serial) {
  *(volatile int *)data = 1;
  proxy_destroy(callback);
}
static void (*const sync_listener[])(void) = {(void (*)(void))sync_done};

static int resolve(const void *caller) {
  Dl_info info;
  void *h;
  if (!dladdr(caller, &info) || !info.dli_fname)
    return 0;
  h = dlopen(info.dli_fname, RTLD_NOW | RTLD_NOLOAD);
  if (!h)
    return 0;
  marshal_constructor_versioned =
      dlsym(h, "wl_proxy_marshal_constructor_versioned");
  add_listener = dlsym(h, "wl_proxy_add_listener");
  proxy_destroy = dlsym(h, "wl_proxy_destroy");
  get_version = dlsym(h, "wl_proxy_get_version");
  prepare_read = dlsym(h, "wl_display_prepare_read");
  get_fd = dlsym(h, "wl_display_get_fd");
  display_flush = dlsym(h, "wl_display_flush");
  read_events = dlsym(h, "wl_display_read_events");
  cancel_read = dlsym(h, "wl_display_cancel_read");
  dispatch_pending = dlsym(h, "wl_display_dispatch_pending");
  return marshal_constructor_versioned && add_listener && proxy_destroy &&
         get_version && prepare_read && get_fd && display_flush &&
         read_events && cancel_read && dispatch_pending;
}

int wl_display_roundtrip(struct wl_display *display) {
  volatile int done = 0;
  int ret = 0;
  if (!dispatch_pending && !resolve(__builtin_return_address(0)))
    return -1;
  /* wl_display.sync's callback interface, from the display's own interface
   * (a proxy starts with its interface pointer in every libwayland). */
  const struct wl_interface *display_interface =
      *(const struct wl_interface *const *)display;
  if (!display_interface || display_interface->method_count < 1)
    return -1;
  const struct wl_interface *callback_interface =
      display_interface->methods[WL_DISPLAY_SYNC].types[0];
  struct wl_proxy *callback = marshal_constructor_versioned(
      (struct wl_proxy *)display, WL_DISPLAY_SYNC, callback_interface,
      get_version((struct wl_proxy *)display), (void *)0);
  if (!callback)
    return -1;
  add_listener(callback, (void (**)(void))sync_listener, (void *)&done);
  while (!done) {
    if (prepare_read(display) != 0) {
      /* Events already queued: dispatch them first. */
      if (dispatch_pending(display) < 0) {
        ret = -1;
        break;
      }
      continue;
    }
    if (display_flush(display) < 0 && *__errno() != EAGAIN) {
      cancel_read(display);
      ret = -1;
      break;
    }
    struct pollfd pfd = {get_fd(display), POLLIN, 0};
    int n = poll(&pfd, 1, 20);
    if (n <= 0) {
      cancel_read(display);
      if (n < 0 && *__errno() != EINTR) {
        ret = -1;
        break;
      }
      continue; /* look at "done" again */
    }
    if (read_events(display) < 0 || dispatch_pending(display) < 0) {
      ret = -1;
      break;
    }
  }
  if (ret < 0 && !done)
    proxy_destroy(callback);
  return ret;
}
