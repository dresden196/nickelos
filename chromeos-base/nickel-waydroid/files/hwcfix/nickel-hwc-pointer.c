/* Distributed under the terms of the GNU General Public License v2
 *
 * NickelOS: preloaded into Waydroid's composer service with
 * nickel-hwc-roundtrip.c. Reads Android's mouse pointer with the CPU.
 *
 * The hwcomposer gives the compositor the pointer as a wl_shm buffer, filled
 * by egl_render_to_pixels: an EGLImage of Android's pointer sprite, bound to
 * a framebuffer, then glReadPixels. Android draws the sprite with the CPU
 * (SpriteController locks the buffer). On NVIDIA, where Android's GL is
 * ANGLE over Venus, that GPU read-back comes out as noise around pieces of
 * the arrow (325 of 784 pixels not even valid premultiplied colour): the
 * pointer over every Android window was garbage. Intel's Mesa reads the same
 * buffer fine.
 *
 * eglCreateImageKHR, glReadPixels and eglDestroyImageKHR are reached through
 * the hwcomposer's PLT. When the image is of this frame's pointer buffer
 * (nickel-hwc-buffers.c's set() wrapper notes the HWC_IS_CURSOR_LAYER
 * layer's handle; the hwcomposer wraps that same handle) in 8-bit RGBA, RGBX
 * or BGRA, the read-back that follows locks the buffer for CPU reading and
 * copies it instead, as the hwcomposer itself does with the default gralloc.
 * (The wrapper gives the buffer GPU texture usage only, so its usage can't
 * tell.) Every other read-back (window snapshots) is the GPU's. */
typedef unsigned int uint32_t;
typedef int int32_t;
typedef unsigned long uint64_t;
typedef __SIZE_TYPE__ size_t;

extern void *dlopen(const char *filename, int flags);
extern void *dlsym(void *handle, const char *symbol);
#define RTLD_NOW 2
#define RTLD_NOLOAD 4
#define RTLD_NEXT ((void *)-1L)

typedef void *EGLDisplay;
typedef void *EGLContext;
typedef void *EGLClientBuffer;
typedef void *EGLImageKHR;
typedef unsigned int EGLenum;
typedef int32_t EGLint;
typedef unsigned int EGLBoolean;
typedef int GLint;
typedef int GLsizei;
typedef unsigned int GLenum;
typedef long EGLAttrib;
#define EGL_NONE 0x3038
#define EGL_NATIVE_BUFFER_ANDROID 0x3140
#define GL_UNSIGNED_BYTE 0x1401
#define GL_BGRA_EXT 0x80E1

/* ANativeWindowBuffer (system/window.h, NDK ABI): the native handle comes
 * after android_native_base_t (56 bytes), five ints, layerCount and one
 * reserved pointer. */
#define NATIVE_BUFFER_HANDLE_OFFSET 96
extern __attribute__((visibility("hidden"))) const void *nickel_hwc_pointer_handle;

/* android/hardware_buffer.h */
struct ahb_desc {
  uint32_t width;
  uint32_t height;
  uint32_t layers;
  uint32_t format;
  uint64_t usage;
  uint32_t stride;
  uint32_t rfu0;
  uint64_t rfu1;
};
#define AHB_FORMAT_RGBA_8888 1
#define AHB_FORMAT_RGBX_8888 2
#define AHB_FORMAT_BGRA_8888 5 /* HAL_PIXEL_FORMAT_BGRA_8888 */
#define AHB_USAGE_CPU_READ_OFTEN 3UL

/* The hwcomposer calls the KHR functions; a vendor process's dlsym on
 * libEGL doesn't give those, but does give EGL 1.5's eglCreateImage and
 * eglDestroyImage, which do the same. */
static EGLImageKHR (*real_create)(EGLDisplay, EGLContext, EGLenum,
                                  EGLClientBuffer, const EGLint *);
static EGLImageKHR (*real_create15)(EGLDisplay, EGLContext, EGLenum,
                                    EGLClientBuffer, const EGLAttrib *);
static EGLBoolean (*real_destroy)(EGLDisplay, EGLImageKHR);
static void (*real_read)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum,
                         void *);
static void *(*get_hardware_buffer)(void *native_buffer);
static void (*describe)(const void *ahb, struct ahb_desc *desc);
static int (*lock)(void *ahb, uint64_t usage, int32_t fence, const void *rect,
                   void **address);
static int (*unlock)(void *ahb, int32_t *fence);

/* The image whose read-back goes through the CPU, if the last one made is. */
static EGLImageKHR cpu_image;
static void *cpu_buffer;
static struct ahb_desc cpu_desc;

/* The real function from the library that has it (Android's linker doesn't
 * reach libEGL or libGLESv2 from here through RTLD_NEXT). Never this file's
 * own. */
static void *real_function(const char *library, const char *name) {
  void *f = dlsym(RTLD_NEXT, name);
  if (!f) {
    void *h = dlopen(library, RTLD_NOW | RTLD_NOLOAD);
    if (!h)
      h = dlopen(library, RTLD_NOW);
    f = h ? dlsym(h, name) : 0;
  }
  return f;
}

static int resolve_nativewindow(void) {
  static int done;
  if (!done) {
    void *h = dlopen("libnativewindow.so", RTLD_NOW | RTLD_NOLOAD);
    if (!h)
      h = dlopen("libnativewindow.so", RTLD_NOW);
    if (h) {
      get_hardware_buffer = dlsym(h, "ANativeWindowBuffer_getHardwareBuffer");
      describe = dlsym(h, "AHardwareBuffer_describe");
      lock = dlsym(h, "AHardwareBuffer_lock");
      unlock = dlsym(h, "AHardwareBuffer_unlock");
    }
    done = 1;
  }
  return get_hardware_buffer && describe && lock && unlock;
}

static EGLImageKHR create_image(EGLDisplay dpy, EGLContext ctx,
                               EGLenum target, EGLClientBuffer buffer,
                               const EGLint *attrs) {
  static int resolved;
  if (!resolved) {
    real_create = real_function("libEGL.so", "eglCreateImageKHR");
    if (!real_create)
      real_create15 = real_function("libEGL.so", "eglCreateImage");
    resolved = 1;
  }
  if (real_create)
    return real_create(dpy, ctx, target, buffer, attrs);
  if (!real_create15)
    return 0;
  EGLAttrib list[33];
  int n = 0;
  while (attrs && attrs[n] != EGL_NONE && n < 32) {
    list[n] = attrs[n];
    list[n + 1] = attrs[n + 1];
    n += 2;
  }
  list[n] = EGL_NONE;
  return real_create15(dpy, ctx, target, buffer, list);
}

EGLImageKHR eglCreateImageKHR(EGLDisplay dpy, EGLContext ctx, EGLenum target,
                              EGLClientBuffer buffer, const EGLint *attrs) {
  EGLImageKHR image = create_image(dpy, ctx, target, buffer, attrs);
  cpu_image = 0;
  cpu_buffer = 0;
  if (image && target == EGL_NATIVE_BUFFER_ANDROID && buffer &&
      nickel_hwc_pointer_handle &&
      *(const void *const *)((const char *)buffer +
                             NATIVE_BUFFER_HANDLE_OFFSET) ==
          nickel_hwc_pointer_handle &&
      resolve_nativewindow()) {
    void *ahb = get_hardware_buffer(buffer);
    if (ahb) {
      describe(ahb, &cpu_desc);
      if ((cpu_desc.format == AHB_FORMAT_RGBA_8888 ||
           cpu_desc.format == AHB_FORMAT_RGBX_8888 ||
           cpu_desc.format == AHB_FORMAT_BGRA_8888)) {
        cpu_image = image;
        cpu_buffer = ahb;
      }
    }
  }
  return image;
}

EGLBoolean eglDestroyImageKHR(EGLDisplay dpy, EGLImageKHR image) {
  if (!real_destroy)
    real_destroy = real_function("libEGL.so", "eglDestroyImageKHR");
  if (!real_destroy)
    real_destroy = real_function("libEGL.so", "eglDestroyImage");
  if (image && image == cpu_image) {
    cpu_image = 0;
    cpu_buffer = 0;
  }
  return real_destroy ? real_destroy(dpy, image) : 0;
}

/* Rows of width pixels, top first, BGRA, packed: what glReadPixels gives
 * the hwcomposer from an image of the buffer. */
static int read_with_cpu(GLsizei width, GLsizei height, uint32_t *out) {
  void *address = 0;
  if (lock(cpu_buffer, AHB_USAGE_CPU_READ_OFTEN, -1, 0, &address) != 0 ||
      !address)
    return 0;
  for (GLsizei y = 0; y < height; y++) {
    const uint32_t *in = (const uint32_t *)address + (size_t)y * cpu_desc.stride;
    uint32_t *row = out + (size_t)y * width;
    for (GLsizei x = 0; x < width; x++) {
      uint32_t c = in[x];
      if (cpu_desc.format != AHB_FORMAT_BGRA_8888)
        c = (c & 0xFF00FF00) | ((c & 0xFF0000) >> 16) | ((c & 0xFF) << 16);
      if (cpu_desc.format == AHB_FORMAT_RGBX_8888)
        c |= 0xFF000000;
      row[x] = c;
    }
  }
  unlock(cpu_buffer, 0);
  return 1;
}

void glReadPixels(GLint x, GLint y, GLsizei width, GLsizei height,
                  GLenum format, GLenum type, void *pixels) {
  if (!real_read)
    real_read = real_function("libGLESv2.so", "glReadPixels");
  if (cpu_buffer && pixels && x == 0 && y == 0 && width > 0 && height > 0 &&
      (uint32_t)width <= cpu_desc.width && (uint32_t)height <= cpu_desc.height &&
      cpu_desc.stride >= cpu_desc.width && format == GL_BGRA_EXT &&
      type == GL_UNSIGNED_BYTE && read_with_cpu(width, height, pixels))
    return;
  if (real_read)
    real_read(x, y, width, height, format, type, pixels);
}
