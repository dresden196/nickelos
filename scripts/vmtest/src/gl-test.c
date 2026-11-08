/* NickelOS: which OpenGL ES renderer a Wayland app gets: EGL on the session's
 * Wayland display (libglvnd -> the vendor chosen for Linux apps), a context
 * without a surface, then GL_VENDOR, GL_RENDERER and GL_VERSION. "llvmpipe"
 * means software; the GPU's name means hardware. Build: see
 * vmtest/gpu-test.sh. */
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <stdio.h>
#include <wayland-client.h>

int main(void) {
  struct wl_display *wl = wl_display_connect(NULL);
  if (!wl) {
    printf("no Wayland display\n");
    return 1;
  }
  EGLDisplay dpy = eglGetPlatformDisplay(EGL_PLATFORM_WAYLAND_KHR, wl, NULL);
  EGLint major = 0, minor = 0;
  if (dpy == EGL_NO_DISPLAY || !eglInitialize(dpy, &major, &minor)) {
    printf("EGL: cannot initialize (error 0x%x)\n", eglGetError());
    return 1;
  }
  printf("EGL %d.%d, vendor: %s\n", major, minor,
         eglQueryString(dpy, EGL_VENDOR));
  eglBindAPI(EGL_OPENGL_ES_API);
  const EGLint attrs[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
  EGLContext ctx = eglCreateContext(dpy, EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT, attrs);
  if (ctx == EGL_NO_CONTEXT ||
      !eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx)) {
    printf("EGL: no context (error 0x%x)\n", eglGetError());
    return 1;
  }
  printf("GL_VENDOR: %s\nGL_RENDERER: %s\nGL_VERSION: %s\n",
         glGetString(GL_VENDOR), glGetString(GL_RENDERER),
         glGetString(GL_VERSION));
  return 0;
}
