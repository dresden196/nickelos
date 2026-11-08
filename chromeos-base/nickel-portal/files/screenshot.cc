// Distributed under the terms of the GNU General Public License v2
//
// Screenshot (org.freedesktop.impl.portal.Screenshot) for
// xdg-desktop-portal-nickel: apps that take a screenshot through the portal
// (GIMP's File > Create > Screenshot, screenshot tools, apps that attach
// one to a bug report) get a PNG of the screen or a window.
//
// An interactive screenshot: ChromeOS's own picker asks the user which
// screen or window, with the app's name, as for screen sharing
// (VmApplicationsService.NickelScreenCastChoose, Chrome patch 0022;
// canceling it answers the app "canceled"). A non-interactive one is the
// primary screen, without the picker (NickelScreenCastPrimary, Chrome patch
// 0096): xdg-desktop-portal asks before that, once, whether the app may take
// screenshots any time (the Access backend: Chrome's dialog), and remembers
// the answer, as GNOME's and KDE's portals do; screenshot tools (Flameshot,
// ksnip) take the screen that way and let the user pick the part in their
// own window. Chrome then copies the source once into shared memory this
// backend hands it (NickelScreenCastShot, Chrome patch 0065), after the
// picker has faded out and with no "sharing your screen" notification.
// The frame (BGRx) is saved as a PNG in the user's Pictures/Screenshots,
// "Screenshot from <date> <time>.png" as GNOME names them, and the app
// gets its file: URI.
//
// PickColor: ChromeOS's eyedropper, the one web pages' colour inputs use
// (VmApplicationsService.NickelPickColor, Chrome patch 0067), over the
// whole screen: a click answers the app with the colour (sRGB, 0 to 1), Esc
// with "canceled".

#include "screenshot.h"

#include <chromeos/dbus/vm_applications/dbus-constants.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gio/gdesktopappinfo.h>
#include <gio/gunixfdlist.h>
#include <glib/gstdio.h>
#include <sys/mman.h>
#include <syslog.h>
#include <unistd.h>

#include <climits>
#include <cstdlib>
#include <cstring>
#include <string>

namespace {

// Portal responses.
constexpr guint32 kSuccess = 0;
constexpr guint32 kCanceled = 1;
constexpr guint32 kFailed = 2;
// ScreenCast source types: a monitor and a window.
constexpr guint32 kMonitorOrWindow = 1 | 2;
// Chrome waits at most a second for the screen to be still, then copies:
// past this, give up.
constexpr gint kShotTimeoutMs = 10000;

std::string AppName(const std::string& app_id) {
  if (app_id.empty()) {
    return "";
  }
  std::string desktop_id = app_id + ".desktop";
  g_autoptr(GDesktopAppInfo) info = g_desktop_app_info_new(desktop_id.c_str());
  return info ? g_app_info_get_display_name(G_APP_INFO(info)) : app_id;
}

void Reply(GDBusMethodInvocation* invocation,
           guint32 response,
           GVariant* results) {
  g_dbus_method_invocation_return_value(
      invocation,
      g_variant_new("(u@a{sv})", response,
                    results ? results
                            : g_variant_new_array(G_VARIANT_TYPE("{sv}"),
                                                  nullptr, 0)));
}

}  // namespace

struct Screenshot {
  GDBusConnection* system = nullptr;
};

namespace {

// One Screenshot call, from the picker to the reply.
struct Shot {
  Screenshot* screenshot = nullptr;
  GDBusMethodInvocation* invocation = nullptr;
  std::string app_id;
  std::string source_id;
  gint32 width = 0;
  gint32 height = 0;
  guint8* frame = nullptr;
  size_t frame_size = 0;
};

// Answers the app.
void Finish(Shot* shot, guint32 response, GVariant* results) {
  if (shot->frame) {
    munmap(shot->frame, shot->frame_size);
  }
  Reply(shot->invocation, response, results);
  delete shot;
}

// The frame as a PNG in Pictures/Screenshots; its path, or "" on failure.
std::string Save(const Shot* shot) {
  const gchar* pictures = g_get_user_special_dir(G_USER_DIRECTORY_PICTURES);
  g_autofree gchar* dir = g_build_filename(
      pictures ? pictures : g_get_home_dir(), "Screenshots", nullptr);
  if (g_mkdir_with_parents(dir, 0755) < 0) {
    syslog(LOG_WARNING, "screenshot: cannot make %s: %m", dir);
    return "";
  }
  // The time zone as it is now: GLib keeps the local zone it first saw, and
  // on a PC's first session ChromeOS sets /etc/localtime (a link to
  // /var/lib/timezone/localtime) only after this backend started.
  g_autoptr(GTimeZone) zone = nullptr;
  g_autofree gchar* zone_file = realpath("/etc/localtime", nullptr);
  const gchar* zoneinfo = zone_file ? strstr(zone_file, "/zoneinfo/") : nullptr;
  if (zoneinfo) {
    zone = g_time_zone_new_identifier(zoneinfo + strlen("/zoneinfo/"));
  }
  g_autoptr(GDateTime) now =
      zone ? g_date_time_new_now(zone) : g_date_time_new_now_local();
  g_autofree gchar* stamp =
      g_date_time_format(now, "Screenshot from %Y-%m-%d %H-%M-%S");
  g_autofree gchar* path = nullptr;
  for (int n = 0; n < 100; n++) {
    g_autofree gchar* name =
        n ? g_strdup_printf("%s-%d.png", stamp, n) : g_strdup_printf("%s.png", stamp);
    g_free(path);
    path = g_build_filename(dir, name, nullptr);
    if (!g_file_test(path, G_FILE_TEST_EXISTS)) {
      break;
    }
  }

  // BGRx (little-endian 32-bit xRGB) to packed RGB.
  g_autoptr(GdkPixbuf) pixbuf = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8,
                                               shot->width, shot->height);
  if (!pixbuf) {
    return "";
  }
  guint8* out = gdk_pixbuf_get_pixels(pixbuf);
  int stride = gdk_pixbuf_get_rowstride(pixbuf);
  for (gint32 y = 0; y < shot->height; y++) {
    const guint8* in = shot->frame + static_cast<size_t>(y) * shot->width * 4;
    guint8* row = out + static_cast<size_t>(y) * stride;
    for (gint32 x = 0; x < shot->width; x++) {
      row[x * 3] = in[x * 4 + 2];
      row[x * 3 + 1] = in[x * 4 + 1];
      row[x * 3 + 2] = in[x * 4];
    }
  }
  g_autoptr(GError) error = nullptr;
  if (!gdk_pixbuf_save(pixbuf, path, "png", &error, nullptr)) {
    syslog(LOG_WARNING, "screenshot: cannot save %s: %s", path,
           error->message);
    return "";
  }
  return path;
}

void OnShotTaken(GObject* source, GAsyncResult* result, gpointer data) {
  Shot* shot = static_cast<Shot*>(data);
  g_autoptr(GError) error = nullptr;
  g_autoptr(GVariant) reply = g_dbus_connection_call_with_unix_fd_list_finish(
      G_DBUS_CONNECTION(source), nullptr, result, &error);
  gboolean taken = FALSE;
  if (reply) {
    g_variant_get(reply, "(b)", &taken);
  } else {
    syslog(LOG_WARNING, "screenshot: NickelScreenCastShot: %s",
           error->message);
  }
  if (!taken) {
    syslog(LOG_WARNING, "screenshot: no frame from Chrome");
    Finish(shot, kFailed, nullptr);
    return;
  }
  std::string path = Save(shot);
  if (path.empty()) {
    Finish(shot, kFailed, nullptr);
    return;
  }
  g_autofree gchar* uri = g_filename_to_uri(path.c_str(), nullptr, nullptr);
  syslog(LOG_INFO, "screenshot: %s took %dx%d of %s",
         AppName(shot->app_id).c_str(), shot->width, shot->height,
         shot->source_id.c_str());
  GVariantBuilder results;
  g_variant_builder_init(&results, G_VARIANT_TYPE("a{sv}"));
  g_variant_builder_add(&results, "{sv}", "uri",
                        g_variant_new_string(uri ? uri : ""));
  Finish(shot, kSuccess, g_variant_builder_end(&results));
}

void OnSourceChosen(GObject* source, GAsyncResult* result, gpointer data) {
  Shot* shot = static_cast<Shot*>(data);
  g_autoptr(GError) error = nullptr;
  g_autoptr(GVariant) reply = g_dbus_connection_call_finish(
      G_DBUS_CONNECTION(source), result, &error);
  const gchar* source_id = "";
  gint32 width = 0;
  gint32 height = 0;
  if (reply) {
    g_variant_get(reply, "(&sii)", &source_id, &width, &height);
  } else {
    syslog(LOG_WARNING, "screenshot: the source (picker or primary screen): %s",
           error->message);
  }
  if (!*source_id || width <= 0 || height <= 0) {
    // The user canceled (or Chrome could not ask).
    Finish(shot, reply ? kCanceled : kFailed, nullptr);
    return;
  }
  shot->source_id = source_id;
  shot->width = width;
  shot->height = height;

  // One frame of shared memory for Chrome to copy the source into.
  shot->frame_size = static_cast<size_t>(width) * height * 4;
  int frame_fd = memfd_create("nickel-screenshot", MFD_CLOEXEC);
  if (frame_fd < 0 || ftruncate(frame_fd, shot->frame_size) < 0) {
    syslog(LOG_ERR, "screenshot: cannot make the frame buffer: %m");
    if (frame_fd >= 0) {
      close(frame_fd);
    }
    Finish(shot, kFailed, nullptr);
    return;
  }
  void* frame = mmap(nullptr, shot->frame_size, PROT_READ, MAP_SHARED,
                     frame_fd, 0);
  shot->frame = frame == MAP_FAILED ? nullptr : static_cast<guint8*>(frame);
  g_autoptr(GUnixFDList) fds = g_unix_fd_list_new();
  gint frame_index = g_unix_fd_list_append(fds, frame_fd, nullptr);
  close(frame_fd);
  if (!shot->frame) {
    Finish(shot, kFailed, nullptr);
    return;
  }
  g_dbus_connection_call_with_unix_fd_list(
      shot->screenshot->system, vm_tools::apps::kVmApplicationsServiceName,
      vm_tools::apps::kVmApplicationsServicePath,
      vm_tools::apps::kVmApplicationsServiceInterface, "NickelScreenCastShot",
      g_variant_new("(shuu)", shot->source_id.c_str(), frame_index,
                    static_cast<guint32>(width), static_cast<guint32>(height)),
      G_VARIANT_TYPE("(b)"), G_DBUS_CALL_FLAGS_NONE, kShotTimeoutMs, fds,
      nullptr, OnShotTaken, shot);
}

// Screenshot(o handle, s app_id, s parent_window, a{sv} options)
//   -> (u response, a{sv} results{uri})
void TakeScreenshot(Screenshot* screenshot,
                    GDBusMethodInvocation* invocation,
                    GVariant* parameters) {
  const gchar* handle = nullptr;
  const gchar* app_id = nullptr;
  const gchar* parent_window = nullptr;
  g_autoptr(GVariant) options = nullptr;
  g_variant_get(parameters, "(&o&s&s@a{sv})", &handle, &app_id,
                &parent_window, &options);
  if (!screenshot->system) {
    Reply(invocation, kFailed, nullptr);
    return;
  }
  Shot* shot = new Shot;
  shot->screenshot = screenshot;
  shot->invocation = invocation;
  shot->app_id = app_id;
  gboolean interactive = FALSE;
  g_variant_lookup(options, "interactive", "b", &interactive);
  if (!interactive) {
    // The app may take screenshots any time (xdg-desktop-portal asked):
    // the primary screen, no picker.
    g_dbus_connection_call(
        screenshot->system, vm_tools::apps::kVmApplicationsServiceName,
        vm_tools::apps::kVmApplicationsServicePath,
        vm_tools::apps::kVmApplicationsServiceInterface,
        "NickelScreenCastPrimary", nullptr, G_VARIANT_TYPE("(sii)"),
        G_DBUS_CALL_FLAGS_NONE, -1, nullptr, OnSourceChosen, shot);
    return;
  }
  // Chrome's picker waits for the user: no timeout.
  g_dbus_connection_call(
      screenshot->system, vm_tools::apps::kVmApplicationsServiceName,
      vm_tools::apps::kVmApplicationsServicePath,
      vm_tools::apps::kVmApplicationsServiceInterface,
      "NickelScreenCastChoose",
      g_variant_new("(su)", AppName(shot->app_id).c_str(), kMonitorOrWindow),
      G_VARIANT_TYPE("(sii)"), G_DBUS_CALL_FLAGS_NONE, G_MAXINT, nullptr,
      OnSourceChosen, shot);
}

struct Pick {
  GDBusMethodInvocation* invocation;
  std::string app_id;
};

void OnColorPicked(GObject* source, GAsyncResult* result, gpointer data) {
  Pick* pick = static_cast<Pick*>(data);
  g_autoptr(GError) error = nullptr;
  g_autoptr(GVariant) reply =
      g_dbus_connection_call_finish(G_DBUS_CONNECTION(source), result, &error);
  if (!reply) {
    syslog(LOG_WARNING, "screenshot: NickelPickColor: %s", error->message);
    Reply(pick->invocation, kFailed, nullptr);
    delete pick;
    return;
  }
  gboolean picked = FALSE;
  guint32 argb = 0;
  g_variant_get(reply, "(bu)", &picked, &argb);
  if (!picked) {
    Reply(pick->invocation, kCanceled, nullptr);
    delete pick;
    return;
  }
  const double red = ((argb >> 16) & 0xff) / 255.0;
  const double green = ((argb >> 8) & 0xff) / 255.0;
  const double blue = (argb & 0xff) / 255.0;
  syslog(LOG_INFO, "screenshot: %s picked #%06x", pick->app_id.c_str(),
         argb & 0xffffff);
  GVariantBuilder results;
  g_variant_builder_init(&results, G_VARIANT_TYPE("a{sv}"));
  g_variant_builder_add(&results, "{sv}", "color",
                        g_variant_new("(ddd)", red, green, blue));
  Reply(pick->invocation, kSuccess, g_variant_builder_end(&results));
  delete pick;
}

// PickColor(o handle, s app_id, s parent_window, a{sv} options)
//   -> (u response, a{sv} results{color (ddd)})
void PickColor(Screenshot* screenshot,
               GDBusMethodInvocation* invocation,
               GVariant* parameters) {
  const gchar* app_id = nullptr;
  g_variant_get(parameters, "(&o&s&s@a{sv})", nullptr, &app_id, nullptr,
                nullptr);
  if (!screenshot->system) {
    Reply(invocation, kFailed, nullptr);
    return;
  }
  // The user picks: no timeout.
  g_dbus_connection_call(
      screenshot->system, vm_tools::apps::kVmApplicationsServiceName,
      vm_tools::apps::kVmApplicationsServicePath,
      vm_tools::apps::kVmApplicationsServiceInterface, "NickelPickColor",
      nullptr, G_VARIANT_TYPE("(bu)"), G_DBUS_CALL_FLAGS_NONE, G_MAXINT,
      nullptr, OnColorPicked, new Pick{invocation, app_id});
}

}  // namespace

Screenshot* ScreenshotNew(GDBusConnection* system) {
  Screenshot* screenshot = new Screenshot;
  screenshot->system = system;
  return screenshot;
}

void OnScreenshotCall(GDBusConnection*,
                      const gchar*,
                      const gchar*,
                      const gchar*,
                      const gchar* method_name,
                      GVariant* parameters,
                      GDBusMethodInvocation* invocation,
                      gpointer data) {
  Screenshot* screenshot = static_cast<Screenshot*>(data);
  if (g_strcmp0(method_name, "Screenshot") == 0) {
    TakeScreenshot(screenshot, invocation, parameters);
  } else if (g_strcmp0(method_name, "PickColor") == 0) {
    PickColor(screenshot, invocation, parameters);
  } else {
    g_dbus_method_invocation_return_error(invocation, G_DBUS_ERROR,
                                          G_DBUS_ERROR_UNKNOWN_METHOD,
                                          "unknown method %s", method_name);
  }
}

GVariant* OnScreenshotProperty(GDBusConnection*,
                               const gchar*,
                               const gchar*,
                               const gchar*,
                               const gchar* property_name,
                               GError** error,
                               gpointer) {
  if (g_strcmp0(property_name, "version") == 0) {
    return g_variant_new_uint32(2);
  }
  g_set_error(error, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_PROPERTY,
              "unknown property %s", property_name);
  return nullptr;
}
