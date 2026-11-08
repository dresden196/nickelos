/* Distributed under the terms of the GNU General Public License v2
 *
 * NickelOS: preloaded into Waydroid's composer service with
 * nickel-hwc-roundtrip.c. Shows, in an app's window, the colour layers
 * Android draws behind the app.
 *
 * A task's background (a colour layer, no buffer) shows wherever the app's
 * window is see-through, and SurfaceFlinger draws such layers itself, into
 * its client target (the HWC1 adapter hands them over marked
 * HWC_SKIP_LAYER). In multi-window mode the hwcomposer shows each window's
 * layers on their own and never the client target, so they were missing:
 * under fullscreen apps Android's freeform caption leaves the window's
 * bottom 48 px bare (it measures the content that much short but lays it
 * out from the top), and where the window is transparent there (Mindustry,
 * Pixel Wheels) the desktop showed through instead of the task's colour.
 *
 * When split_layer_names_helper::setup() (PLT) is called from
 * multi_window_mode::setup_set(), each such layer of a task (bufferless,
 * skipped, named "TID:...") that's visible only inside one of the same
 * task's windows is given the client target's buffer, cropped to where the
 * layer is visible, as an ordinary layer: the hwcomposer puts it in the
 * task's window, under the app, with its own buffer path. (Only inside a
 * window: a task holding nothing but a dialog has its background all
 * around the dialog, which Android would show as a dark box the size of
 * the task; such tasks stay as they were, the dialog alone.) The client
 * target's fence is given to it too (a copy). The layers are put back as
 * they were once the frame is shown (nickel-hwc-buffers.c's set()
 * wrapper), for the HWC1 adapter. */
typedef unsigned char uint8_t;
typedef unsigned int uint32_t;
typedef int int32_t;
typedef __SIZE_TYPE__ size_t;

typedef struct {
  const char *dli_fname;
  void *dli_fbase;
  const char *dli_sname;
  void *dli_saddr;
} Dl_info;
extern int dladdr(const void *addr, Dl_info *info);
extern int dup(int fd);

/* hardware/hwcomposer.h, 64-bit layout. */
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
  const void *handle;
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
_Static_assert(sizeof(struct hwc_layer) == 120, "hwc_layer_1_t");
#define HWC_FRAMEBUFFER 0
#define HWC_OVERLAY 1
#define HWC_FRAMEBUFFER_TARGET 3
#define HWC_SKIP_LAYER 1
#define HWC_BLENDING_PREMULT 0x0105

/* libc++'s std::string and nickel-hwc-surfaces.c's layer_info. */
struct std_string {
  union {
    unsigned char raw[24];
    size_t words[3];
  };
};
static const char *str_data(const struct std_string *s, size_t *size) {
  if (s->raw[0] & 1) {
    *size = *(const size_t *)(s->raw + 8);
    return *(const char *const *)(s->raw + 16);
  }
  *size = s->raw[0] >> 1;
  return (const char *)s->raw + 1;
}
enum { TYPE_TID = 0 };
struct layer_info {
  int32_t type;
  struct std_string aid;
  struct std_string tid;
  struct std_string component;
};
_Static_assert(sizeof(struct layer_info) == 80, "layer_info");

static int same_task(const struct layer_info *a, const struct layer_info *b) {
  size_t na, nb;
  const char *ta = str_data(&a->tid, &na), *tb = str_data(&b->tid, &nb);
  if (a->type != TYPE_TID || b->type != TYPE_TID || na != nb)
    return 0;
  for (size_t i = 0; i < na; i++)
    if (ta[i] != tb[i])
      return 0;
  return 1;
}

static int contains(const struct hwc_rect *outer, const struct hwc_rect *r) {
  return r->left >= outer->left && r->top >= outer->top &&
         r->right <= outer->right && r->bottom <= outer->bottom;
}

#define MULTI_WINDOW_SETUP_SET "_ZN17multi_window_mode9setup_set"

/* The layers changed this frame, and what they were. */
#define MAX_SHOWN 16
static struct {
  struct hwc_layer *layer;
  struct hwc_layer was;
} shown[MAX_SHOWN];
static int nshown;

static int starts_with(const char *s, const char *prefix) {
  while (*prefix && *s == *prefix)
    s++, prefix++;
  return !*prefix;
}

/* Whether the setup() just done was multi-window mode's (called from it). */
static int from_multi_window(const void *caller) {
  static const void *known_caller;
  static int known;
  if (caller != known_caller) {
    Dl_info info;
    known = dladdr(caller, &info) && info.dli_sname &&
            starts_with(info.dli_sname, MULTI_WINDOW_SETUP_SET);
    known_caller = caller;
  }
  return known;
}

__attribute__((visibility("hidden"))) void
nickel_hwc_show_client_layers(const void *caller, const void *infos_,
                              void *layers_, size_t count) {
  const struct layer_info *infos = infos_;
  struct hwc_layer *layers = layers_;
  if (!infos || !layers || !from_multi_window(caller))
    return;
  const struct hwc_layer *target = 0;
  for (size_t i = 0; i < count; i++)
    if (layers[i].compositionType == HWC_FRAMEBUFFER_TARGET)
      target = &layers[i];
  /* The client target as the display, unscaled. */
  if (!target || !target->handle || target->displayFrame.left != 0 ||
      target->displayFrame.top != 0 || target->sourceCropf[0] != 0 ||
      target->sourceCropf[1] != 0 ||
      target->sourceCropf[2] != (float)target->displayFrame.right ||
      target->sourceCropf[3] != (float)target->displayFrame.bottom)
    return;
  for (size_t i = 0; i < count && nshown < MAX_SHOWN; i++) {
    struct hwc_layer *l = &layers[i];
    if (l->handle || l->compositionType != HWC_FRAMEBUFFER ||
        !(l->flags & HWC_SKIP_LAYER) || infos[i].type != TYPE_TID)
      continue;
    /* Where it's visible, within the display. */
    struct hwc_rect r = {0, 0, 0, 0};
    for (size_t k = 0; k < l->visibleRegionScreen.numRects; k++) {
      struct hwc_rect v = l->visibleRegionScreen.rects[k];
      if (v.left < 0)
        v.left = 0;
      if (v.top < 0)
        v.top = 0;
      if (v.right > target->displayFrame.right)
        v.right = target->displayFrame.right;
      if (v.bottom > target->displayFrame.bottom)
        v.bottom = target->displayFrame.bottom;
      if (v.right <= v.left || v.bottom <= v.top)
        continue;
      if (r.right <= r.left) {
        r = v;
        continue;
      }
      if (v.left < r.left)
        r.left = v.left;
      if (v.top < r.top)
        r.top = v.top;
      if (v.right > r.right)
        r.right = v.right;
      if (v.bottom > r.bottom)
        r.bottom = v.bottom;
    }
    if (r.right <= r.left)
      continue;
    /* Inside a window of the task's own (one the hwcomposer shows). */
    int inside = 0;
    for (size_t j = 0; j < count && !inside; j++)
      inside = j != i && layers[j].handle &&
               layers[j].handle != target->handle &&
               layers[j].compositionType == HWC_OVERLAY &&
               !(layers[j].flags & HWC_SKIP_LAYER) &&
               same_task(&infos[i], &infos[j]) &&
               contains(&layers[j].displayFrame, &r);
    if (!inside)
      continue;
    shown[nshown].layer = l;
    shown[nshown].was = *l;
    nshown++;
    l->compositionType = HWC_OVERLAY;
    l->flags &= ~HWC_SKIP_LAYER;
    l->handle = target->handle;
    l->transform = 0;
    /* What SurfaceFlinger drew there, alpha and all (a dim is see-through). */
    l->blending = HWC_BLENDING_PREMULT;
    l->sourceCropf[0] = (float)r.left;
    l->sourceCropf[1] = (float)r.top;
    l->sourceCropf[2] = (float)r.right;
    l->sourceCropf[3] = (float)r.bottom;
    l->displayFrame = r;
    l->acquireFenceFd =
        target->acquireFenceFd >= 0 ? dup(target->acquireFenceFd) : -1;
    l->releaseFenceFd = -1;
    l->planeAlpha = 255;
    l->surfaceDamage.numRects = 0; /* all of it */
    l->surfaceDamage.rects = 0;
  }
}

/* Puts the layers back as the HWC1 adapter made them. */
__attribute__((visibility("hidden"))) void nickel_hwc_restore_client_layers(void) {
  for (int i = 0; i < nshown; i++)
    *shown[i].layer = shown[i].was;
  nshown = 0;
}
