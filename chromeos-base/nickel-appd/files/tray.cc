// Distributed under the terms of the GNU General Public License v2
//
// Linux apps' tray icons on ChromeOS's shelf, for nickel-appd.
//
// Apps that live in the system tray (Discord, Slack, Steam, Telegram...)
// keep running when their window closes and come back through their tray
// icon. ChromeOS has no tray for apps; its shelf is where running apps are.
// So nickel-appd is the tray Linux apps look for: it owns
// org.kde.StatusNotifierWatcher (the StatusNotifierItem specification,
// which Electron, Qt, libappindicator/libayatana and Flatpak apps use; also
// as org.freedesktop.StatusNotifierWatcher) and is its host. For each item
// it finds the app (a Flatpak's id from /.flatpak-info, the desktop file
// GLib launched it from, else its name or executable matched against the
// installed desktop files), reads its status and its menu
// (com.canonical.dbusmenu), and tells Chrome (patch 0045: vm_applications'
// NickelTrayItem(key, app_id, title, attention, menu as JSON) and
// NickelTrayItemRemoved(key)). Chrome keeps the app on the shelf, running,
// while it has an item, puts the menu in the app's shelf menu and a badge on
// it when it needs attention, and reports clicks back (TrayEvent).

#include "tray.h"

#include <gio/gdesktopappinfo.h>
#include <glib.h>
#include <signal.h>
#include <syslog.h>
#include <unistd.h>

#include <cstring>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <chromeos/dbus/vm_applications/dbus-constants.h>

namespace {

constexpr char kWatcherPath[] = "/StatusNotifierWatcher";
constexpr char kKdeWatcher[] = "org.kde.StatusNotifierWatcher";
constexpr char kFdoWatcher[] = "org.freedesktop.StatusNotifierWatcher";
constexpr char kDefaultItemPath[] = "/StatusNotifierItem";
constexpr char kKdeItem[] = "org.kde.StatusNotifierItem";
constexpr char kFdoItem[] = "org.freedesktop.StatusNotifierItem";
constexpr char kMenuInterface[] = "com.canonical.dbusmenu";
constexpr char kChromeOSHandlerId[] = "org.nickelos.ChromeOS";
// Signals come in bursts (a new icon, title and status at once).
constexpr guint kRefreshDelayMs = 100;

constexpr char kWatcherInterfaceXml[] =
    "  <interface name='NAME'>"
    "    <method name='RegisterStatusNotifierItem'>"
    "      <arg type='s' name='service' direction='in'/>"
    "    </method>"
    "    <method name='RegisterStatusNotifierHost'>"
    "      <arg type='s' name='service' direction='in'/>"
    "    </method>"
    "    <property name='RegisteredStatusNotifierItems' type='as'"
    "              access='read'/>"
    "    <property name='IsStatusNotifierHostRegistered' type='b'"
    "              access='read'/>"
    "    <property name='ProtocolVersion' type='i' access='read'/>"
    "    <signal name='StatusNotifierItemRegistered'>"
    "      <arg type='s'/>"
    "    </signal>"
    "    <signal name='StatusNotifierItemUnregistered'>"
    "      <arg type='s'/>"
    "    </signal>"
    "    <signal name='StatusNotifierHostRegistered'/>"
    "    <signal name='StatusNotifierHostUnregistered'/>"
    "  </interface>";

#define TRAY_INFO(...) syslog(LOG_INFO, __VA_ARGS__)
#define TRAY_WARN(...) syslog(LOG_WARNING, __VA_ARGS__)

struct Item {
  Tray* tray = nullptr;
  std::string key;   // unique bus name + object path
  std::string name;  // the item's unique bus name
  std::string path;
  std::string item_interface = kKdeItem;
  guint32 pid = 0;
  std::string flatpak_id;
  std::string desktop_id;  // without ".desktop"
  std::string app_id;      // Chrome's
  std::string sni_id;
  std::string title;
  std::string status;
  bool item_is_menu = false;
  std::string menu_path;
  std::string menu_json = "[]";
  std::string sent;  // what Chrome last got
  bool resolved = false;
  guint name_watch = 0;
  guint item_subscription = 0;
  guint menu_subscription = 0;
  guint refresh_source = 0;
  guint menu_source = 0;
};

}  // namespace

struct Tray {
  GDBusConnection* session = nullptr;
  GDBusConnection* system = nullptr;
  GDBusNodeInfo* node = nullptr;
  std::map<std::string, std::unique_ptr<Item>> items;
};

namespace {

// --- Helpers ---------------------------------------------------------------

// Async callbacks find their item again by key: it may be gone by then.
struct ItemRef {
  Tray* tray;
  std::string key;
};

Item* Find(ItemRef* ref) {
  auto it = ref->tray->items.find(ref->key);
  return it == ref->tray->items.end() ? nullptr : it->second.get();
}

ItemRef* RefOf(Item* item) {
  return new ItemRef{item->tray, item->key};
}

std::string Lower(const std::string& s) {
  gchar* lower = g_ascii_strdown(s.c_str(), -1);
  std::string out(lower);
  g_free(lower);
  return out;
}

std::string Basename(const std::string& path) {
  size_t slash = path.rfind('/');
  return slash == std::string::npos ? path : path.substr(slash + 1);
}

std::string StripDesktop(const std::string& id) {
  constexpr char kSuffix[] = ".desktop";
  if (g_str_has_suffix(id.c_str(), kSuffix)) {
    return id.substr(0, id.size() - strlen(kSuffix));
  }
  return id;
}

bool HasDesktopFile(const std::string& desktop_id) {
  GDesktopAppInfo* info =
      g_desktop_app_info_new((desktop_id + ".desktop").c_str());
  if (!info) {
    return false;
  }
  g_object_unref(info);
  return true;
}

// A dbusmenu label: "_" marks the access key, "__" is an underscore.
std::string PlainLabel(const char* label) {
  std::string out;
  for (const char* p = label; *p; ++p) {
    if (*p == '_') {
      if (p[1] == '_') {
        out += '_';
        ++p;
      }
      continue;
    }
    out += *p;
  }
  return out;
}

void AppendJsonString(std::string* out, const std::string& s) {
  *out += '"';
  for (unsigned char c : s) {
    switch (c) {
      case '"':
        *out += "\\\"";
        break;
      case '\\':
        *out += "\\\\";
        break;
      case '\n':
        *out += "\\n";
        break;
      case '\t':
        *out += "\\t";
        break;
      default:
        if (c < 0x20) {
          char buf[8];
          snprintf(buf, sizeof(buf), "\\u%04x", c);
          *out += buf;
        } else {
          *out += static_cast<char>(c);
        }
    }
  }
  *out += '"';
}

// --- Which app ---------------------------------------------------------------

std::string FlatpakIdOf(guint32 pid) {
  std::string path = "/proc/" + std::to_string(pid) + "/root/.flatpak-info";
  GKeyFile* info = g_key_file_new();
  std::string id;
  if (g_key_file_load_from_file(info, path.c_str(), G_KEY_FILE_NONE,
                                nullptr)) {
    gchar* name = g_key_file_get_string(info, "Application", "name", nullptr);
    if (name) {
      id = name;
      g_free(name);
    }
  }
  g_key_file_free(info);
  return id;
}

// The desktop file GLib launched the process from (GIO_LAUNCHED_DESKTOP_FILE,
// which nickel-appd's launches set and children inherit).
std::string LaunchedDesktopIdOf(guint32 pid) {
  std::string path = "/proc/" + std::to_string(pid) + "/environ";
  gchar* data = nullptr;
  gsize size = 0;
  std::string id;
  if (!g_file_get_contents(path.c_str(), &data, &size, nullptr)) {
    return id;
  }
  constexpr char kVar[] = "GIO_LAUNCHED_DESKTOP_FILE=";
  for (gsize i = 0; i < size; i += strlen(data + i) + 1) {
    if (g_str_has_prefix(data + i, kVar)) {
      GDesktopAppInfo* info =
          g_desktop_app_info_new_from_filename(data + i + strlen(kVar));
      if (info) {
        const char* app_id = g_app_info_get_id(G_APP_INFO(info));
        if (app_id) {
          id = StripDesktop(app_id);
        }
        g_object_unref(info);
      }
      break;
    }
  }
  g_free(data);
  return id;
}

// Matches what the item says about itself (its Id, title, executable) against
// the installed desktop files: their ids (also an id's last part, as in
// com.discordapp.Discord), StartupWMClass, executables, then names.
std::string MatchDesktopId(const std::vector<std::string>& hints) {
  std::set<std::string> wanted;
  for (const std::string& hint : hints) {
    if (!hint.empty()) {
      wanted.insert(Lower(hint));
    }
  }
  if (wanted.empty()) {
    return std::string();
  }
  GList* all = g_app_info_get_all();
  std::string by_name;
  std::string found;
  for (GList* l = all; l && found.empty(); l = l->next) {
    if (!G_IS_DESKTOP_APP_INFO(l->data)) {
      continue;
    }
    GDesktopAppInfo* info = G_DESKTOP_APP_INFO(l->data);
    const char* raw_id = g_app_info_get_id(G_APP_INFO(info));
    if (!raw_id) {
      continue;
    }
    std::string id = StripDesktop(raw_id);
    if (id == kChromeOSHandlerId) {
      continue;
    }
    std::vector<std::string> keys = {Lower(id)};
    size_t dot = id.rfind('.');
    if (dot != std::string::npos) {
      keys.push_back(Lower(id.substr(dot + 1)));
    }
    if (const char* wm_class = g_desktop_app_info_get_startup_wm_class(info)) {
      keys.push_back(Lower(wm_class));
    }
    if (const char* exe = g_app_info_get_executable(G_APP_INFO(info))) {
      keys.push_back(Lower(Basename(exe)));
    }
    for (const std::string& key : keys) {
      if (wanted.count(key)) {
        found = id;
        break;
      }
    }
    const char* name = g_app_info_get_name(G_APP_INFO(info));
    if (found.empty() && by_name.empty() && name && wanted.count(Lower(name))) {
      by_name = id;
    }
  }
  g_list_free_full(all, g_object_unref);
  return found.empty() ? by_name : found;
}

void Resolve(Item* item) {
  if (item->resolved) {
    return;
  }
  if (item->pid) {
    item->flatpak_id = FlatpakIdOf(item->pid);
    if (!item->flatpak_id.empty() && HasDesktopFile(item->flatpak_id)) {
      item->desktop_id = item->flatpak_id;
    }
    if (item->desktop_id.empty()) {
      item->desktop_id = LaunchedDesktopIdOf(item->pid);
    }
  }
  if (item->desktop_id.empty()) {
    std::vector<std::string> hints = {item->sni_id, item->title};
    if (item->pid) {
      std::string proc = "/proc/" + std::to_string(item->pid);
      gchar* exe = g_file_read_link((proc + "/exe").c_str(), nullptr);
      if (exe) {
        hints.push_back(Basename(exe));
        g_free(exe);
      }
      gchar* comm = nullptr;
      if (g_file_get_contents((proc + "/comm").c_str(), &comm, nullptr,
                              nullptr)) {
        hints.push_back(g_strstrip(comm));
        g_free(comm);
      }
    }
    item->desktop_id = MatchDesktopId(hints);
  }
  item->resolved = true;
  if (item->desktop_id.empty()) {
    TRAY_WARN("tray item %s (id %s, pid %u): no app found for it",
              item->key.c_str(), item->sni_id.c_str(), item->pid);
    return;
  }
  item->app_id = NickelAppId(item->desktop_id);
  TRAY_INFO("tray item %s is %s", item->key.c_str(), item->desktop_id.c_str());
}

// --- Chrome --------------------------------------------------------------------

void SendItem(Item* item) {
  Resolve(item);
  if (item->app_id.empty()) {
    return;
  }
  bool attention = item->status == "NeedsAttention";
  std::string state = item->title + '\n' + (attention ? "1" : "0") + '\n' +
                      item->menu_json;
  if (state == item->sent) {
    return;
  }
  item->sent = state;
  g_dbus_connection_call(
      item->tray->system, vm_tools::apps::kVmApplicationsServiceName,
      vm_tools::apps::kVmApplicationsServicePath,
      vm_tools::apps::kVmApplicationsServiceInterface, "NickelTrayItem",
      g_variant_new("(sssbs)", item->key.c_str(), item->app_id.c_str(),
                    item->title.c_str(), attention, item->menu_json.c_str()),
      nullptr, G_DBUS_CALL_FLAGS_NONE, -1, nullptr,
      [](GObject* source, GAsyncResult* result, gpointer) {
        g_autoptr(GError) error = nullptr;
        g_autoptr(GVariant) reply = g_dbus_connection_call_finish(
            G_DBUS_CONNECTION(source), result, &error);
        if (!reply) {
          TRAY_WARN("NickelTrayItem: %s", error->message);
        }
      },
      nullptr);
}

void SendRemoved(Tray* tray, const std::string& key) {
  g_dbus_connection_call(
      tray->system, vm_tools::apps::kVmApplicationsServiceName,
      vm_tools::apps::kVmApplicationsServicePath,
      vm_tools::apps::kVmApplicationsServiceInterface, "NickelTrayItemRemoved",
      g_variant_new("(s)", key.c_str()), nullptr, G_DBUS_CALL_FLAGS_NONE, -1,
      nullptr, nullptr, nullptr);
}

// Chrome started (again: a sign-in restarts it) and knows no items.
void OnChromeAppeared(GDBusConnection*, const gchar*, const gchar*,
                      gpointer data) {
  Tray* tray = static_cast<Tray*>(data);
  for (auto& [key, item] : tray->items) {
    item->sent.clear();
    SendItem(item.get());
  }
}

// --- The menu --------------------------------------------------------------------

// A dbusmenu layout node (ia{sv}av)'s children, as a JSON array of
// {"id", "label", "separator", "enabled", "toggle", "checked", "children"};
// hidden ones are left out.
void ChildrenToJson(GVariant* node, std::string* out) {
  g_autoptr(GVariant) children = g_variant_get_child_value(node, 2);
  *out += '[';
  bool first = true;
  for (gsize i = 0; i < g_variant_n_children(children); ++i) {
    g_autoptr(GVariant) boxed = g_variant_get_child_value(children, i);
    g_autoptr(GVariant) child = g_variant_get_variant(boxed);
    if (!g_variant_is_of_type(child, G_VARIANT_TYPE("(ia{sv}av)"))) {
      continue;
    }
    gint32 id = 0;
    g_variant_get_child(child, 0, "i", &id);
    g_autoptr(GVariant) props = g_variant_get_child_value(child, 1);
    gboolean visible = TRUE, enabled = TRUE;
    g_variant_lookup(props, "visible", "b", &visible);
    if (!visible) {
      continue;
    }
    g_variant_lookup(props, "enabled", "b", &enabled);
    const char* label = "";
    g_variant_lookup(props, "label", "&s", &label);
    const char* type = "standard";
    g_variant_lookup(props, "type", "&s", &type);
    const char* toggle = "";
    g_variant_lookup(props, "toggle-type", "&s", &toggle);
    gint32 toggle_state = -1;
    g_variant_lookup(props, "toggle-state", "i", &toggle_state);
    if (!first) {
      *out += ',';
    }
    first = false;
    *out += "{\"id\":" + std::to_string(id) + ",\"label\":";
    AppendJsonString(out, PlainLabel(label));
    *out += std::string(",\"separator\":") +
            (strcmp(type, "separator") == 0 ? "true" : "false");
    *out += std::string(",\"enabled\":") + (enabled ? "true" : "false");
    *out += ",\"toggle\":";
    AppendJsonString(out, toggle);
    *out += std::string(",\"checked\":") +
            (toggle_state == 1 ? "true" : "false");
    *out += ",\"children\":";
    ChildrenToJson(child, out);
    *out += '}';
  }
  *out += ']';
}

void OnLayout(GObject* source, GAsyncResult* result, gpointer data) {
  std::unique_ptr<ItemRef> ref(static_cast<ItemRef*>(data));
  g_autoptr(GError) error = nullptr;
  g_autoptr(GVariant) reply = g_dbus_connection_call_finish(
      G_DBUS_CONNECTION(source), result, &error);
  Item* item = Find(ref.get());
  if (!item) {
    return;
  }
  if (!reply) {
    TRAY_WARN("tray item %s: no menu: %s", item->key.c_str(), error->message);
    return;
  }
  // (u revision, (ia{sv}av) layout)
  g_autoptr(GVariant) layout = g_variant_get_child_value(reply, 1);
  std::string json;
  ChildrenToJson(layout, &json);
  item->menu_json = json;
  SendItem(item);
}

void FetchMenu(Item* item) {
  if (item->menu_path.empty()) {
    return;
  }
  // Some apps fill their menus when they are about to be shown.
  g_dbus_connection_call(item->tray->session, item->name.c_str(),
                         item->menu_path.c_str(), kMenuInterface, "AboutToShow",
                         g_variant_new("(i)", 0), nullptr,
                         G_DBUS_CALL_FLAGS_NONE, -1, nullptr, nullptr, nullptr);
  const gchar* no_properties[] = {nullptr};
  g_dbus_connection_call(
      item->tray->session, item->name.c_str(), item->menu_path.c_str(),
      kMenuInterface, "GetLayout",
      g_variant_new("(ii^as)", 0, -1, no_properties),
      G_VARIANT_TYPE("(u(ia{sv}av))"), G_DBUS_CALL_FLAGS_NONE, -1, nullptr,
      OnLayout, RefOf(item));
}

void ScheduleMenu(Item* item) {
  if (item->menu_source) {
    return;
  }
  item->menu_source = g_timeout_add_full(
      G_PRIORITY_DEFAULT, kRefreshDelayMs,
      [](gpointer data) -> gboolean {
        Item* item = Find(static_cast<ItemRef*>(data));
        if (item) {
          item->menu_source = 0;
          FetchMenu(item);
        }
        return G_SOURCE_REMOVE;
      },
      RefOf(item), [](gpointer data) { delete static_cast<ItemRef*>(data); });
}

void OnMenuSignal(GDBusConnection*, const gchar*, const gchar*, const gchar*,
                  const gchar*, GVariant*, gpointer data) {
  Item* item = Find(static_cast<ItemRef*>(data));
  if (item) {
    ScheduleMenu(item);
  }
}

void WatchMenu(Item* item, const std::string& menu_path) {
  if (menu_path == item->menu_path) {
    return;
  }
  if (item->menu_subscription) {
    g_dbus_connection_signal_unsubscribe(item->tray->session,
                                         item->menu_subscription);
    item->menu_subscription = 0;
  }
  item->menu_path = menu_path;
  item->menu_json = "[]";
  if (menu_path.empty() || menu_path == "/") {
    item->menu_path.clear();
    return;
  }
  item->menu_subscription = g_dbus_connection_signal_subscribe(
      item->tray->session, item->name.c_str(), kMenuInterface, nullptr,
      menu_path.c_str(), nullptr, G_DBUS_SIGNAL_FLAGS_NONE, OnMenuSignal,
      RefOf(item), [](gpointer data) { delete static_cast<ItemRef*>(data); });
  FetchMenu(item);
}

// --- The item's properties ---------------------------------------------------------

void Refresh(Item* item);

void OnProperties(GObject* source, GAsyncResult* result, gpointer data) {
  std::unique_ptr<ItemRef> ref(static_cast<ItemRef*>(data));
  g_autoptr(GError) error = nullptr;
  g_autoptr(GVariant) reply = g_dbus_connection_call_finish(
      G_DBUS_CONNECTION(source), result, &error);
  Item* item = Find(ref.get());
  if (!item) {
    return;
  }
  if (!reply) {
    // A few implement it under the freedesktop name.
    if (item->item_interface == kKdeItem) {
      item->item_interface = kFdoItem;
      Refresh(item);
      return;
    }
    TRAY_WARN("tray item %s: %s", item->key.c_str(), error->message);
    return;
  }
  g_autoptr(GVariant) props = g_variant_get_child_value(reply, 0);
  const char* value = nullptr;
  if (g_variant_lookup(props, "Id", "&s", &value)) {
    item->sni_id = value;
  }
  if (g_variant_lookup(props, "Title", "&s", &value)) {
    item->title = value;
  }
  if (g_variant_lookup(props, "Status", "&s", &value)) {
    item->status = value;
  }
  // NickelOS's XEmbed proxy (nickel-xembed-tray) speaks for old X11 tray
  // icons: the app is that process, not the proxy.
  guint32 app_pid = 0;
  if (g_variant_lookup(props, "X-NickelOS-Pid", "u", &app_pid) && app_pid &&
      app_pid != item->pid) {
    item->pid = app_pid;
    item->resolved = false;
  }
  gboolean is_menu = FALSE;
  if (g_variant_lookup(props, "ItemIsMenu", "b", &is_menu)) {
    item->item_is_menu = is_menu;
  }
  std::string menu_path;
  if (g_variant_lookup(props, "Menu", "&o", &value)) {
    menu_path = value;
  }
  WatchMenu(item, menu_path);
  SendItem(item);
}

void Refresh(Item* item) {
  g_dbus_connection_call(
      item->tray->session, item->name.c_str(), item->path.c_str(),
      "org.freedesktop.DBus.Properties", "GetAll",
      g_variant_new("(s)", item->item_interface.c_str()),
      G_VARIANT_TYPE("(a{sv})"), G_DBUS_CALL_FLAGS_NONE, -1, nullptr,
      OnProperties, RefOf(item));
}

void ScheduleRefresh(Item* item) {
  if (item->refresh_source) {
    return;
  }
  item->refresh_source = g_timeout_add_full(
      G_PRIORITY_DEFAULT, kRefreshDelayMs,
      [](gpointer data) -> gboolean {
        Item* item = Find(static_cast<ItemRef*>(data));
        if (item) {
          item->refresh_source = 0;
          Refresh(item);
        }
        return G_SOURCE_REMOVE;
      },
      RefOf(item), [](gpointer data) { delete static_cast<ItemRef*>(data); });
}

void OnItemSignal(GDBusConnection*, const gchar*, const gchar* path,
                  const gchar*, const gchar*, GVariant*, gpointer data) {
  Item* item = Find(static_cast<ItemRef*>(data));
  if (item && item->path == path) {
    ScheduleRefresh(item);
  }
}

// --- Registration --------------------------------------------------------------------

void EmitWatcherSignal(Tray* tray, const char* signal, const char* arg) {
  for (const char* interface : {kKdeWatcher, kFdoWatcher}) {
    g_dbus_connection_emit_signal(
        tray->session, nullptr, kWatcherPath, interface, signal,
        arg ? g_variant_new("(s)", arg) : nullptr, nullptr);
  }
}

void RemoveItem(Tray* tray, const std::string& key) {
  auto it = tray->items.find(key);
  if (it == tray->items.end()) {
    return;
  }
  Item* item = it->second.get();
  if (item->name_watch) {
    g_bus_unwatch_name(item->name_watch);
  }
  if (item->item_subscription) {
    g_dbus_connection_signal_unsubscribe(tray->session,
                                         item->item_subscription);
  }
  if (item->menu_subscription) {
    g_dbus_connection_signal_unsubscribe(tray->session,
                                         item->menu_subscription);
  }
  if (item->refresh_source) {
    g_source_remove(item->refresh_source);
  }
  if (item->menu_source) {
    g_source_remove(item->menu_source);
  }
  bool was_sent = !item->sent.empty();
  std::string service = item->name + item->path;
  tray->items.erase(it);
  if (was_sent) {
    SendRemoved(tray, key);
  }
  EmitWatcherSignal(tray, "StatusNotifierItemUnregistered", service.c_str());
  TRAY_INFO("tray item %s gone", key.c_str());
}

void OnItemVanished(GDBusConnection*, const gchar*, gpointer data) {
  ItemRef* ref = static_cast<ItemRef*>(data);
  // Not now: this runs from the watch, which RemoveItem removes.
  g_idle_add_full(
      G_PRIORITY_DEFAULT,
      [](gpointer data) -> gboolean {
        ItemRef* ref = static_cast<ItemRef*>(data);
        RemoveItem(ref->tray, ref->key);
        return G_SOURCE_REMOVE;
      },
      new ItemRef(*ref), [](gpointer data) {
        delete static_cast<ItemRef*>(data);
      });
}

void AddItem(Tray* tray, const std::string& name, const std::string& path) {
  std::string key = name + path;
  if (tray->items.count(key)) {
    return;
  }
  auto owned = std::make_unique<Item>();
  Item* item = owned.get();
  item->tray = tray;
  item->key = key;
  item->name = name;
  item->path = path;
  tray->items[key] = std::move(owned);

  g_autoptr(GVariant) pid = g_dbus_connection_call_sync(
      tray->session, "org.freedesktop.DBus", "/org/freedesktop/DBus",
      "org.freedesktop.DBus", "GetConnectionUnixProcessID",
      g_variant_new("(s)", name.c_str()), G_VARIANT_TYPE("(u)"),
      G_DBUS_CALL_FLAGS_NONE, -1, nullptr, nullptr);
  if (pid) {
    g_variant_get(pid, "(u)", &item->pid);
  }
  item->name_watch = g_bus_watch_name_on_connection(
      tray->session, name.c_str(), G_BUS_NAME_WATCHER_FLAGS_NONE, nullptr,
      OnItemVanished, RefOf(item),
      [](gpointer data) { delete static_cast<ItemRef*>(data); });
  item->item_subscription = g_dbus_connection_signal_subscribe(
      tray->session, name.c_str(), nullptr, nullptr, path.c_str(), nullptr,
      G_DBUS_SIGNAL_FLAGS_NONE, OnItemSignal, RefOf(item),
      [](gpointer data) { delete static_cast<ItemRef*>(data); });
  TRAY_INFO("tray item %s registered (pid %u)", key.c_str(), item->pid);
  EmitWatcherSignal(tray, "StatusNotifierItemRegistered",
                    (name + path).c_str());
  Refresh(item);
}

// org.kde.StatusNotifierWatcher.RegisterStatusNotifierItem(service): a bus
// name (its item at /StatusNotifierItem) or, as Chromium and libayatana do,
// an object path on the caller's connection.
void RegisterItem(Tray* tray, const char* sender, const char* service) {
  std::string name, path;
  if (service[0] == '/') {
    name = sender;
    path = service;
  } else {
    name = service;
    path = kDefaultItemPath;
    if (name[0] != ':') {
      g_autoptr(GVariant) owner = g_dbus_connection_call_sync(
          tray->session, "org.freedesktop.DBus", "/org/freedesktop/DBus",
          "org.freedesktop.DBus", "GetNameOwner",
          g_variant_new("(s)", service), G_VARIANT_TYPE("(s)"),
          G_DBUS_CALL_FLAGS_NONE, -1, nullptr, nullptr);
      if (!owner) {
        return;
      }
      const char* unique = nullptr;
      g_variant_get(owner, "(&s)", &unique);
      name = unique;
    }
  }
  AddItem(tray, name, path);
}

void OnWatcherCall(GDBusConnection*, const gchar* sender, const gchar*,
                   const gchar*, const gchar* method, GVariant* parameters,
                   GDBusMethodInvocation* invocation, gpointer data) {
  Tray* tray = static_cast<Tray*>(data);
  const char* service = nullptr;
  g_variant_get(parameters, "(&s)", &service);
  if (strcmp(method, "RegisterStatusNotifierItem") == 0) {
    RegisterItem(tray, sender, service);
  }
  // RegisterStatusNotifierHost: there is one host, this.
  g_dbus_method_invocation_return_value(invocation, nullptr);
}

GVariant* OnWatcherProperty(GDBusConnection*, const gchar*, const gchar*,
                            const gchar*, const gchar* property, GError**,
                            gpointer data) {
  Tray* tray = static_cast<Tray*>(data);
  if (strcmp(property, "RegisteredStatusNotifierItems") == 0) {
    GVariantBuilder builder;
    g_variant_builder_init(&builder, G_VARIANT_TYPE("as"));
    for (auto& [key, item] : tray->items) {
      g_variant_builder_add(&builder, "s", key.c_str());
    }
    return g_variant_builder_end(&builder);
  }
  if (strcmp(property, "IsStatusNotifierHostRegistered") == 0) {
    return g_variant_new_boolean(TRUE);
  }
  if (strcmp(property, "ProtocolVersion") == 0) {
    return g_variant_new_int32(0);
  }
  return nullptr;
}

void OnWatcherNameLost(GDBusConnection*, const gchar* name, gpointer) {
  TRAY_WARN("cannot own %s: another tray runs, Linux apps' tray icons go "
            "there",
            name);
}

// --- Events from ChromeOS ------------------------------------------------------------

void CallItem(Item* item, const char* method, GVariant* args) {
  g_dbus_connection_call(item->tray->session, item->name.c_str(),
                         item->path.c_str(), item->item_interface.c_str(),
                         method, args, nullptr, G_DBUS_CALL_FLAGS_NONE, -1,
                         nullptr, nullptr, nullptr);
}

// The app's own way to show itself, when the item has no Activate: start it
// again, which a single-instance app answers by showing its window.
void LaunchApp(Item* item, const std::string& token) {
  if (item->desktop_id.empty()) {
    return;
  }
  GDesktopAppInfo* info =
      g_desktop_app_info_new((item->desktop_id + ".desktop").c_str());
  if (!info) {
    return;
  }
  GAppLaunchContext* context = g_app_launch_context_new();
  if (!token.empty()) {
    g_app_launch_context_setenv(context, "XDG_ACTIVATION_TOKEN",
                                token.c_str());
  }
  g_app_info_launch(G_APP_INFO(info), nullptr, context, nullptr);
  g_object_unref(context);
  g_object_unref(info);
}

struct ActivateCall {
  ItemRef ref;
  std::string token;
};

void OnActivated(GObject* source, GAsyncResult* result, gpointer data) {
  std::unique_ptr<ActivateCall> call(static_cast<ActivateCall*>(data));
  g_autoptr(GError) error = nullptr;
  g_autoptr(GVariant) reply = g_dbus_connection_call_finish(
      G_DBUS_CONNECTION(source), result, &error);
  Item* item = Find(&call->ref);
  if (!reply && item) {
    TRAY_INFO("tray item %s: Activate: %s; starting the app instead",
              item->key.c_str(), error->message);
    LaunchApp(item, call->token);
  }
}

void Quit(Item* item) {
  if (!item->flatpak_id.empty()) {
    // Its bus connection is the sandbox's D-Bus proxy, not the app.
    const gchar* argv[] = {"flatpak", "kill", item->flatpak_id.c_str(),
                           nullptr};
    g_spawn_async(nullptr, const_cast<gchar**>(argv), nullptr,
                  G_SPAWN_SEARCH_PATH, nullptr, nullptr, nullptr, nullptr);
  } else if (item->pid > 1) {
    kill(static_cast<pid_t>(item->pid), SIGTERM);
  }
}

}  // namespace

Tray* TrayNew(GDBusConnection* session, GDBusConnection* system) {
  Tray* tray = new Tray;
  tray->session = session;
  tray->system = system;
  std::string xml = "<node>";
  for (const char* name : {kKdeWatcher, kFdoWatcher}) {
    std::string interface = kWatcherInterfaceXml;
    interface.replace(interface.find("NAME"), 4, name);
    xml += interface;
  }
  xml += "</node>";
  g_autoptr(GError) error = nullptr;
  tray->node = g_dbus_node_info_new_for_xml(xml.c_str(), &error);
  if (!tray->node) {
    TRAY_WARN("tray: %s", error->message);
    return tray;
  }
  static const GDBusInterfaceVTable kVTable = {OnWatcherCall,
                                               OnWatcherProperty, nullptr};
  for (GDBusInterfaceInfo** info = tray->node->interfaces; *info; ++info) {
    if (!g_dbus_connection_register_object(session, kWatcherPath, *info,
                                           &kVTable, tray, nullptr, &error)) {
      TRAY_WARN("tray: cannot export %s: %s", (*info)->name, error->message);
      g_clear_error(&error);
    }
  }
  for (const char* name : {kKdeWatcher, kFdoWatcher}) {
    g_bus_own_name_on_connection(session, name, G_BUS_NAME_OWNER_FLAGS_NONE,
                                 nullptr, OnWatcherNameLost, nullptr, nullptr);
  }
  // Some apps look for a host by its name.
  std::string host = "org.kde.StatusNotifierHost-" + std::to_string(getpid());
  g_bus_own_name_on_connection(session, host.c_str(),
                               G_BUS_NAME_OWNER_FLAGS_NONE, nullptr, nullptr,
                               nullptr, nullptr);
  EmitWatcherSignal(tray, "StatusNotifierHostRegistered", nullptr);
  g_bus_watch_name_on_connection(system,
                                 vm_tools::apps::kVmApplicationsServiceName,
                                 G_BUS_NAME_WATCHER_FLAGS_NONE,
                                 OnChromeAppeared, nullptr, tray, nullptr);
  return tray;
}

void TrayEvent(Tray* tray,
               const std::string& key,
               const std::string& event,
               int item_id,
               const std::string& token) {
  auto it = tray->items.find(key);
  if (it == tray->items.end()) {
    return;
  }
  Item* item = it->second.get();
  // Lets the app bring its window forward (KDE's addition to the
  // specification; apps without it ignore the call).
  if (!token.empty() && event != "quit") {
    CallItem(item, "ProvideXdgActivationToken",
             g_variant_new("(s)", token.c_str()));
  }
  if (event == "activate") {
    if (item->item_is_menu) {
      LaunchApp(item, token);
      return;
    }
    g_dbus_connection_call(item->tray->session, item->name.c_str(),
                           item->path.c_str(), item->item_interface.c_str(),
                           "Activate", g_variant_new("(ii)", 0, 0), nullptr,
                           G_DBUS_CALL_FLAGS_NONE, -1, nullptr, OnActivated,
                           new ActivateCall{{tray, key}, token});
  } else if (event == "menu" && !item->menu_path.empty()) {
    g_dbus_connection_call(
        tray->session, item->name.c_str(), item->menu_path.c_str(),
        kMenuInterface, "Event",
        g_variant_new("(isvu)", item_id, "clicked", g_variant_new_int32(0),
                      static_cast<guint32>(g_get_monotonic_time() / 1000)),
        nullptr, G_DBUS_CALL_FLAGS_NONE, -1, nullptr, nullptr, nullptr);
  } else if (event == "quit") {
    Quit(item);
  }
}
