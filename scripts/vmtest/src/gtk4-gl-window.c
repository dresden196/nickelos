/* NickelOS: a native GTK 4 window drawn with OpenGL (GtkGLArea), to see GPU
 * rendering reach the screen through Exo: the window is cleared to a color
 * that cycles, and the title bar shows the GL renderer. Prints the renderer
 * and the frames drawn, then quits after the seconds given (default 4).
 * Build: see vmtest/gpu-test.sh. */
#include <GLES2/gl2.h>
#include <gtk/gtk.h>
#include <stdlib.h>

static int frames;
static GtkWidget *window;

static gboolean render(GtkGLArea *area, GdkGLContext *context, gpointer data) {
  float t = (frames++ % 120) / 120.0f;
  glClearColor(t, 0.4f, 1.0f - t, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);
  if (frames == 1) {
    const char *renderer = (const char *)glGetString(GL_RENDERER);
    g_print("GL_RENDERER: %s\n", renderer);
    gtk_window_set_title(GTK_WINDOW(window), renderer);
  }
  return TRUE;
}

static gboolean tick(GtkWidget *widget, GdkFrameClock *clock, gpointer data) {
  gtk_gl_area_queue_render(GTK_GL_AREA(widget));
  return G_SOURCE_CONTINUE;
}

static gboolean quit(gpointer app) {
  g_print("frames=%d\n", frames);
  g_application_quit(G_APPLICATION(app));
  return G_SOURCE_REMOVE;
}

static void on_activate(GtkApplication *app, gpointer data) {
  window = gtk_application_window_new(app);
  gtk_window_set_default_size(GTK_WINDOW(window), 480, 320);
  GtkWidget *area = gtk_gl_area_new();
  gtk_gl_area_set_allowed_apis(GTK_GL_AREA(area), GDK_GL_API_GLES);
  g_signal_connect(area, "render", G_CALLBACK(render), NULL);
  gtk_widget_add_tick_callback(area, tick, NULL, NULL);
  gtk_window_set_child(GTK_WINDOW(window), area);
  gtk_window_present(GTK_WINDOW(window));
  g_timeout_add_seconds(GPOINTER_TO_INT(data), quit, app);
}

int main(int argc, char **argv) {
  int seconds = argc > 1 ? atoi(argv[1]) : 4;
  GtkApplication *app = gtk_application_new("org.nickelos.GlWindowTest",
                                            G_APPLICATION_NON_UNIQUE);
  g_signal_connect(app, "activate", G_CALLBACK(on_activate),
                   GINT_TO_POINTER(seconds));
  return g_application_run(G_APPLICATION(app), 1, argv);
}
