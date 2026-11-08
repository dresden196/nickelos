/* NickelOS: which OpenGL driver an X11 program gets through GLX, built
 * 32-bit or 64-bit (gcc -m32), for testing the prefix's 32-bit graphics
 * (hw/lib32-test.sh): most 32-bit games draw with OpenGL on X11, through
 * glvnd's libGL, which loads the driver Xwayland names (NVIDIA's on NVIDIA,
 * Mesa's elsewhere). Prints the word size, then GL_VENDOR, GL_RENDERER and
 * GL_VERSION of a context on an unmapped window.
 *   glx-renderer
 * Build: gcc [-m32] -o glx-renderer glx-renderer.c -lGL -lX11 */
#include <stdio.h>
#include <GL/gl.h>
#include <GL/glx.h>
#include <X11/Xlib.h>

int main(void) {
  printf("%zu-bit\n", sizeof(void*) * 8);
  Display* display = XOpenDisplay(NULL);
  if (!display) {
    printf("no X display\n");
    return 1;
  }
  int attributes[] = {GLX_RGBA, GLX_DOUBLEBUFFER, GLX_DEPTH_SIZE, 24, None};
  XVisualInfo* visual =
      glXChooseVisual(display, DefaultScreen(display), attributes);
  if (!visual) {
    printf("glXChooseVisual failed\n");
    return 1;
  }
  Window root = RootWindow(display, visual->screen);
  XSetWindowAttributes window_attributes = {0};
  window_attributes.colormap =
      XCreateColormap(display, root, visual->visual, AllocNone);
  Window window = XCreateWindow(display, root, 0, 0, 64, 64, 0, visual->depth,
                                InputOutput, visual->visual, CWColormap,
                                &window_attributes);
  GLXContext context = glXCreateContext(display, visual, NULL, True);
  if (!context || !glXMakeCurrent(display, window, context)) {
    printf("no GLX context\n");
    return 1;
  }
  printf("vendor: %s\nrenderer: %s\nversion: %s\n",
         (const char*)glGetString(GL_VENDOR),
         (const char*)glGetString(GL_RENDERER),
         (const char*)glGetString(GL_VERSION));
  glXMakeCurrent(display, None, NULL);
  glXDestroyContext(display, context);
  XDestroyWindow(display, window);
  XCloseDisplay(display);
  return 0;
}
