/* NickelOS: a native (non-Flatpak) GTK3 app, to check that such apps get
 * ChromeOS's theme, fonts and file picker through the portal. Prints what
 * GTK's settings say, then opens GtkFileChooserNative (the portal's
 * FileChooser when GTK_USE_PORTAL=1, as nickel-session sets) and prints the
 * answer. With the argument "save", a Save dialog suggesting untitled.txt.
 * Build: see vmtest/native-apps-test.sh. */
#include <gtk/gtk.h>

static gboolean save_mode;

static void on_activate(GtkApplication *app, gpointer data) {
  GtkWidget *win = gtk_application_window_new(app);
  gtk_window_set_title(GTK_WINDOW(win), "NickelOS GTK3 portal test");
  gtk_window_set_default_size(GTK_WINDOW(win), 420, 240);
  gtk_widget_show_all(win);

  gchar *theme = NULL, *font = NULL;
  g_object_get(gtk_settings_get_default(), "gtk-theme-name", &theme,
               "gtk-font-name", &font, NULL);
  g_print("theme=%s font=%s\n", theme, font);
  g_free(theme);
  g_free(font);

  GtkFileChooserNative *fc = gtk_file_chooser_native_new(
      save_mode ? "Native GTK3 save test" : "Native GTK3 open test",
      GTK_WINDOW(win),
      save_mode ? GTK_FILE_CHOOSER_ACTION_SAVE : GTK_FILE_CHOOSER_ACTION_OPEN,
      save_mode ? "_Save" : "_Open", "_Cancel");
  if (save_mode) {
    gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(fc), "untitled.txt");
  }
  gint r = gtk_native_dialog_run(GTK_NATIVE_DIALOG(fc));
  if (r == GTK_RESPONSE_ACCEPT) {
    gchar *f = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(fc));
    g_print("picked=%s\n", f);
    g_free(f);
  } else {
    g_print("response=%d\n", r);
  }
  g_application_quit(G_APPLICATION(app));
}

int main(int argc, char **argv) {
  if (argc > 1 && g_str_equal(argv[1], "save")) {
    save_mode = TRUE;
    argc = 1;
  }
  GtkApplication *app = gtk_application_new("org.nickelos.Gtk3PortalTest",
                                            G_APPLICATION_NON_UNIQUE);
  g_signal_connect(app, "activate", G_CALLBACK(on_activate), NULL);
  return g_application_run(G_APPLICATION(app), argc, argv);
}
