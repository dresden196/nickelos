/* NickelOS: does a Linux app's new window, and a dialog it opens, get the
 * keyboard focus? Prints the compositor's answer (GtkWindow:is-active):
 *   main=<0|1>     the app's first window, 3 s after it is shown
 *   dialog=<0|1>   a modal dialog opened from it, 3 s later
 * Build: see vmtest/native-apps-test.sh. */
#include <gtk/gtk.h>

static GtkWidget *win, *dialog;

static gboolean report_dialog(gpointer app) {
  g_print("dialog=%d main=%d\n", gtk_window_is_active(GTK_WINDOW(dialog)),
          gtk_window_is_active(GTK_WINDOW(win)));
  g_application_quit(G_APPLICATION(app));
  return G_SOURCE_REMOVE;
}

static gboolean open_dialog(gpointer app) {
  g_print("main=%d\n", gtk_window_is_active(GTK_WINDOW(win)));
  dialog = gtk_message_dialog_new(GTK_WINDOW(win), GTK_DIALOG_MODAL,
                                  GTK_MESSAGE_QUESTION, GTK_BUTTONS_OK_CANCEL,
                                  "NickelOS focus test dialog");
  gtk_widget_show(dialog);
  g_timeout_add_seconds(3, report_dialog, app);
  return G_SOURCE_REMOVE;
}

static void on_activate(GtkApplication *app, gpointer data) {
  win = gtk_application_window_new(app);
  gtk_window_set_title(GTK_WINDOW(win), "NickelOS GTK3 focus test");
  gtk_window_set_default_size(GTK_WINDOW(win), 420, 240);
  gtk_widget_show_all(win);
  g_timeout_add_seconds(3, open_dialog, app);
}

int main(int argc, char **argv) {
  GtkApplication *app = gtk_application_new("org.nickelos.Gtk3FocusTest",
                                            G_APPLICATION_NON_UNIQUE);
  g_signal_connect(app, "activate", G_CALLBACK(on_activate), NULL);
  return g_application_run(G_APPLICATION(app), argc, argv);
}
