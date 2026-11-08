/* Distributed under the terms of the GNU General Public License v2
 *
 * NickelOS: preloaded into Waydroid's composer service with
 * nickel-hwc-roundtrip.c. Tells Android which window has the keyboard
 * without holding up the hwcomposer.
 *
 * When a window gets the keyboard, the hwcomposer's Wayland thread takes the
 * window lock and, still holding it, calls Android's setFocusedTask (through
 * the vendor.waydroid.task HAL, into system_server's window manager). If
 * system_server is waiting on SurfaceFlinger right then, and SurfaceFlinger
 * on a frame whose present needs that window lock, none of them moves again
 * and Android's watchdog restarts it (seen when an app's window opens and
 * gets the keyboard, a few times an hour on some machines).
 *
 * The call is made by the HAL's static _hidl_setFocusedTask, which its
 * setFocusedTask reaches through the PLT. This one hands the call to a thread
 * of its own and returns success at once, so the lock is let go; the newest
 * request wins if several wait. */
typedef unsigned char uint8_t;
typedef unsigned int uint32_t;
typedef int int32_t;

extern void *dlopen(const char *filename, int flags);
extern void *dlsym(void *handle, const char *symbol);
#define RTLD_NOW 2
#define RTLD_NOLOAD 4
#define RTLD_NEXT ((void *)-1L)

/* bionic's 64-bit pthread types, zero-initialized as their initializers. */
typedef struct {
  long private_[8];
} pthread_mutex_t;
typedef struct {
  long private_[8];
} pthread_cond_t;
typedef long pthread_t;
extern int pthread_create(pthread_t *t, const void *attr, void *(*fn)(void *),
                          void *arg);
extern int pthread_mutex_lock(pthread_mutex_t *m);
extern int pthread_mutex_unlock(pthread_mutex_t *m);
extern int pthread_cond_wait(pthread_cond_t *c, pthread_mutex_t *m);
extern int pthread_cond_signal(pthread_cond_t *c);

/* android::hardware::Return<void>: a Status (exception code, error code,
 * String8 message) and whether it was checked. */
struct hidl_return {
  int32_t exception;
  int32_t error;
  void *message;
  uint8_t checked;
};
typedef struct hidl_return *(*hidl_call)(struct hidl_return *ret, void *iface,
                                         void *instrumentor, uint32_t task);
typedef void *(*status_ok)(void *status);
typedef void (*return_dtor)(struct hidl_return *ret);

#define SET_FOCUSED_TASK                                                       \
  "_ZN6vendor8waydroid4task4V1_016BpHwWaydroidTask20_hidl_setFocusedTaskEPN7" \
  "android8hardware10IInterfaceEPNS5_7details16HidlInstrumentorEj"

static hidl_call real_call;
static status_ok ok;
static return_dtor dtor;
static pthread_mutex_t lock;
static pthread_cond_t wake;
static int started, pending;
static void *pending_iface, *pending_instrumentor;
static uint32_t pending_task;

static void *focus_thread(void *unused) {
  for (;;) {
    pthread_mutex_lock(&lock);
    while (!pending)
      pthread_cond_wait(&wake, &lock);
    void *iface = pending_iface, *instrumentor = pending_instrumentor;
    uint32_t task = pending_task;
    pending = 0;
    pthread_mutex_unlock(&lock);
    struct hidl_return r;
    real_call(&r, iface, instrumentor, task);
    r.checked = 1; /* a failure is only logged, as the hwcomposer would */
    dtor(&r);
  }
  return 0;
}

static int resolve(void) {
  void *hidl = dlopen("libhidlbase.so", RTLD_NOW | RTLD_NOLOAD);
  real_call = dlsym(RTLD_NEXT, SET_FOCUSED_TASK);
  if (!real_call) {
    void *h = dlopen("vendor.waydroid.task@1.0.so", RTLD_NOW | RTLD_NOLOAD);
    real_call = h ? dlsym(h, SET_FOCUSED_TASK) : 0;
  }
  if (hidl) {
    ok = dlsym(hidl, "_ZN7android8hardware6Status2okEv");
    dtor = dlsym(hidl, "_ZN7android8hardware7details13return_statusD1Ev");
    if (!dtor)
      dtor = dlsym(hidl, "_ZN7android8hardware7details13return_statusD2Ev");
  }
  return real_call && ok && dtor;
}

struct hidl_return *
_ZN6vendor8waydroid4task4V1_016BpHwWaydroidTask20_hidl_setFocusedTaskEPN7android8hardware10IInterfaceEPNS5_7details16HidlInstrumentorEj(
    struct hidl_return *ret, void *iface, void *instrumentor, uint32_t task) {
  pthread_mutex_lock(&lock);
  if (!started) {
    pthread_t t;
    started = resolve() && pthread_create(&t, 0, focus_thread, 0) == 0 ? 1 : -1;
  }
  if (started < 0) {
    pthread_mutex_unlock(&lock);
    /* Can't defer it: the call as it was. */
    return real_call(ret, iface, instrumentor, task);
  }
  pending_iface = iface;
  pending_instrumentor = instrumentor;
  pending_task = task;
  pending = 1;
  pthread_cond_signal(&wake);
  pthread_mutex_unlock(&lock);
  ok(ret); /* Return<void> holding Status::ok() */
  ret->checked = 0;
  return ret;
}
