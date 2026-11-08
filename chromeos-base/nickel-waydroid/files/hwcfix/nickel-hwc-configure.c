/* Distributed under the terms of the GNU General Public License v2
 *
 * NickelOS: preloaded into Waydroid's composer service with
 * nickel-hwc-roundtrip.c. Clicking another window doesn't restart Android's
 * display any more.
 *
 * Each Android window is an xdg_toplevel the size of Android's whole display
 * (transparent, the app drawn inside it), and the hwcomposer's configure
 * handler takes every configure's size as the display's and hotplugs the
 * display, whether the size changed or not. The compositor configures a
 * window whenever it gains or loses focus, so clicking the window behind an
 * Android app, or the Terminal next to it, hotplugged Android's display once
 * per Android window, several times in a row: windows were made again and
 * jumped back in front (flashing, fighting for focus), and SurfaceFlinger
 * could crash on a display that had just gone ("Invalid display").
 *
 * The hwcomposer adds its listener through wl_proxy_add_listener, reached
 * through its PLT. For an xdg_toplevel, the listener given instead passes a
 * configure on only when its size isn't the one passed on last (sizes of 0
 * or 1, "your choice", always go through, as the hwcomposer ignores them).
 * Close goes straight through.
 *
 * (zwp_linux_dmabuf_v1's listener is swapped here too, for
 * nickel-hwc-opaque.c, which notes the formats the compositor takes.) */
typedef unsigned int uint32_t;
typedef int int32_t;

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

struct wl_proxy;
struct wl_interface {
  const char *name;
  int version;
  int method_count;
  const void *methods;
  int event_count;
  const void *events;
};

typedef void (*configure_fn)(void *data, struct wl_proxy *toplevel,
                             int32_t width, int32_t height, void *states);
typedef void (*close_fn)(void *data, struct wl_proxy *toplevel);
struct toplevel_listener {
  configure_fn configure;
  close_fn close;
};

static int (*real_add_listener)(struct wl_proxy *, void (**)(void), void *);
static const struct toplevel_listener *original;
extern __attribute__((visibility("hidden"))) const void *
nickel_hwc_dmabuf_listener(const void *listener);
static int32_t last_width, last_height;

static void configure(void *data, struct wl_proxy *toplevel, int32_t width,
                      int32_t height, void *states) {
  if (width > 1 && height > 1) {
    if (width == last_width && height == last_height)
      return;
    last_width = width;
    last_height = height;
  }
  original->configure(data, toplevel, width, height, states);
}

static void close_window(void *data, struct wl_proxy *toplevel) {
  original->close(data, toplevel);
}

static const struct toplevel_listener filtered = {configure, close_window};

static int streq(const char *a, const char *b) {
  while (*a && *a == *b)
    a++, b++;
  return *a == *b;
}

int wl_proxy_add_listener(struct wl_proxy *proxy, void (**implementation)(void),
                          void *data) {
  if (!real_add_listener) {
    /* The caller's own libwayland (the hwcomposer links it in). */
    Dl_info info;
    void *h = 0;
    if (dladdr(__builtin_return_address(0), &info) && info.dli_fname)
      h = dlopen(info.dli_fname, RTLD_NOW | RTLD_NOLOAD);
    if (h)
      real_add_listener = dlsym(h, "wl_proxy_add_listener");
    if (!real_add_listener || real_add_listener == wl_proxy_add_listener) {
      real_add_listener = 0;
      return -1;
    }
  }
  const struct wl_interface *interface =
      proxy ? *(const struct wl_interface *const *)proxy : 0;
  if (interface && interface->name && streq(interface->name, "xdg_toplevel") &&
      implementation) {
    const struct toplevel_listener *listener =
        (const struct toplevel_listener *)implementation;
    if (!original)
      original = listener;
    if (listener == original && listener->configure && listener->close)
      implementation = (void (**)(void))&filtered;
  } else if (interface && interface->name &&
             streq(interface->name, "zwp_linux_dmabuf_v1") && implementation) {
    implementation =
        (void (**)(void))nickel_hwc_dmabuf_listener(implementation);
  }
  return real_add_listener(proxy, implementation, data);
}
