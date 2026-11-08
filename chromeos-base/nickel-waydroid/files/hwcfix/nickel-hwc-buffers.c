/* Distributed under the terms of the GNU General Public License v2
 *
 * NickelOS: preloaded into Waydroid's composer service with
 * nickel-hwc-roundtrip.c. Keeps the hwcomposer's wl_buffers from being made
 * again every frame.
 *
 * Waydroid's hwcomposer keeps a wl_buffer for each Android buffer it has
 * shown, keyed by the buffer's handle, and empties that cache whenever a
 * frame comes with HWC_GEOMETRY_CHANGED: a handle can be reused for a new
 * buffer once the old one is freed, and a geometry change is when that
 * tends to happen. But Android's HWC1 adapter flags a geometry change
 * whenever SurfaceFlinger re-sends any layer's position, which it does on
 * every frame while the mouse pointer moves (the pointer is a layer) and on
 * most frames of a scroll. So every buffer was made again each frame, and
 * Chrome imported each one again and made a new scanout framebuffer for it
 * (about 180 a second while the pointer moved over an Android window): the
 * choppy scrolling, and the pointer lagging behind the mouse.
 *
 * This wraps the HWC1 device's set() and takes the flag out when nothing
 * but the pointer and the buffers' contents changed. The flag also tells the
 * hwcomposer to empty the subsurfaces a window no longer uses, so it stays
 * whenever the layers (other than the pointer) differ from the last frame's
 * in number, place or kind. The handles shown since the cache was last
 * emptied are tracked with the inode of the buffer's dma-buf, which a new
 * buffer can't share: a handle that comes with a different buffer empties
 * the cache, flag or not. The flag also stays (and the tracking starts over)
 * when a buffer can't be told apart, and when one hasn't been seen for 3 s
 * (it's likely freed, and emptying the cache lets Chrome free it too). The
 * wrapper goes in through hw_get_module(), which the composer service calls
 * to load Waydroid's HAL.
 *
 * It also notes which of the frame's buffers are opaque layers' (for
 * nickel-hwc-opaque.c, which gives those the compositor without alpha), and
 * a buffer whose layer turns opaque or stops being opaque empties the cache
 * too, so that it's made again the way its layer now is. */
typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
typedef int int32_t;
typedef unsigned long uint64_t;
typedef long int64_t;
typedef __SIZE_TYPE__ size_t;

extern void *dlopen(const char *filename, int flags);
extern void *dlsym(void *handle, const char *symbol);
#define RTLD_NOW 2
#define RTLD_NOLOAD 4
#define RTLD_NEXT ((void *)-1L)

struct timespec {
  long tv_sec;
  long tv_nsec;
};
extern int clock_gettime(int clock, struct timespec *ts);
extern int fstat(int fd, void *st);
#define CLOCK_MONOTONIC 1

/* hardware/hardware.h and hardware/hwcomposer.h, 64-bit layout. */
struct hw_module;
struct hw_device {
  uint32_t tag;
  uint32_t version;
  struct hw_module *module;
  uint64_t reserved[12];
  int (*close)(struct hw_device *device);
};
struct hw_module_methods {
  int (*open)(const struct hw_module *module, const char *id,
              struct hw_device **device);
};
struct hw_module {
  uint32_t tag;
  uint16_t module_api_version;
  uint16_t hal_api_version;
  const char *id;
  const char *name;
  const char *author;
  struct hw_module_methods *methods;
  void *dso;
  uint64_t reserved[32 - 7];
};
struct native_handle {
  int version;
  int numFds;
  int numInts;
  int data[];
};
struct hwc_rect {
  int left, top, right, bottom;
};
struct hwc_region {
  size_t numRects;
  const struct hwc_rect *rects;
};
struct hwc_layer {
  int32_t compositionType;
  uint32_t hints;
  uint32_t flags;
  const struct native_handle *handle;
  uint32_t transform;
  int32_t blending;
  float sourceCropf[4];
  struct hwc_rect displayFrame;
  struct hwc_region visibleRegionScreen;
  int acquireFenceFd;
  int releaseFenceFd;
  uint8_t planeAlpha;
  uint8_t pad[3];
  struct hwc_region surfaceDamage;
  uint8_t reserved[120 - 112];
};
struct hwc_contents {
  int retireFenceFd;
  void *dpy;
  void *sur;
  uint32_t flags;
  size_t numHwLayers;
  struct hwc_layer hwLayers[];
};
struct hwc_device {
  struct hw_device common;
  int (*prepare)(struct hwc_device *dev, size_t numDisplays,
                 struct hwc_contents **displays);
  int (*set)(struct hwc_device *dev, size_t numDisplays,
             struct hwc_contents **displays);
};
_Static_assert(sizeof(struct hw_device) == 120, "hw_device_t");
_Static_assert(sizeof(struct hw_module) == 248, "hw_module_t");
_Static_assert(sizeof(struct hwc_layer) == 120, "hwc_layer_1_t");
_Static_assert(__builtin_offsetof(struct hwc_layer, surfaceDamage) == 96,
               "hwc_layer_1_t");
_Static_assert(__builtin_offsetof(struct hwc_contents, hwLayers) == 40,
               "hwc_display_contents_1_t");
#define HWC_GEOMETRY_CHANGED 1
#define HWC_IS_CURSOR_LAYER 2
#define HWC_BACKGROUND 2
#define HWC_FRAMEBUFFER_TARGET 3
#define HWC_SIDEBAND 4
#define HWC_BLENDING_NONE 0x0100

#define MAX_TRACKED 128
#define UNSEEN_NS (3L * 1000000000L)

struct tracked {
  const struct native_handle *handle;
  uint64_t ino;
  int64_t seen;
  int opaque;
};
static struct tracked tracked[MAX_TRACKED];
static int ntracked;

/* The last frame's layers, other than the pointer: kind and place. */
#define MAX_SHAPE 64
struct shape {
  int32_t type;
  uint32_t flags;
  int has_handle;
  struct hwc_rect frame;
};
static struct shape last_shape[MAX_SHAPE];
static int last_count = -1;

/* Whether this frame's layers (but the pointer) are the last frame's, in
 * the same order and places; remembers this frame's for the next. */
static int same_layers(const struct hwc_contents *c) {
  int n = 0, same = 1;
  for (size_t i = 0; i < c->numHwLayers; i++) {
    const struct hwc_layer *l = &c->hwLayers[i];
    if (l->flags & HWC_IS_CURSOR_LAYER)
      continue;
    if (n == MAX_SHAPE) {
      last_count = -1;
      return 0;
    }
    struct shape now = {l->compositionType, l->flags, l->handle != 0,
                        l->displayFrame};
    struct shape *was = &last_shape[n++];
    if (n > last_count || was->type != now.type || was->flags != now.flags ||
        was->has_handle != now.has_handle ||
        was->frame.left != now.frame.left || was->frame.top != now.frame.top ||
        was->frame.right != now.frame.right ||
        was->frame.bottom != now.frame.bottom)
      same = 0;
    *was = now;
  }
  if (n != last_count)
    same = 0;
  last_count = n;
  return same;
}

static int64_t now_ns(void) {
  struct timespec ts = {0, 0};
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return ts.tv_sec * 1000000000L + ts.tv_nsec;
}

/* The inode of the buffer's first file descriptor (its dma-buf), 0 if
 * there's none. struct stat on x86_64: st_ino is the second field. */
static uint64_t buffer_ino(const struct native_handle *h) {
  uint64_t st[18];
  if (h->numFds < 1 || fstat(h->data[0], st) != 0)
    return 0;
  return st[1];
}

static int has_buffer(const struct hwc_layer *l) {
  return l->handle && l->compositionType != HWC_BACKGROUND &&
         l->compositionType != HWC_SIDEBAND;
}

/* What SurfaceFlinger draws without blending: its buffer's alpha means
 * nothing (games often leave it below 1). */
static int is_opaque(const struct hwc_layer *l) {
  return l->blending == HWC_BLENDING_NONE &&
         !(l->flags & HWC_IS_CURSOR_LAYER) &&
         l->compositionType != HWC_FRAMEBUFFER_TARGET;
}

static struct tracked *find(const struct native_handle *h) {
  for (int i = 0; i < ntracked; i++)
    if (tracked[i].handle == h)
      return &tracked[i];
  return 0;
}

/* Tracking from scratch, with this frame's buffers: the cache is emptied. */
static void start_over(struct hwc_contents *c, int64_t now) {
  ntracked = 0;
  for (size_t i = 0; i < c->numHwLayers; i++) {
    const struct hwc_layer *l = &c->hwLayers[i];
    if (!has_buffer(l) || find(l->handle) || ntracked == MAX_TRACKED)
      continue;
    tracked[ntracked++] = (struct tracked){l->handle, buffer_ino(l->handle),
                                           now, is_opaque(l)};
  }
  c->flags |= HWC_GEOMETRY_CHANGED;
}

static void keep_cache(struct hwc_contents *c) {
  int64_t now = now_ns();
  int strip = same_layers(c);
  for (size_t i = 0; i < c->numHwLayers; i++) {
    const struct hwc_layer *l = &c->hwLayers[i];
    if (!has_buffer(l))
      continue;
    uint64_t ino = buffer_ino(l->handle);
    int opaque = is_opaque(l);
    struct tracked *t = find(l->handle);
    if (t && (t->ino != ino || t->opaque != opaque)) {
      /* A handle with a new buffer, or a buffer whose layer turned opaque
       * or stopped being so: the cache must go, geometry change or not
       * (the hwcomposer would show the old buffer, or the buffer as its
       * layer was). */
      start_over(c, now);
      return;
    }
    if (!ino)
      strip = 0; /* can't tell: the hwcomposer's own rule */
    if (!t) {
      if (ntracked == MAX_TRACKED) {
        strip = 0;
        continue;
      }
      t = &tracked[ntracked++];
    }
    *t = (struct tracked){l->handle, ino, now, opaque};
  }
  if (!(c->flags & HWC_GEOMETRY_CHANGED))
    return;
  for (int i = 0; strip && i < ntracked; i++)
    if (now - tracked[i].seen > UNSEEN_NS)
      strip = 0;
  if (strip)
    c->flags &= ~HWC_GEOMETRY_CHANGED;
  else
    start_over(c, now);
}

/* This frame's pointer buffer, for nickel-hwc-pointer.c. */
__attribute__((visibility("hidden"))) const void *nickel_hwc_pointer_handle;

/* This frame's opaque layers' buffers, for nickel-hwc-opaque.c. */
#define MAX_OPAQUE 64
static const struct native_handle *opaque_handles[MAX_OPAQUE];
static int nopaque;
__attribute__((visibility("hidden"))) int
nickel_hwc_opaque_buffer(const void *handle) {
  for (int i = 0; i < nopaque; i++)
    if (opaque_handles[i] == handle)
      return 1;
  return 0;
}

/* nickel-hwc-surfaces.c's: the frame's layer names, read during set(). */
extern __attribute__((visibility("hidden"))) const void *nickel_hwc_layer_infos;
/* nickel-hwc-client.c's: layers it changed during set(), put back. */
extern __attribute__((visibility("hidden"))) void
nickel_hwc_restore_client_layers(void);

static int (*real_set)(struct hwc_device *, size_t, struct hwc_contents **);
static int set_kept(struct hwc_device *dev, size_t n,
                    struct hwc_contents **displays) {
  nopaque = 0;
  if (n > 0 && displays && displays[0]) {
    const void *pointer = 0;
    for (size_t i = 0; i < displays[0]->numHwLayers; i++) {
      const struct hwc_layer *l = &displays[0]->hwLayers[i];
      if (l->flags & HWC_IS_CURSOR_LAYER)
        pointer = l->handle;
      else if (has_buffer(l) && is_opaque(l) && nopaque < MAX_OPAQUE)
        opaque_handles[nopaque++] = l->handle;
    }
    nickel_hwc_pointer_handle = pointer;
    keep_cache(displays[0]);
  }
  int ret = real_set(dev, n, displays);
  nickel_hwc_restore_client_layers();
  nickel_hwc_layer_infos = 0;
  nopaque = 0;
  return ret;
}

static const struct hw_module *real_module;
static struct hw_module module_copy;
static int (*real_open)(const struct hw_module *, const char *,
                        struct hw_device **);
static int open_wrapped(const struct hw_module *module, const char *id,
                        struct hw_device **device) {
  int ret = real_open(real_module, id, device);
  /* Only an HWC1 device (version 1.x) has this set(). */
  if (ret == 0 && device && *device && ((*device)->version >> 24) == 1) {
    struct hwc_device *d = (struct hwc_device *)*device;
    if (d->set && d->set != set_kept) {
      real_set = d->set;
      d->set = set_kept;
    }
  }
  return ret;
}
static struct hw_module_methods methods_wrapped = {open_wrapped};

static int streq(const char *a, const char *b) {
  while (*a && *a == *b)
    a++, b++;
  return *a == *b;
}

int hw_get_module(const char *id, const struct hw_module **module) {
  static int (*real)(const char *, const struct hw_module **);
  if (!real) {
    real = dlsym(RTLD_NEXT, "hw_get_module");
    if (!real) {
      void *h = dlopen("libhardware.so", RTLD_NOW | RTLD_NOLOAD);
      real = h ? dlsym(h, "hw_get_module") : 0;
    }
    if (!real)
      return -2; /* -ENOENT */
  }
  int ret = real(id, module);
  if (ret != 0 || !module || !*module || !id || !streq(id, "hwcomposer"))
    return ret;
  if (real_module) {
    if (*module == real_module)
      *module = &module_copy;
    return ret;
  }
  if (!(*module)->methods || !(*module)->methods->open)
    return ret;
  /* A copy of Waydroid's module whose open() wraps the device. */
  real_module = *module;
  real_open = real_module->methods->open;
  module_copy = *real_module;
  module_copy.methods = &methods_wrapped;
  *module = &module_copy;
  return ret;
}
