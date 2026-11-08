// Distributed under the terms of the GNU General Public License v2
//
// Notifications for xdg-desktop-portal-nickel: Linux apps' notifications as
// ChromeOS's own.
//
// org.freedesktop.Notifications (Desktop Notifications Specification 1.2),
// for native apps (GNotification outside a sandbox, libnotify, Electron,
// Valent's phone notifications): Notify, CloseNotification,
// GetCapabilities, GetServerInformation; the NotificationClosed,
// ActionInvoked and ActivationToken signals, to the app that sent it.
// org.freedesktop.impl.portal.Notification, for Flatpak apps:
// AddNotification, RemoveNotification and the ActionInvoked signal.
//
// Each becomes a ChromeOS notification through Chrome (patch 0027:
// vm_applications' NickelNotify(key, app_id, app_name, title, body, icon,
// buttons, urgent) and NickelNotifyClose(key)), under the app's name. Chrome
// reports what the user did through nickel-appd's NotificationEvent(key,
// event, button, token) signal: "clicked" (button -1 for the notification
// itself) with an xdg-activation token, so that the app may bring a window
// forward, or "closed" (button 1 when the user dismissed it).
//
// The icon is the notification's image (image-data, image-path or app_icon
// hints; a portal notification's GIcon), or else the app's own (the Icon of
// its desktop file), looked up in the icon themes and sent as PNG.

#include "notifications.h"

#include <chromeos/dbus/vm_applications/dbus-constants.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gio/gdesktopappinfo.h>
#include <syslog.h>

#include <algorithm>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace {

constexpr char kFdoName[] = "org.freedesktop.Notifications";
constexpr char kFdoPath[] = "/org/freedesktop/Notifications";
constexpr char kFdoInterface[] = "org.freedesktop.Notifications";
constexpr char kPortalPath[] = "/org/freedesktop/portal/desktop";
constexpr char kPortalInterface[] =
    "org.freedesktop.impl.portal.Notification";
constexpr char kAppsName[] = "org.nickelos.Apps";
constexpr char kAppsPath[] = "/org/nickelos/Apps";
constexpr int kIconSize = 128;

// NotificationClosed reasons.
constexpr guint32 kExpired = 1;
constexpr guint32 kDismissed = 2;
constexpr guint32 kClosedByCall = 3;

constexpr char kFdoIntrospection[] =
    "<node>"
    "  <interface name='org.freedesktop.Notifications'>"
    "    <method name='Notify'>"
    "      <arg type='s' name='app_name' direction='in'/>"
    "      <arg type='u' name='replaces_id' direction='in'/>"
    "      <arg type='s' name='app_icon' direction='in'/>"
    "      <arg type='s' name='summary' direction='in'/>"
    "      <arg type='s' name='body' direction='in'/>"
    "      <arg type='as' name='actions' direction='in'/>"
    "      <arg type='a{sv}' name='hints' direction='in'/>"
    "      <arg type='i' name='expire_timeout' direction='in'/>"
    "      <arg type='u' name='id' direction='out'/>"
    "    </method>"
    "    <method name='CloseNotification'>"
    "      <arg type='u' name='id' direction='in'/>"
    "    </method>"
    "    <method name='GetCapabilities'>"
    "      <arg type='as' name='capabilities' direction='out'/>"
    "    </method>"
    "    <method name='GetServerInformation'>"
    "      <arg type='s' name='name' direction='out'/>"
    "      <arg type='s' name='vendor' direction='out'/>"
    "      <arg type='s' name='version' direction='out'/>"
    "      <arg type='s' name='spec_version' direction='out'/>"
    "    </method>"
    "    <signal name='NotificationClosed'>"
    "      <arg type='u' name='id'/>"
    "      <arg type='u' name='reason'/>"
    "    </signal>"
    "    <signal name='ActionInvoked'>"
    "      <arg type='u' name='id'/>"
    "      <arg type='s' name='action_key'/>"
    "    </signal>"
    "    <signal name='ActivationToken'>"
    "      <arg type='u' name='id'/>"
    "      <arg type='s' name='activation_token'/>"
    "    </signal>"
    "  </interface>"
    "</node>";

// A notification on ChromeOS, from either interface.
struct Note {
  // org.freedesktop.Notifications: its id and the app's bus name; the
  // action keys of its buttons, in order, and whether it has "default".
  guint32 id = 0;
  std::string sender;
  std::vector<std::string> button_keys;
  bool has_default = false;
  // Notification portal: the app and its id for it; the actions (and
  // targets) of the notification itself and of its buttons.
  bool portal = false;
  std::string app_id;
  std::string portal_id;
  std::string default_action;
  GVariant* default_target = nullptr;
  std::vector<std::pair<std::string, GVariant*>> button_actions;

  ~Note() {
    if (default_target) {
      g_variant_unref(default_target);
    }
    for (auto& b : button_actions) {
      if (b.second) {
        g_variant_unref(b.second);
      }
    }
  }
};

}  // namespace

struct Notifications {
  GDBusConnection* session = nullptr;
  GDBusConnection* system = nullptr;
  guint32 next_id = 1;
  // By the key Chrome knows it by.
  std::map<std::string, std::unique_ptr<Note>> notes;
};

namespace {

// --- Icons -----------------------------------------------------------------

// A themed icon's file: hicolor (where apps and Flatpak exports put theirs)
// and Adwaita, large sizes first, then pixmaps.
std::string FindIcon(const std::string& name) {
  if (name.empty()) {
    return std::string();
  }
  if (name[0] == '/') {
    return g_file_test(name.c_str(), G_FILE_TEST_EXISTS) ? name
                                                          : std::string();
  }
  if (g_str_has_prefix(name.c_str(), "file://")) {
    g_autofree gchar* path =
        g_filename_from_uri(name.c_str(), nullptr, nullptr);
    return path ? FindIcon(path) : std::string();
  }
  std::vector<std::string> dirs = {g_get_user_data_dir()};
  for (const gchar* const* d = g_get_system_data_dirs(); *d; ++d) {
    dirs.push_back(*d);
  }
  static const char* const kThemes[] = {"hicolor", "Adwaita"};
  static const char* const kSizes[] = {"256x256", "128x128", "scalable",
                                       "96x96",   "64x64",   "512x512",
                                       "48x48",   "32x32"};
  static const char* const kContexts[] = {
      "apps",    "status", "devices", "categories", "actions",
      "emblems", "places", "mimetypes"};
  for (const auto& dir : dirs) {
    for (const char* theme : kThemes) {
      for (const char* size : kSizes) {
        for (const char* context : kContexts) {
          for (const char* ext : {".png", ".svg"}) {
            std::string path = dir + "/icons/" + theme + "/" + size + "/" +
                               context + "/" + name + ext;
            if (g_file_test(path.c_str(), G_FILE_TEST_EXISTS)) {
              return path;
            }
          }
        }
      }
    }
    for (const char* ext : {".png", ".svg"}) {
      std::string path = dir + "/pixmaps/" + name + ext;
      if (g_file_test(path.c_str(), G_FILE_TEST_EXISTS)) {
        return path;
      }
    }
  }
  return std::string();
}

// The PNG Chrome gets, at most kIconSize square.
GBytes* PngOf(GdkPixbuf* pixbuf) {
  if (!pixbuf) {
    return nullptr;
  }
  int w = gdk_pixbuf_get_width(pixbuf), h = gdk_pixbuf_get_height(pixbuf);
  g_autoptr(GdkPixbuf) scaled = nullptr;
  if (w > kIconSize || h > kIconSize) {
    double s = static_cast<double>(kIconSize) / std::max(w, h);
    scaled = gdk_pixbuf_scale_simple(pixbuf, std::max(1, int(w * s)),
                                     std::max(1, int(h * s)),
                                     GDK_INTERP_BILINEAR);
    pixbuf = scaled;
  }
  gchar* buffer = nullptr;
  gsize size = 0;
  if (!gdk_pixbuf_save_to_buffer(pixbuf, &buffer, &size, "png", nullptr,
                                 nullptr)) {
    return nullptr;
  }
  return g_bytes_new_take(buffer, size);
}

GBytes* PngOfFile(const std::string& path) {
  if (path.empty()) {
    return nullptr;
  }
  g_autoptr(GdkPixbuf) pixbuf = gdk_pixbuf_new_from_file_at_scale(
      path.c_str(), kIconSize, kIconSize, TRUE, nullptr);
  return PngOf(pixbuf);
}

GBytes* PngOfData(GBytes* data) {
  g_autoptr(GInputStream) stream = g_memory_input_stream_new_from_bytes(data);
  g_autoptr(GdkPixbuf) pixbuf = gdk_pixbuf_new_from_stream_at_scale(
      stream, kIconSize, kIconSize, TRUE, nullptr, nullptr);
  return PngOf(pixbuf);
}

// The image-data hint: (iiibiiay) width, height, rowstride, has_alpha,
// bits per sample, channels, pixels.
GBytes* PngOfImageData(GVariant* value) {
  if (!g_variant_is_of_type(value, G_VARIANT_TYPE("(iiibiiay)"))) {
    return nullptr;
  }
  gint32 width, height, rowstride, bits, channels;
  gboolean alpha;
  g_autoptr(GVariant) data_value = nullptr;
  g_variant_get(value, "(iiibii@ay)", &width, &height, &rowstride, &alpha,
                &bits, &channels, &data_value);
  gsize size = 0;
  const guchar* data = static_cast<const guchar*>(
      g_variant_get_fixed_array(data_value, &size, 1));
  if (bits != 8 || width <= 0 || height <= 0 ||
      size < gsize(rowstride) * (height - 1) + width * channels) {
    return nullptr;
  }
  g_autoptr(GBytes) pixels = g_bytes_new(data, size);
  g_autoptr(GdkPixbuf) pixbuf = gdk_pixbuf_new_from_bytes(
      pixels, GDK_COLORSPACE_RGB, alpha, bits, width, height, rowstride);
  return PngOf(pixbuf);
}

GBytes* PngOfGIcon(GIcon* icon) {
  if (!icon) {
    return nullptr;
  }
  if (G_IS_BYTES_ICON(icon)) {
    return PngOfData(g_bytes_icon_get_bytes(G_BYTES_ICON(icon)));
  }
  if (G_IS_FILE_ICON(icon)) {
    g_autofree gchar* path = g_file_get_path(g_file_icon_get_file(
        G_FILE_ICON(icon)));
    return path ? PngOfFile(path) : nullptr;
  }
  if (G_IS_THEMED_ICON(icon)) {
    for (const gchar* const* n =
             g_themed_icon_get_names(G_THEMED_ICON(icon));
         n && *n; ++n) {
      if (GBytes* png = PngOfFile(FindIcon(*n))) {
        return png;
      }
    }
  }
  return nullptr;
}

// The app's name and icon, from its desktop file.
struct AppInfo {
  std::string name;
  GBytes* icon = nullptr;
};

AppInfo LookUpApp(const std::string& app_id, const std::string& fallback) {
  AppInfo info{fallback, nullptr};
  if (app_id.empty()) {
    return info;
  }
  std::string file = app_id;
  if (!g_str_has_suffix(file.c_str(), ".desktop")) {
    file += ".desktop";
  }
  g_autoptr(GDesktopAppInfo) app = g_desktop_app_info_new(file.c_str());
  if (!app) {
    return info;
  }
  info.name = g_app_info_get_display_name(G_APP_INFO(app));
  info.icon = PngOfGIcon(g_app_info_get_icon(G_APP_INFO(app)));
  return info;
}

// Markup is not shown: tags go, entities become their characters.
std::string PlainText(const char* text) {
  std::string out;
  bool tag = false;
  for (const char* p = text; *p; ++p) {
    if (*p == '<') {
      tag = true;
    } else if (*p == '>' && tag) {
      tag = false;
    } else if (!tag) {
      out += *p;
    }
  }
  static const std::pair<const char*, const char*> kEntities[] = {
      {"&lt;", "<"},   {"&gt;", ">"},    {"&quot;", "\""},
      {"&apos;", "'"}, {"&#39;", "'"},   {"&amp;", "&"}};
  for (const auto& [entity, character] : kEntities) {
    size_t at;
    while ((at = out.find(entity)) != std::string::npos) {
      out.replace(at, strlen(entity), character);
    }
  }
  return out;
}

// --- Chrome ----------------------------------------------------------------

void ShowOnChromeOS(Notifications* n,
                    const std::string& key,
                    const std::string& app_id,
                    const std::string& app_name,
                    const std::string& title,
                    const std::string& body,
                    GBytes* icon,
                    const std::vector<std::string>& buttons,
                    bool urgent) {
  if (!n->system) {
    return;
  }
  GVariantBuilder labels;
  g_variant_builder_init(&labels, G_VARIANT_TYPE("as"));
  for (const auto& label : buttons) {
    g_variant_builder_add(&labels, "s", label.c_str());
  }
  g_autoptr(GBytes) empty = g_bytes_new(nullptr, 0);
  GVariant* icon_value = g_variant_new_from_bytes(
      G_VARIANT_TYPE("ay"), icon ? icon : empty, TRUE);
  g_dbus_connection_call(
      n->system, vm_tools::apps::kVmApplicationsServiceName,
      vm_tools::apps::kVmApplicationsServicePath,
      vm_tools::apps::kVmApplicationsServiceInterface, "NickelNotify",
      g_variant_new("(sssss@ayasb)", key.c_str(), app_id.c_str(),
                    app_name.c_str(), title.c_str(), body.c_str(), icon_value,
                    &labels, urgent),
      nullptr, G_DBUS_CALL_FLAGS_NONE, -1, nullptr,
      [](GObject* source, GAsyncResult* result, gpointer) {
        g_autoptr(GError) error = nullptr;
        g_autoptr(GVariant) reply = g_dbus_connection_call_finish(
            G_DBUS_CONNECTION(source), result, &error);
        if (!reply) {
          syslog(LOG_WARNING, "NickelNotify: %s", error->message);
        }
      },
      nullptr);
}

void CloseOnChromeOS(Notifications* n, const std::string& key) {
  if (!n->system) {
    return;
  }
  g_dbus_connection_call(
      n->system, vm_tools::apps::kVmApplicationsServiceName,
      vm_tools::apps::kVmApplicationsServicePath,
      vm_tools::apps::kVmApplicationsServiceInterface, "NickelNotifyClose",
      g_variant_new("(s)", key.c_str()), nullptr, G_DBUS_CALL_FLAGS_NONE, -1,
      nullptr, nullptr, nullptr);
}

void EmitFdo(Notifications* n,
             const Note& note,
             const char* signal,
             GVariant* params) {
  g_dbus_connection_emit_signal(n->session, note.sender.c_str(), kFdoPath,
                                kFdoInterface, signal, params, nullptr);
}

// Chrome, through nickel-appd: what the user did with a notification.
void OnNotificationEvent(GDBusConnection*,
                         const gchar*,
                         const gchar*,
                         const gchar*,
                         const gchar*,
                         GVariant* params,
                         gpointer user_data) {
  auto* n = static_cast<Notifications*>(user_data);
  const gchar *key, *event, *token;
  gint32 button;
  g_variant_get(params, "(&s&si&s)", &key, &event, &button, &token);
  auto it = n->notes.find(key);
  if (it == n->notes.end()) {
    return;
  }
  Note& note = *it->second;
  if (g_str_equal(event, "closed")) {
    if (!note.portal) {
      EmitFdo(n, note, "NotificationClosed",
              g_variant_new("(uu)", note.id,
                            button == 1 ? kDismissed : kExpired));
    }
    n->notes.erase(it);
    return;
  }
  if (!g_str_equal(event, "clicked")) {
    return;
  }
  if (!note.portal) {
    const char* action = nullptr;
    if (button < 0 && note.has_default) {
      action = "default";
    } else if (button >= 0 && size_t(button) < note.button_keys.size()) {
      action = note.button_keys[button].c_str();
    }
    if (action) {
      if (*token) {
        EmitFdo(n, note, "ActivationToken",
                g_variant_new("(us)", note.id, token));
      }
      EmitFdo(n, note, "ActionInvoked", g_variant_new("(us)", note.id, action));
    }
    return;
  }
  const std::string* action = nullptr;
  GVariant* target = nullptr;
  if (button < 0 && !note.default_action.empty()) {
    action = &note.default_action;
    target = note.default_target;
  } else if (button >= 0 && size_t(button) < note.button_actions.size()) {
    action = &note.button_actions[button].first;
    target = note.button_actions[button].second;
  }
  if (!action) {
    return;
  }
  GVariantBuilder parameter;
  g_variant_builder_init(&parameter, G_VARIANT_TYPE("av"));
  if (target) {
    g_variant_builder_add(&parameter, "v", target);
  }
  g_dbus_connection_emit_signal(
      n->session, nullptr, kPortalPath, kPortalInterface, "ActionInvoked",
      g_variant_new("(sssav)", note.app_id.c_str(), note.portal_id.c_str(),
                    action->c_str(), &parameter),
      nullptr);
}

// --- org.freedesktop.Notifications -------------------------------------------

std::string FdoKey(guint32 id) {
  return "fdo-" + std::to_string(id);
}

void HandleNotify(Notifications* n,
                  const gchar* sender,
                  GVariant* params,
                  GDBusMethodInvocation* invocation) {
  const gchar *app_name, *app_icon, *summary, *body;
  guint32 replaces_id;
  gint32 timeout;
  g_autoptr(GVariant) actions = nullptr;
  g_autoptr(GVariant) hints = nullptr;
  g_variant_get(params, "(&su&s&s&s@as@a{sv}i)", &app_name, &replaces_id,
                &app_icon, &summary, &body, &actions, &hints, &timeout);

  guint32 id = replaces_id;
  if (!id || !n->notes.count(FdoKey(id))) {
    id = n->next_id++;
  }
  auto note = std::make_unique<Note>();
  note->id = id;
  note->sender = sender;
  std::vector<std::string> labels;
  gsize count = 0;
  const gchar** strv = g_variant_get_strv(actions, &count);
  for (gsize i = 0; i + 1 < count; i += 2) {
    if (g_str_equal(strv[i], "default")) {
      note->has_default = true;
    } else {
      note->button_keys.push_back(strv[i]);
      labels.push_back(strv[i + 1]);
    }
  }
  g_free(strv);

  const gchar* desktop_entry = nullptr;
  g_variant_lookup(hints, "desktop-entry", "&s", &desktop_entry);
  guchar urgency = 1;
  g_variant_lookup(hints, "urgency", "y", &urgency);
  AppInfo app = LookUpApp(desktop_entry ? desktop_entry : "", app_name);

  GBytes* icon = nullptr;
  for (const char* key : {"image-data", "image_data", "icon_data"}) {
    g_autoptr(GVariant) value = g_variant_lookup_value(hints, key, nullptr);
    if (value && (icon = PngOfImageData(value))) {
      break;
    }
  }
  const gchar* image_path = nullptr;
  if (!icon && (g_variant_lookup(hints, "image-path", "&s", &image_path) ||
                g_variant_lookup(hints, "image_path", "&s", &image_path))) {
    icon = PngOfFile(FindIcon(image_path));
  }
  if (!icon && *app_icon) {
    icon = PngOfFile(FindIcon(app_icon));
  }
  if (!icon) {
    icon = app.icon;
    app.icon = nullptr;
  }

  const std::string key = FdoKey(id);
  ShowOnChromeOS(n, key, desktop_entry ? desktop_entry : "", app.name,
                 PlainText(summary), PlainText(body), icon, labels,
                 urgency >= 2);
  if (icon) {
    g_bytes_unref(icon);
  }
  if (app.icon) {
    g_bytes_unref(app.icon);
  }
  n->notes[key] = std::move(note);
  g_dbus_method_invocation_return_value(invocation,
                                        g_variant_new("(u)", id));
}

void OnFdoCall(GDBusConnection*,
               const gchar* sender,
               const gchar*,
               const gchar*,
               const gchar* method,
               GVariant* params,
               GDBusMethodInvocation* invocation,
               gpointer user_data) {
  auto* n = static_cast<Notifications*>(user_data);
  if (g_str_equal(method, "Notify")) {
    return HandleNotify(n, sender, params, invocation);
  }
  if (g_str_equal(method, "CloseNotification")) {
    guint32 id;
    g_variant_get(params, "(u)", &id);
    auto it = n->notes.find(FdoKey(id));
    if (it != n->notes.end()) {
      CloseOnChromeOS(n, it->first);
      EmitFdo(n, *it->second, "NotificationClosed",
              g_variant_new("(uu)", id, kClosedByCall));
      n->notes.erase(it);
    }
    return g_dbus_method_invocation_return_value(invocation, nullptr);
  }
  if (g_str_equal(method, "GetCapabilities")) {
    const gchar* caps[] = {"actions", "body", "icon-static", "persistence",
                           nullptr};
    return g_dbus_method_invocation_return_value(
        invocation, g_variant_new("(^as)", caps));
  }
  if (g_str_equal(method, "GetServerInformation")) {
    return g_dbus_method_invocation_return_value(
        invocation,
        g_variant_new("(ssss)", "NickelOS", "NickelOS", "1.0", "1.2"));
  }
  g_dbus_method_invocation_return_error(invocation, G_DBUS_ERROR,
                                        G_DBUS_ERROR_UNKNOWN_METHOD,
                                        "unknown method %s", method);
}

void OnFdoNameLost(GDBusConnection*, const gchar* name, gpointer) {
  syslog(LOG_WARNING, "could not own %s", name);
}

}  // namespace

// --- org.freedesktop.impl.portal.Notification ----------------------------------

void OnNotificationPortalCall(GDBusConnection*,
                              const gchar*,
                              const gchar*,
                              const gchar*,
                              const gchar* method,
                              GVariant* params,
                              GDBusMethodInvocation* invocation,
                              gpointer user_data) {
  auto* n = static_cast<Notifications*>(user_data);
  if (g_str_equal(method, "AddNotification")) {
    const gchar *app_id, *id;
    g_autoptr(GVariant) notification = nullptr;
    g_variant_get(params, "(&s&s@a{sv})", &app_id, &id, &notification);
    auto note = std::make_unique<Note>();
    note->portal = true;
    note->app_id = app_id;
    note->portal_id = id;
    const gchar *title = "", *body = "", *priority = "normal";
    g_variant_lookup(notification, "title", "&s", &title);
    g_variant_lookup(notification, "body", "&s", &body);
    g_variant_lookup(notification, "priority", "&s", &priority);
    const gchar* default_action = nullptr;
    if (g_variant_lookup(notification, "default-action", "&s",
                         &default_action)) {
      note->default_action = default_action;
      note->default_target = g_variant_lookup_value(
          notification, "default-action-target", nullptr);
    }
    std::vector<std::string> labels;
    g_autoptr(GVariant) buttons = g_variant_lookup_value(
        notification, "buttons", G_VARIANT_TYPE("aa{sv}"));
    if (buttons) {
      for (gsize i = 0; i < g_variant_n_children(buttons); ++i) {
        g_autoptr(GVariant) button = g_variant_get_child_value(buttons, i);
        const gchar *label = "", *action = "";
        g_variant_lookup(button, "label", "&s", &label);
        g_variant_lookup(button, "action", "&s", &action);
        labels.push_back(label);
        note->button_actions.emplace_back(
            action, g_variant_lookup_value(button, "target", nullptr));
      }
    }
    AppInfo app = LookUpApp(app_id, app_id);
    GBytes* icon = nullptr;
    g_autoptr(GVariant) icon_value =
        g_variant_lookup_value(notification, "icon", nullptr);
    if (icon_value) {
      g_autoptr(GIcon) gicon = g_icon_deserialize(icon_value);
      icon = PngOfGIcon(gicon);
    }
    if (!icon) {
      icon = app.icon;
      app.icon = nullptr;
    }
    const std::string key = std::string("portal-") + app_id + "-" + id;
    ShowOnChromeOS(n, key, app_id, app.name, PlainText(title),
                   PlainText(body), icon, labels,
                   g_str_equal(priority, "urgent"));
    if (icon) {
      g_bytes_unref(icon);
    }
    if (app.icon) {
      g_bytes_unref(app.icon);
    }
    n->notes[key] = std::move(note);
    return g_dbus_method_invocation_return_value(invocation, nullptr);
  }
  if (g_str_equal(method, "RemoveNotification")) {
    const gchar *app_id, *id;
    g_variant_get(params, "(&s&s)", &app_id, &id);
    const std::string key = std::string("portal-") + app_id + "-" + id;
    if (n->notes.erase(key)) {
      CloseOnChromeOS(n, key);
    }
    return g_dbus_method_invocation_return_value(invocation, nullptr);
  }
  g_dbus_method_invocation_return_error(invocation, G_DBUS_ERROR,
                                        G_DBUS_ERROR_UNKNOWN_METHOD,
                                        "unknown method %s", method);
}

GVariant* OnNotificationPortalProperty(GDBusConnection*,
                                       const gchar*,
                                       const gchar*,
                                       const gchar*,
                                       const gchar* property,
                                       GError** error,
                                       gpointer) {
  if (g_str_equal(property, "version")) {
    return g_variant_new_uint32(1);
  }
  g_set_error(error, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_PROPERTY,
              "unknown property %s", property);
  return nullptr;
}

Notifications* NotificationsNew(GDBusConnection* session,
                                GDBusConnection* system) {
  auto* n = new Notifications;
  n->session = session;
  n->system = system;
  if (system) {
    g_dbus_connection_signal_subscribe(
        system, kAppsName, kAppsName, "NotificationEvent", kAppsPath, nullptr,
        G_DBUS_SIGNAL_FLAGS_NONE, OnNotificationEvent, n, nullptr);
  }
  GDBusNodeInfo* node =
      g_dbus_node_info_new_for_xml(kFdoIntrospection, nullptr);
  static const GDBusInterfaceVTable kVTable = {OnFdoCall, nullptr, nullptr};
  g_autoptr(GError) error = nullptr;
  if (!g_dbus_connection_register_object(session, kFdoPath,
                                         node->interfaces[0], &kVTable, n,
                                         nullptr, &error)) {
    syslog(LOG_ERR, "cannot export %s: %s", kFdoInterface, error->message);
  }
  g_bus_own_name_on_connection(session, kFdoName, G_BUS_NAME_OWNER_FLAGS_NONE,
                               nullptr, OnFdoNameLost, nullptr, nullptr);
  return n;
}
