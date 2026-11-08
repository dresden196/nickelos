/* NickelOS: a native (non-Flatpak) GTK 4 + libadwaita app, to check that
 * such apps run on the image's own GTK 4 and libadwaita and get ChromeOS's
 * look and file picker through the portal. Prints the color scheme and
 * accent color libadwaita's style manager reports (from the portal's
 * org.freedesktop.appearance keys) and any change to them while it runs,
 * which GSK renderer GTK picked, then opens GtkFileDialog (always the
 * portal's FileChooser in GTK 4 when one is running) and prints the answer.
 * With the argument "save", a Save dialog suggesting untitled.txt.
 * Build: see vmtest/native-apps-test.sh. */
#include <adwaita.h>

static gboolean save_mode;

static const char *accent_name(AdwAccentColor c) {
  switch (c) {
    case ADW_ACCENT_COLOR_BLUE: return "blue";
    case ADW_ACCENT_COLOR_TEAL: return "teal";
    case ADW_ACCENT_COLOR_GREEN: return "green";
    case ADW_ACCENT_COLOR_YELLOW: return "yellow";
    case ADW_ACCENT_COLOR_ORANGE: return "orange";
    case ADW_ACCENT_COLOR_RED: return "red";
    case ADW_ACCENT_COLOR_PINK: return "pink";
    case ADW_ACCENT_COLOR_PURPLE: return "purple";
    case ADW_ACCENT_COLOR_SLATE: return "slate";
    default: return "?";
  }
}

static void print_style(AdwStyleManager *sm) {
  GdkRGBA *rgba = adw_style_manager_get_accent_color_rgba(sm);
  char *rgba_str = gdk_rgba_to_string(rgba);
  g_print("scheme=%s system_supports_color_schemes=%d accent=%s (%s) "
          "system_supports_accent_colors=%d high_contrast=%d\n",
          adw_style_manager_get_dark(sm) ? "dark" : "light",
          adw_style_manager_get_system_supports_color_schemes(sm),
          accent_name(adw_style_manager_get_accent_color(sm)), rgba_str,
          adw_style_manager_get_system_supports_accent_colors(sm),
          adw_style_manager_get_high_contrast(sm));
  g_free(rgba_str);
  gdk_rgba_free(rgba);
}

static void on_style_changed(AdwStyleManager *sm, GParamSpec *pspec,
                             gpointer data) {
  g_print("changed: ");
  print_style(sm);
}

static void on_file_done(GObject *source, GAsyncResult *res, gpointer app) {
  GError *error = NULL;
  GFile *file = save_mode
      ? gtk_file_dialog_save_finish(GTK_FILE_DIALOG(source), res, &error)
      : gtk_file_dialog_open_finish(GTK_FILE_DIALOG(source), res, &error);
  if (file) {
    char *path = g_file_get_path(file);
    g_print("picked=%s\n", path);
    g_free(path);
    g_object_unref(file);
  } else {
    g_print("response=%s\n", error ? error->message : "none");
    g_clear_error(&error);
  }
  g_application_quit(G_APPLICATION(app));
}

static gboolean open_dialog(gpointer win) {
  GtkApplication *app = gtk_window_get_application(GTK_WINDOW(win));
  GtkFileDialog *fd = gtk_file_dialog_new();
  gtk_file_dialog_set_title(fd, save_mode ? "Native GTK4 save test"
                                          : "Native GTK4 open test");
  if (save_mode) {
    gtk_file_dialog_set_initial_name(fd, "untitled.txt");
    gtk_file_dialog_save(fd, GTK_WINDOW(win), NULL, on_file_done, app);
  } else {
    gtk_file_dialog_open(fd, GTK_WINDOW(win), NULL, on_file_done, app);
  }
  g_object_unref(fd);
  return G_SOURCE_REMOVE;
}

/* The dialog opens 6 s after the window shows, so a screenshot can catch the
 * window itself first. */
static void on_map(GtkWidget *win, gpointer data) {
  GskRenderer *renderer = gtk_native_get_renderer(GTK_NATIVE(win));
  g_print("renderer=%s backend=%s\n",
          renderer ? G_OBJECT_TYPE_NAME(renderer) : "none",
          G_OBJECT_TYPE_NAME(gtk_widget_get_display(win)));
  g_timeout_add_seconds(6, open_dialog, win);
}

static void on_activate(GtkApplication *app, gpointer data) {
  AdwStyleManager *sm = adw_style_manager_get_default();
  g_print("libadwaita=%u.%u.%u gtk=%u.%u.%u\n", adw_get_major_version(),
          adw_get_minor_version(), adw_get_micro_version(),
          gtk_get_major_version(), gtk_get_minor_version(),
          gtk_get_micro_version());
  print_style(sm);
  g_signal_connect(sm, "notify::dark", G_CALLBACK(on_style_changed), NULL);
  g_signal_connect(sm, "notify::accent-color", G_CALLBACK(on_style_changed),
                   NULL);

  GtkWidget *win = adw_application_window_new(app);
  gtk_window_set_title(GTK_WINDOW(win), "NickelOS GTK4 portal test");
  gtk_window_set_default_size(GTK_WINDOW(win), 560, 460);

  GtkWidget *view = adw_toolbar_view_new();
  adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(view), adw_header_bar_new());
  GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
  gtk_widget_set_valign(box, GTK_ALIGN_CENTER);
  GtkWidget *icon = gtk_image_new_from_icon_name("document-open-symbolic");
  gtk_image_set_pixel_size(GTK_IMAGE(icon), 48);
  GtkWidget *title = gtk_label_new("GTK 4 + libadwaita");
  gtk_widget_add_css_class(title, "title-2");
  GtkWidget *desc = gtk_label_new("Native, from the NickelOS image");
  gtk_widget_add_css_class(desc, "dim-label");
  GtkWidget *button = gtk_button_new_with_label("Accent color");
  gtk_widget_add_css_class(button, "suggested-action");
  gtk_widget_add_css_class(button, "pill");
  gtk_widget_set_halign(button, GTK_ALIGN_CENTER);
  gtk_box_append(GTK_BOX(box), icon);
  gtk_box_append(GTK_BOX(box), title);
  gtk_box_append(GTK_BOX(box), desc);
  gtk_box_append(GTK_BOX(box), button);
  adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(view), box);
  adw_application_window_set_content(ADW_APPLICATION_WINDOW(win), view);

  g_signal_connect_after(win, "map", G_CALLBACK(on_map), NULL);
  gtk_window_present(GTK_WINDOW(win));
}

int main(int argc, char **argv) {
  if (argc > 1 && g_str_equal(argv[1], "save")) {
    save_mode = TRUE;
    argc = 1;
  }
  AdwApplication *app = adw_application_new("org.nickelos.Gtk4PortalTest",
                                            G_APPLICATION_NON_UNIQUE);
  g_signal_connect(app, "activate", G_CALLBACK(on_activate), NULL);
  int status = g_application_run(G_APPLICATION(app), argc, argv);
  g_object_unref(app);
  return status;
}
