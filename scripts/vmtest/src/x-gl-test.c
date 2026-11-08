/* NickelOS: is OpenGL in X11 apps on the GPU? A GLX window, as X11 games
 * and Wine/Proton's OpenGL use it, that prints what drives it and how fast
 * it draws. Run inside the session (nickel-session-exec).
 *   x-gl-test SECONDS
 * prints "dri3=yes|no" (the X server's DRI3: GPU buffers shared with the
 * server, which needs glamor), "direct=yes|no", "renderer=..." (llvmpipe
 * means software), "version=..." and, after drawing for SECONDS,
 * "fps=N" (frames swapped per second; unthrottled if the driver allows).
 * Build: vmtest/build-test-client.sh x-gl-test x11 gl. */
#define _GNU_SOURCE
#include <GL/gl.h>
#include <GL/glx.h>
#include <X11/Xlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static double now(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return ts.tv_sec + ts.tv_nsec / 1e9;
}

int main(int argc, char** argv) {
  double seconds = argc > 1 ? atof(argv[1]) : 3;
  Display* display = XOpenDisplay(NULL);
  if (!display) {
    printf("no display\n");
    return 1;
  }
  int opcode, event, error;
  printf("dri3=%s\n",
         XQueryExtension(display, "DRI3", &opcode, &event, &error) ? "yes"
                                                                    : "no");
  int attributes[] = {GLX_RGBA, GLX_DOUBLEBUFFER, GLX_DEPTH_SIZE, 24, None};
  XVisualInfo* visual =
      glXChooseVisual(display, DefaultScreen(display), attributes);
  if (!visual) {
    printf("no GLX visual\n");
    return 1;
  }
  Window root = RootWindow(display, visual->screen);
  XSetWindowAttributes window_attributes = {0};
  window_attributes.colormap =
      XCreateColormap(display, root, visual->visual, AllocNone);
  window_attributes.event_mask = StructureNotifyMask;
  Window window = XCreateWindow(display, root, 0, 0, 640, 480, 0,
                                visual->depth, InputOutput, visual->visual,
                                CWColormap | CWEventMask, &window_attributes);
  XStoreName(display, window, "x-gl-test");
  XMapWindow(display, window);
  GLXContext context = glXCreateContext(display, visual, NULL, True);
  if (!context || !glXMakeCurrent(display, window, context)) {
    printf("no GLX context\n");
    return 1;
  }
  printf("direct=%s\n", glXIsDirect(display, context) ? "yes" : "no");
  printf("renderer=%s\n", (const char*)glGetString(GL_RENDERER));
  printf("version=%s\n", (const char*)glGetString(GL_VERSION));
  fflush(stdout);

  int frames = 0;
  double start = now(), end = start + seconds;
  while (now() < end) {
    while (XPending(display)) {
      XEvent e;
      XNextEvent(display, &e);
    }
    float t = (float)(now() - start);
    glClearColor(0.5f + 0.5f * (frames % 60) / 60.0f, 0.2f, 0.6f - t / 100, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    // Some fill work, so a software renderer shows.
    glBegin(GL_TRIANGLES);
    for (int i = 0; i < 200; i++) {
      float x = (i % 20) / 10.0f - 1, y = (i / 20) / 5.0f - 1;
      glColor3f(x, y, 1 - x);
      glVertex2f(x, y);
      glVertex2f(x + 0.3f, y);
      glVertex2f(x, y + 0.6f);
    }
    glEnd();
    glXSwapBuffers(display, window);
    frames++;
  }
  printf("fps=%.0f\n", frames / (now() - start));
  glXMakeCurrent(display, None, NULL);
  glXDestroyContext(display, context);
  XCloseDisplay(display);
  return 0;
}
