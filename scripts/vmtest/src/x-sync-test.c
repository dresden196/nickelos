/* NickelOS: are an X11 app's frames shown only once the GPU has finished
 * drawing them? On GPUs without implicit synchronization (NVIDIA) nothing
 * else stops the compositor from reading a frame the GPU is still drawing.
 * A fullscreen GLX window: every frame is cleared to black, then filled by
 * one slow fragment shader with pure red or pure blue, alternating. A
 * frame shown before the GPU finished has black (or the other color) in it;
 * synchronized, every screenshot is all red or all blue (hw/x-sync-test.sh
 * takes and checks them). Run inside the session (nickel-session-exec).
 *   x-sync-test SECONDS [MS] [window]
 * MS: GPU time per frame to aim for (default 8). "window": a 1600x900
 * window instead of fullscreen, which Chrome composites on the GPU (a
 * fullscreen one can go straight to a display plane, where the kernel waits
 * for the buffer's fences anyway). Prints "renderer=...",
 * "loops=N frame_ms=M" (the calibrated shader work) and, at the end,
 * "fps=N". No glFinish while it presents: only the driver's and the
 * compositor's synchronization keep the frames whole.
 * Build: vmtest/build-test-client.sh x-sync-test x11 gl. */
#define _GNU_SOURCE
#include <GL/gl.h>
#include <GL/glext.h>
#include <GL/glx.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static double now(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return ts.tv_sec + ts.tv_nsec / 1e9;
}

#define GL_FN(type, name) static type name##_;
GL_FN(PFNGLCREATESHADERPROC, glCreateShader)
GL_FN(PFNGLSHADERSOURCEPROC, glShaderSource)
GL_FN(PFNGLCOMPILESHADERPROC, glCompileShader)
GL_FN(PFNGLGETSHADERIVPROC, glGetShaderiv)
GL_FN(PFNGLCREATEPROGRAMPROC, glCreateProgram)
GL_FN(PFNGLATTACHSHADERPROC, glAttachShader)
GL_FN(PFNGLLINKPROGRAMPROC, glLinkProgram)
GL_FN(PFNGLUSEPROGRAMPROC, glUseProgram)
GL_FN(PFNGLGETUNIFORMLOCATIONPROC, glGetUniformLocation)
GL_FN(PFNGLUNIFORM1IPROC, glUniform1i)
GL_FN(PFNGLUNIFORM3FPROC, glUniform3f)

static int load_gl(void) {
#define LOAD(name)                                                       \
  if (!(name##_ = (void*)glXGetProcAddress((const GLubyte*)#name))) { \
    printf("no %s\n", #name);                                          \
    return 0;                                                          \
  }
  LOAD(glCreateShader) LOAD(glShaderSource) LOAD(glCompileShader)
  LOAD(glGetShaderiv) LOAD(glCreateProgram) LOAD(glAttachShader)
  LOAD(glLinkProgram) LOAD(glUseProgram) LOAD(glGetUniformLocation)
  LOAD(glUniform1i) LOAD(glUniform3f)
  return 1;
}

/* Busy work whose result cannot be optimized away, yet leaves the color
 * pure: the sum only decides whether a never-true branch is taken. */
static const char* kFragment =
    "#version 120\n"
    "uniform int loops;\n"
    "uniform vec3 color;\n"
    "void main() {\n"
    "  float s = gl_FragCoord.x * 0.001 + gl_FragCoord.y * 0.0007;\n"
    "  for (int i = 0; i < loops; i++) s = fract(sin(s * 12.9898 + 78.233) * 43758.5453);\n"
    "  gl_FragColor = vec4(s > 2.0 ? vec3(0.5) : color, 1.0);\n"
    "}\n";

static GLuint program, u_loops, u_color;

static void draw(int loops, int frame) {
  glClearColor(0, 0, 0, 1);
  glClear(GL_COLOR_BUFFER_BIT);
  glUseProgram_(program);
  glUniform1i_(u_loops, loops);
  if (frame % 2)
    glUniform3f_(u_color, 0, 0, 1);
  else
    glUniform3f_(u_color, 1, 0, 0);
  glBegin(GL_TRIANGLE_STRIP);
  glVertex2f(-1, -1);
  glVertex2f(1, -1);
  glVertex2f(-1, 1);
  glVertex2f(1, 1);
  glEnd();
}

int main(int argc, char** argv) {
  double seconds = argc > 1 ? atof(argv[1]) : 10;
  double target_ms = argc > 2 ? atof(argv[2]) : 8;
  int windowed = argc > 3 && !strcmp(argv[3], "window");
  Display* display = XOpenDisplay(NULL);
  if (!display) {
    printf("no display\n");
    return 1;
  }
  int attributes[] = {GLX_RGBA, GLX_DOUBLEBUFFER, None};
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
  Window window = XCreateWindow(display, root, 0, 0, windowed ? 1600 : 800,
                                windowed ? 900 : 600, 0,
                                visual->depth, InputOutput, visual->visual,
                                CWColormap | CWEventMask, &window_attributes);
  XStoreName(display, window, "x-sync-test");
  Atom state = XInternAtom(display, "_NET_WM_STATE", False);
  Atom fullscreen = XInternAtom(display, "_NET_WM_STATE_FULLSCREEN", False);
  if (!windowed) {
    XChangeProperty(display, window, state, XA_ATOM, 32, PropModeReplace,
                    (unsigned char*)&fullscreen, 1);
  }
  XMapWindow(display, window);
  GLXContext context = glXCreateContext(display, visual, NULL, True);
  if (!context || !glXMakeCurrent(display, window, context) || !load_gl()) {
    printf("no GLX context\n");
    return 1;
  }
  printf("renderer=%s\n", (const char*)glGetString(GL_RENDERER));

  GLuint shader = glCreateShader_(GL_FRAGMENT_SHADER);
  glShaderSource_(shader, 1, &kFragment, NULL);
  glCompileShader_(shader);
  GLint ok = 0;
  glGetShaderiv_(shader, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    printf("shader does not compile\n");
    return 1;
  }
  program = glCreateProgram_();
  glAttachShader_(program, shader);
  glLinkProgram_(program);
  u_loops = glGetUniformLocation_(program, "loops");
  u_color = glGetUniformLocation_(program, "color");

  /* Let the window reach its fullscreen size, then calibrate the shader
   * work to the target GPU time (glFinish only here). */
  double settle = now() + 2;
  while (now() < settle) {
    while (XPending(display)) {
      XEvent e;
      XNextEvent(display, &e);
      if (e.type == ConfigureNotify)
        glViewport(0, 0, e.xconfigure.width, e.xconfigure.height);
    }
    draw(1, 0);
    glXSwapBuffers(display, window);
  }
  int loops = 16;
  double frame_ms = 0;
  for (int round = 0; round < 12; round++) {
    glFinish();
    double t = now();
    draw(loops, 0);
    glFinish();
    frame_ms = (now() - t) * 1000;
    if (frame_ms >= target_ms * 0.8 || loops >= (1 << 20))
      break;
    loops = frame_ms > 0.05 ? (int)(loops * target_ms / frame_ms) + 1
                            : loops * 8;
  }
  printf("loops=%d frame_ms=%.1f\n", loops, frame_ms);
  fflush(stdout);

  int frames = 0;
  double start = now(), end = start + seconds;
  while (now() < end) {
    while (XPending(display)) {
      XEvent e;
      XNextEvent(display, &e);
      if (e.type == ConfigureNotify)
        glViewport(0, 0, e.xconfigure.width, e.xconfigure.height);
    }
    draw(loops, frames);
    glXSwapBuffers(display, window);
    frames++;
  }
  printf("fps=%.0f\n", frames / (now() - start));
  glXMakeCurrent(display, None, NULL);
  glXDestroyContext(display, context);
  XCloseDisplay(display);
  return 0;
}
