/* Distributed under the terms of the GNU General Public License v2
 *
 * NickelOS: preloaded into Waydroid's composer service with
 * nickel-hwc-roundtrip.c. Opaque Android layers stay opaque on the desktop.
 *
 * SurfaceFlinger draws a layer it knows to be opaque (blending NONE: most
 * app windows, and games' SurfaceViews) without blending, so whatever alpha
 * its buffer holds doesn't matter, and games often leave it below 1 (the
 * libGDX ones, Shattered Pixel Dungeon and Mindustry, in much of the
 * screen). The hwcomposer gives the compositor every buffer as the format
 * Android allocated, alpha included, and sets no opaque region, so the
 * desktop compositor blended those games with whatever was behind their
 * window: the wallpaper and other windows showed through them.
 *
 * create_dmabuf_wl_buffer(), which makes the wl_buffer for a dma-buf, is
 * reached through the hwcomposer's PLT and is given the buffer's handle.
 * When that's an opaque layer's this frame (nickel-hwc-buffers.c's set()
 * wrapper notes them), an 8-bit or 10-bit RGBA format is given as its
 * alpha-ignoring twin (ABGR8888 as XBGR8888, same layout), when the
 * compositor said it takes that with the buffer's modifier: the formats
 * and modifiers it lists (zwp_linux_dmabuf_v1's events, whose listener
 * nickel-hwc-configure.c's wl_proxy_add_listener hands here) are noted. */
typedef unsigned int uint32_t;
typedef int int32_t;
typedef unsigned long uint64_t;
typedef __SIZE_TYPE__ size_t;

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

#define FOURCC(a, b, c, d)                                                     \
  ((uint32_t)(a) | ((uint32_t)(b) << 8) | ((uint32_t)(c) << 16) |              \
   ((uint32_t)(d) << 24))
#define DRM_FORMAT_ABGR8888 FOURCC('A', 'B', '2', '4')
#define DRM_FORMAT_XBGR8888 FOURCC('X', 'B', '2', '4')
#define DRM_FORMAT_ARGB8888 FOURCC('A', 'R', '2', '4')
#define DRM_FORMAT_XRGB8888 FOURCC('X', 'R', '2', '4')
#define DRM_FORMAT_ABGR2101010 FOURCC('A', 'B', '3', '0')
#define DRM_FORMAT_XBGR2101010 FOURCC('X', 'B', '3', '0')
#define DRM_FORMAT_ARGB2101010 FOURCC('A', 'R', '3', '0')
#define DRM_FORMAT_XRGB2101010 FOURCC('X', 'R', '3', '0')
#define DRM_FORMAT_MOD_INVALID 0x00ffffffffffffffUL
/* system/graphics.h */
#define HAL_PIXEL_FORMAT_RGBA_8888 1
#define HAL_PIXEL_FORMAT_BGRA_8888 5

/* The hwcomposer's buffer_metadata: height, width, pixel stride, format. */
struct buffer_metadata {
  uint32_t height;
  uint32_t width;
  uint32_t pixel_stride;
  uint32_t format;
};

extern __attribute__((visibility("hidden"))) int
nickel_hwc_opaque_buffer(const void *handle);

/* The formats and modifiers the compositor takes (a format announced
 * without modifiers counts as taking DRM_FORMAT_MOD_INVALID). */
#define MAX_PAIRS 1024
static struct {
  uint32_t format;
  uint64_t modifier;
} pairs[MAX_PAIRS];
static int npairs;

static void note(uint32_t format, uint64_t modifier) {
  int n = __atomic_load_n(&npairs, __ATOMIC_ACQUIRE);
  for (int i = 0; i < n; i++)
    if (pairs[i].format == format && pairs[i].modifier == modifier)
      return;
  if (n == MAX_PAIRS)
    return;
  pairs[n].format = format;
  pairs[n].modifier = modifier;
  __atomic_store_n(&npairs, n + 1, __ATOMIC_RELEASE);
}

static int takes(uint32_t format, uint64_t modifier) {
  int n = __atomic_load_n(&npairs, __ATOMIC_ACQUIRE);
  for (int i = 0; i < n; i++)
    if (pairs[i].format == format && pairs[i].modifier == modifier)
      return 1;
  return 0;
}

struct wl_proxy;
struct dmabuf_listener {
  void (*format)(void *data, struct wl_proxy *dmabuf, uint32_t format);
  void (*modifier)(void *data, struct wl_proxy *dmabuf, uint32_t format,
                   uint32_t modifier_hi, uint32_t modifier_lo);
};
static const struct dmabuf_listener *original;

static void on_format(void *data, struct wl_proxy *dmabuf, uint32_t format) {
  note(format, DRM_FORMAT_MOD_INVALID);
  if (original->format)
    original->format(data, dmabuf, format);
}

static void on_modifier(void *data, struct wl_proxy *dmabuf, uint32_t format,
                        uint32_t modifier_hi, uint32_t modifier_lo) {
  note(format, (uint64_t)modifier_hi << 32 | modifier_lo);
  if (original->modifier)
    original->modifier(data, dmabuf, format, modifier_hi, modifier_lo);
}

static const struct dmabuf_listener noting = {on_format, on_modifier};

/* The listener to give a zwp_linux_dmabuf_v1 instead of the hwcomposer's. */
__attribute__((visibility("hidden"))) const void *
nickel_hwc_dmabuf_listener(const void *listener) {
  if (!original)
    original = listener;
  return listener == original ? &noting : listener;
}

static uint32_t without_alpha(uint32_t format) {
  switch (format) {
  case DRM_FORMAT_ABGR8888:
    return DRM_FORMAT_XBGR8888;
  case DRM_FORMAT_ARGB8888:
    return DRM_FORMAT_XRGB8888;
  case DRM_FORMAT_ABGR2101010:
    return DRM_FORMAT_XBGR2101010;
  case DRM_FORMAT_ARGB2101010:
    return DRM_FORMAT_XRGB2101010;
  }
  return format;
}

/* The format the hwcomposer takes a HAL format as when it's given none
 * (of the ones it knows, those with alpha). */
static uint32_t from_hal(uint32_t hal_format) {
  switch (hal_format) {
  case HAL_PIXEL_FORMAT_RGBA_8888:
    return DRM_FORMAT_ABGR8888;
  case HAL_PIXEL_FORMAT_BGRA_8888:
    return DRM_FORMAT_ARGB8888;
  }
  return 0;
}

#define CREATE                                                                 \
  "_Z23create_dmabuf_wl_bufferP7displayRK15buffer_metadataiiiimPK13native_h" \
  "andle"

/* Returns std::unique_ptr<buffer> through ret. */
typedef void *(*create_fn)(void *ret, void *display,
                           const struct buffer_metadata *metadata,
                           int prime_fd, int drm_format, int byte_stride,
                           int offset, uint64_t modifier, const void *handle);
static create_fn real_create;

void *_Z23create_dmabuf_wl_bufferP7displayRK15buffer_metadataiiiimPK13native_handle(
    void *ret, void *display, const struct buffer_metadata *metadata,
    int prime_fd, int drm_format, int byte_stride, int offset,
    uint64_t modifier, const void *handle) {
  if (!real_create) {
    Dl_info info;
    void *h = 0;
    if (dladdr(__builtin_return_address(0), &info) && info.dli_fname)
      h = dlopen(info.dli_fname, RTLD_NOW | RTLD_NOLOAD);
    create_fn f = h ? (create_fn)dlsym(h, CREATE) : 0;
    if (f ==
        (create_fn)
            _Z23create_dmabuf_wl_bufferP7displayRK15buffer_metadataiiiimPK13native_handle)
      f = 0;
    real_create = f;
  }
  if (!real_create) {
    *(void **)ret = 0; /* no buffer; the hwcomposer skips the layer */
    return ret;
  }
  if (handle && metadata && nickel_hwc_opaque_buffer(handle)) {
    uint32_t format =
        drm_format >= 0 ? (uint32_t)drm_format : from_hal(metadata->format);
    uint32_t opaque = without_alpha(format);
    if (format && opaque != format && takes(opaque, modifier))
      drm_format = (int)opaque;
  }
  return real_create(ret, display, metadata, prime_fd, drm_format, byte_stride,
                     offset, modifier, handle);
}
