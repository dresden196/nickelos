/* Distributed under the terms of the GNU General Public License v2
 *
 * NickelOS: preloaded into Waydroid's composer service with
 * nickel-hwc-roundtrip.c. Shows an app's SurfaceView in the app's window
 * whatever context the view was made with.
 *
 * The hwcomposer puts a layer in an app's window by its name: Waydroid's
 * Android names a window's surfaces "TID:<task>#<title>#<n>", and the
 * hwcomposer gives each task a window. A SurfaceView (where games and video
 * draw) is named the same way only when the view's context is the Activity
 * itself (Waydroid's SurfaceView asks the context for the task). SDL games
 * (SuperTuxKart and every other SDL port) make theirs with the Application
 * as context, so their surfaces came as "<title>(BLAST)#<n>", the hwcomposer
 * gave them no window, and the game's window stayed empty: the desktop
 * showed through it.
 *
 * multi_window_mode::get_window(), which picks a layer's window, is reached
 * through the hwcomposer's PLT, as is split_layer_names_helper::setup(),
 * which reads the frame's layer names first. For a layer without a task
 * whose name starts with the title of a task's window in the same frame
 * (the app's "<package>/<activity>", followed by what SurfaceView adds),
 * the window asked for is that task's. The longest title wins, then the
 * window nearest in the layer order (a SurfaceView sits just below, or
 * above, its own window). */
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
extern void *dlopen(const char *filename, int flags);
extern void *dlsym(void *handle, const char *symbol);
#define RTLD_NOW 2
#define RTLD_NOLOAD 4

/* libc++'s std::string (x86_64): long when the first byte's low bit is set
 * (size at 8, data pointer at 16), else short (size in the first byte's
 * upper bits, data from byte 1). */
struct std_string {
  union {
    unsigned char raw[24];
    size_t words[3]; /* its alignment */
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

/* waydroid_mode.h */
enum { TYPE_TID = 0, TYPE_RAW_NAME = 1 };
struct layer_info {
  int32_t type;
  struct std_string aid;       /* the package (TID), or the name (RawName) */
  struct std_string tid;
  struct std_string component; /* the activity (TID) */
};
_Static_assert(sizeof(struct layer_info) == 80, "layer_info");
_Static_assert(__builtin_offsetof(struct layer_info, aid) == 8, "layer_info");
_Static_assert(__builtin_offsetof(struct layer_info, tid) == 32, "layer_info");
/* split_layer_names_helper: a std::vector<layer_info>. */
struct helper {
  struct layer_info *begin, *end, *capacity;
};

#define SETUP                                                                  \
  "_ZN24split_layer_names_helper5setupEP30waydroid_hwc_composer_device_1PK11h" \
  "wc_layer_1m"
#define GET_WINDOW                                                             \
  "_ZN17multi_window_mode10get_windowEP30waydroid_hwc_composer_device_1R10la" \
  "yer_info"

typedef void (*setup_fn)(struct helper *, void *, const void *, size_t);
typedef void *(*get_window_fn)(void *, struct layer_info *);
static setup_fn real_setup;
static get_window_fn real_get_window;

/* The helper the frame being shown was read into; cleared once it's shown
 * (nickel-hwc-buffers.c's set() wrapper). */
__attribute__((visibility("hidden"))) const void *nickel_hwc_layer_infos;
/* nickel-hwc-client.c */
extern __attribute__((visibility("hidden"))) void
nickel_hwc_show_client_layers(const void *caller, const void *infos,
                              void *layers, size_t count);

/* The real function, from the library of the one calling it (the
 * hwcomposer); never this file's own. */
static void *real_function(const void *caller, const char *name,
                           const void *own) {
  Dl_info info;
  void *h = 0, *f = 0;
  if (dladdr(caller, &info) && info.dli_fname)
    h = dlopen(info.dli_fname, RTLD_NOW | RTLD_NOLOAD);
  if (h)
    f = dlsym(h, name);
  return f == own ? 0 : f;
}

void _ZN24split_layer_names_helper5setupEP30waydroid_hwc_composer_device_1PK11hwc_layer_1m(
    struct helper *self, void *pdev, const void *layers, size_t count) {
  if (!real_setup)
    real_setup = (setup_fn)real_function(
        __builtin_return_address(0), SETUP,
        (const void *)
            _ZN24split_layer_names_helper5setupEP30waydroid_hwc_composer_device_1PK11hwc_layer_1m);
  if (!real_setup)
    return;
  real_setup(self, pdev, layers, count);
  nickel_hwc_layer_infos = self;
  /* (the layers are the frame's own, which set() may change) */
  if ((size_t)(self->end - self->begin) == count)
    nickel_hwc_show_client_layers(__builtin_return_address(0), self->begin,
                                  (void *)layers, count);
}

static int name_char(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
         (c >= '0' && c <= '9') || c == '_' || c == '.';
}

/* How much of name a task window's "<package>/<activity>" covers, 0 if it
 * isn't how name starts. */
static size_t title_match(const struct layer_info *window, const char *name,
                          size_t size) {
  size_t na, nc;
  const char *a = str_data(&window->aid, &na);
  const char *c = str_data(&window->component, &nc);
  size_t n = na + 1 + nc;
  if (na == 0 || nc == 0 || size < n)
    return 0;
  for (size_t i = 0; i < na; i++)
    if (name[i] != a[i])
      return 0;
  if (name[na] != '/')
    return 0;
  for (size_t i = 0; i < nc; i++)
    if (name[na + 1 + i] != c[i])
      return 0;
  /* "...Activity(BLAST)" or "...Activity$Background", not "...Activity2" */
  return n == size || !name_char(name[n]) ? n : 0;
}

/* The task window info whose title the layer's name starts with, if any. */
static struct layer_info *task_of(struct layer_info *info) {
  const struct helper *h = nickel_hwc_layer_infos;
  if (!h || info < h->begin || info >= h->end)
    return 0;
  size_t size;
  const char *name = str_data(&info->aid, &size);
  int slash = 0;
  for (size_t i = 0; i < size; i++)
    slash |= name[i] == '/';
  if (!slash)
    return 0;
  struct layer_info *best = 0;
  size_t best_length = 0, best_distance = 0;
  for (struct layer_info *w = h->begin; w < h->end; w++) {
    if (w->type != TYPE_TID)
      continue;
    size_t length = title_match(w, name, size);
    if (!length)
      continue;
    size_t distance = w > info ? (size_t)(w - info) : (size_t)(info - w);
    if (length > best_length ||
        (length == best_length && distance < best_distance)) {
      best = w;
      best_length = length;
      best_distance = distance;
    }
  }
  return best;
}

void *_ZN17multi_window_mode10get_windowEP30waydroid_hwc_composer_device_1R10layer_info(
    void *pdev, struct layer_info *info) {
  if (!real_get_window)
    real_get_window = (get_window_fn)real_function(
        __builtin_return_address(0), GET_WINDOW,
        (const void *)
            _ZN17multi_window_mode10get_windowEP30waydroid_hwc_composer_device_1R10layer_info);
  if (!real_get_window)
    return 0;
  if (info && info->type == TYPE_RAW_NAME) {
    struct layer_info *task = task_of(info);
    if (task)
      return real_get_window(pdev, task);
  }
  return real_get_window(pdev, info);
}
