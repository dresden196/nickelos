// Distributed under the terms of the GNU General Public License v2
//
// GlobalShortcuts (org.freedesktop.impl.portal.GlobalShortcuts) for
// xdg-desktop-portal-nickel: apps get keyboard shortcuts that work while
// another window has the focus, as push-to-talk in voice chat (Discord,
// Mumble, OBS's hotkeys).
//
// BindShortcuts asks Chrome (VmApplicationsService.NickelShortcutsBind,
// Chrome patch 0046) with the app's name and each shortcut's description and
// preferred keys: a trigger as the portal spells it ("CTRL+ALT+a": SHIFT,
// CTRL, ALT and LOGO, then an xkb key name) becomes modifiers and the evdev
// code of the key on the US layout, from which Chrome takes its key code.
// Chrome asks the user, unless they allowed the same shortcuts for this app
// before, in a ChromeOS dialog where each shortcut's keys can be changed;
// it answers with the keys the user chose, as ChromeOS writes them
// ("Ctrl+Alt+A"), which the app may show.
//
// With the call goes one end of a socket pair. Chrome writes into it, one
// message each, when a bound shortcut is pressed and released ("a INDEX MS",
// "d INDEX MS": the shortcut's place in the call, the key event's time in
// milliseconds), and this sends Activated and Deactivated to the app; press
// and release apart, as push-to-talk needs. Keys never travel over the
// system bus. The bindings last as long as the socket: closing the session
// (or this process ending) closes this end and Chrome forgets them; when
// Chrome drops them, ShortcutsChanged tells the app they are gone.

#include "globalshortcuts.h"

#include <chromeos/dbus/vm_applications/dbus-constants.h>
#include <gio/gdesktopappinfo.h>
#include <gio/gunixfdlist.h>
#include <glib-unix.h>
#include <sys/socket.h>
#include <syslog.h>
#include <unistd.h>
#include <xkbcommon/xkbcommon.h>

#include <cerrno>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

namespace {

constexpr char kObjectPath[] = "/org/freedesktop/portal/desktop";
constexpr char kInterface[] = "org.freedesktop.impl.portal.GlobalShortcuts";
constexpr char kSessionInterface[] = "org.freedesktop.impl.portal.Session";
constexpr char kBindMethod[] = "NickelShortcutsBind";
// Modifiers as NickelShortcutsBind takes them.
constexpr guint32 kShift = 1;
constexpr guint32 kCtrl = 2;
constexpr guint32 kAlt = 4;
constexpr guint32 kLogo = 8;
// Portal responses.
constexpr guint32 kSuccess = 0;
constexpr guint32 kCanceled = 1;
constexpr guint32 kFailed = 2;

struct Shortcut {
  std::string id;
  std::string description;
  // As the user chose them, "" if none.
  std::string trigger;
};

}  // namespace

struct ShortcutSession;

struct GlobalShortcuts {
  GDBusConnection* session = nullptr;
  GDBusConnection* system = nullptr;
  GDBusInterfaceInfo* session_info = nullptr;
  std::map<std::string, ShortcutSession*> sessions;
};

struct ShortcutSession {
  GlobalShortcuts* shortcuts = nullptr;
  std::string handle;
  std::string app_id;
  guint object = 0;
  // In the order Chrome counts them.
  std::vector<Shortcut> bound;
  // This end of the socket Chrome writes to, from BindShortcuts on (its
  // dialog closes with it) while bound.
  int fd = -1;
  guint watch = 0;
  // BindShortcuts, while the user decides.
  GDBusMethodInvocation* binding = nullptr;
  std::vector<Shortcut> pending;
};

namespace {

GVariant* EmptyResults() {
  return g_variant_new_array(G_VARIANT_TYPE("{sv}"), nullptr, 0);
}

void Reply(GDBusMethodInvocation* invocation,
           guint32 response,
           GVariant* results) {
  g_dbus_method_invocation_return_value(
      invocation,
      g_variant_new("(u@a{sv})", response, results ? results : EmptyResults()));
}

// For the log: apps without an id (native apps that did not register with
// the portal) have none to show.
const char* LogName(const ShortcutSession* s) {
  return s->app_id.empty() ? "(an app without id)" : s->app_id.c_str();
}

// The name the user knows the app by, for Chrome's dialog.
std::string AppName(const std::string& app_id) {
  if (app_id.empty()) {
    return "";
  }
  std::string desktop_id = app_id + ".desktop";
  g_autoptr(GDesktopAppInfo) info = g_desktop_app_info_new(desktop_id.c_str());
  return info ? g_app_info_get_display_name(G_APP_INFO(info)) : app_id;
}

// a(sa{sv}), as BindShortcuts' and ListShortcuts' results and
// ShortcutsChanged have them.
GVariant* ShortcutList(const std::vector<Shortcut>& list) {
  GVariantBuilder builder;
  g_variant_builder_init(&builder, G_VARIANT_TYPE("a(sa{sv})"));
  for (const Shortcut& shortcut : list) {
    GVariantBuilder props;
    g_variant_builder_init(&props, G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(&props, "{sv}", "description",
                          g_variant_new_string(shortcut.description.c_str()));
    g_variant_builder_add(&props, "{sv}", "trigger_description",
                          g_variant_new_string(shortcut.trigger.c_str()));
    g_variant_builder_add(&builder, "(s@a{sv})", shortcut.id.c_str(),
                          g_variant_builder_end(&props));
  }
  return g_variant_builder_end(&builder);
}

GVariant* ShortcutResults(const std::vector<Shortcut>& list) {
  GVariantBuilder results;
  g_variant_builder_init(&results, G_VARIANT_TYPE("a{sv}"));
  g_variant_builder_add(&results, "{sv}", "shortcuts", ShortcutList(list));
  return g_variant_builder_end(&results);
}

// The evdev code of the key that types |keysym| on the US layout, the one
// Chrome's key codes are named after.
guint32 KeysymToEvdev(xkb_keysym_t keysym) {
  static xkb_keymap* keymap = [] {
    xkb_context* context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    xkb_rule_names names = {};
    names.layout = "us";
    return context ? xkb_keymap_new_from_names(context, &names,
                                               XKB_KEYMAP_COMPILE_NO_FLAGS)
                   : nullptr;
  }();
  if (!keymap) {
    return 0;
  }
  for (xkb_keycode_t key = xkb_keymap_min_keycode(keymap);
       key <= xkb_keymap_max_keycode(keymap); ++key) {
    const xkb_level_index_t levels =
        xkb_keymap_num_levels_for_key(keymap, key, 0);
    for (xkb_level_index_t level = 0; level < levels && level < 2; ++level) {
      const xkb_keysym_t* syms = nullptr;
      int n = xkb_keymap_key_get_syms_by_level(keymap, key, 0, level, &syms);
      for (int i = 0; i < n; ++i) {
        if (syms[i] == keysym) {
          return key - 8;
        }
      }
    }
  }
  return 0;
}

// A trigger as the portal spells it ("CTRL+ALT+a", "LOGO+F5"): modifiers
// and the key's evdev code. False if it names no key this can find.
bool ParseTrigger(const std::string& trigger,
                  guint32* modifiers,
                  guint32* evdev) {
  *modifiers = 0;
  *evdev = 0;
  g_auto(GStrv) parts = g_strsplit(trigger.c_str(), "+", -1);
  const guint n = g_strv_length(parts);
  if (n == 0 || !*parts[n - 1]) {
    return false;
  }
  for (guint i = 0; i + 1 < n; ++i) {
    const gchar* part = parts[i];
    if (!g_ascii_strcasecmp(part, "SHIFT")) {
      *modifiers |= kShift;
    } else if (!g_ascii_strcasecmp(part, "CTRL") ||
               !g_ascii_strcasecmp(part, "CONTROL")) {
      *modifiers |= kCtrl;
    } else if (!g_ascii_strcasecmp(part, "ALT")) {
      *modifiers |= kAlt;
    } else if (!g_ascii_strcasecmp(part, "LOGO") ||
               !g_ascii_strcasecmp(part, "SUPER") ||
               !g_ascii_strcasecmp(part, "META")) {
      *modifiers |= kLogo;
    } else if (!g_ascii_strcasecmp(part, "NUM") ||
               !g_ascii_strcasecmp(part, "CAPS")) {
      // Num Lock and Caps Lock are states, not keys held: left out.
    } else {
      return false;
    }
  }
  xkb_keysym_t keysym =
      xkb_keysym_from_name(parts[n - 1], XKB_KEYSYM_NO_FLAGS);
  if (keysym == XKB_KEY_NoSymbol) {
    keysym = xkb_keysym_from_name(parts[n - 1], XKB_KEYSYM_CASE_INSENSITIVE);
  }
  if (keysym == XKB_KEY_NoSymbol) {
    return false;
  }
  *evdev = KeysymToEvdev(xkb_keysym_to_lower(keysym));
  if (!*evdev) {
    *evdev = KeysymToEvdev(keysym);
  }
  return *evdev != 0;
}

void Unbind(ShortcutSession* s) {
  if (s->watch) {
    g_source_remove(s->watch);
    s->watch = 0;
  }
  if (s->fd >= 0) {
    close(s->fd);
    s->fd = -1;
  }
}

void CloseSession(ShortcutSession* s, bool tell_app) {
  GlobalShortcuts* shortcuts = s->shortcuts;
  // The dialog goes away with its socket; the request ends as canceled.
  if (s->binding) {
    Reply(s->binding, kCanceled, nullptr);
    s->binding = nullptr;
  }
  Unbind(s);
  if (tell_app) {
    g_dbus_connection_emit_signal(shortcuts->session, nullptr,
                                  s->handle.c_str(), kSessionInterface,
                                  "Closed", nullptr, nullptr);
  }
  if (s->object) {
    g_dbus_connection_unregister_object(shortcuts->session, s->object);
  }
  shortcuts->sessions.erase(s->handle);
  delete s;
}

// What Chrome writes: a shortcut pressed or released.
gboolean OnChromeMessage(gint fd, GIOCondition condition, gpointer data) {
  ShortcutSession* s = static_cast<ShortcutSession*>(data);
  char message[64];
  ssize_t n = (condition & G_IO_IN) ? recv(fd, message, sizeof(message) - 1,
                                           MSG_DONTWAIT)
                                    : 0;
  if (n < 0 && (errno == EAGAIN || errno == EINTR)) {
    return G_SOURCE_CONTINUE;
  }
  if (n <= 0) {
    // Chrome dropped the bindings (or ended).
    syslog(LOG_INFO, "global shortcuts: %s: shortcuts ended", LogName(s));
    s->watch = 0;
    close(s->fd);
    s->fd = -1;
    s->bound.clear();
    g_dbus_connection_emit_signal(
        s->shortcuts->session, nullptr, kObjectPath, kInterface,
        "ShortcutsChanged",
        g_variant_new("(o@a(sa{sv}))", s->handle.c_str(),
                      ShortcutList(s->bound)),
        nullptr);
    return G_SOURCE_REMOVE;
  }
  message[n] = '\0';
  char kind = 0;
  unsigned index = 0;
  unsigned long long ms = 0;
  if (sscanf(message, "%c %u %llu", &kind, &index, &ms) != 3 ||
      (kind != 'a' && kind != 'd') || index >= s->bound.size()) {
    return G_SOURCE_CONTINUE;
  }
  GVariantBuilder options;
  g_variant_builder_init(&options, G_VARIANT_TYPE("a{sv}"));
  g_dbus_connection_emit_signal(
      s->shortcuts->session, nullptr, kObjectPath, kInterface,
      kind == 'a' ? "Activated" : "Deactivated",
      g_variant_new("(ost@a{sv})", s->handle.c_str(),
                    s->bound[index].id.c_str(), static_cast<guint64>(ms),
                    g_variant_builder_end(&options)),
      nullptr);
  return G_SOURCE_CONTINUE;
}

void OnSessionMethodCall(GDBusConnection*,
                         const gchar*,
                         const gchar* object_path,
                         const gchar*,
                         const gchar* method_name,
                         GVariant*,
                         GDBusMethodInvocation* invocation,
                         gpointer user_data) {
  GlobalShortcuts* shortcuts = static_cast<GlobalShortcuts*>(user_data);
  auto it = shortcuts->sessions.find(object_path);
  g_dbus_method_invocation_return_value(invocation, nullptr);
  if (it != shortcuts->sessions.end() &&
      g_strcmp0(method_name, "Close") == 0) {
    CloseSession(it->second, false);
  }
}

GVariant* OnSessionGetProperty(GDBusConnection*,
                               const gchar*,
                               const gchar*,
                               const gchar*,
                               const gchar* property_name,
                               GError** error,
                               gpointer) {
  if (g_strcmp0(property_name, "version") == 0) {
    return g_variant_new_uint32(1);
  }
  g_set_error(error, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_PROPERTY,
              "unknown property %s", property_name);
  return nullptr;
}

void CreateSession(GlobalShortcuts* shortcuts,
                   GDBusMethodInvocation* invocation,
                   GVariant* parameters) {
  const gchar* handle = nullptr;
  const gchar* session_handle = nullptr;
  const gchar* app_id = nullptr;
  g_autoptr(GVariant) options = nullptr;
  g_variant_get(parameters, "(&o&o&s@a{sv})", &handle, &session_handle,
                &app_id, &options);
  if (shortcuts->sessions.count(session_handle)) {
    Reply(invocation, kFailed, nullptr);
    return;
  }
  ShortcutSession* s = new ShortcutSession;
  s->shortcuts = shortcuts;
  s->handle = session_handle;
  s->app_id = app_id;
  static const GDBusInterfaceVTable kSessionVTable = {
      OnSessionMethodCall, OnSessionGetProperty, nullptr};
  s->object = g_dbus_connection_register_object(
      shortcuts->session, session_handle, shortcuts->session_info,
      &kSessionVTable, shortcuts, nullptr, nullptr);
  shortcuts->sessions[session_handle] = s;
  GVariantBuilder results;
  g_variant_builder_init(&results, G_VARIANT_TYPE("a{sv}"));
  g_variant_builder_add(&results, "{sv}", "session_id",
                        g_variant_new_string(session_handle));
  Reply(invocation, kSuccess, g_variant_builder_end(&results));
}

struct BindCall {
  GlobalShortcuts* shortcuts;
  std::string handle;
};

void OnBound(GObject* source, GAsyncResult* result, gpointer data) {
  BindCall* call = static_cast<BindCall*>(data);
  g_autoptr(GError) error = nullptr;
  g_autoptr(GVariant) reply = g_dbus_connection_call_with_unix_fd_list_finish(
      G_DBUS_CONNECTION(source), nullptr, result, &error);
  if (!reply) {
    syslog(LOG_WARNING, "%s: %s", kBindMethod, error->message);
  }
  auto it = call->shortcuts->sessions.find(call->handle);
  ShortcutSession* s =
      it == call->shortcuts->sessions.end() ? nullptr : it->second;
  gboolean allowed = FALSE;
  g_autoptr(GVariantIter) triggers = nullptr;
  if (reply) {
    g_variant_get(reply, "(bas)", &allowed, &triggers);
  }
  delete call;
  if (!s || !s->binding) {
    return;
  }
  GDBusMethodInvocation* invocation = s->binding;
  s->binding = nullptr;
  if (!allowed) {
    Unbind(s);
    s->pending.clear();
    Reply(invocation, reply ? kCanceled : kFailed, nullptr);
    return;
  }
  s->bound = std::move(s->pending);
  s->pending.clear();
  const gchar* trigger = nullptr;
  for (Shortcut& shortcut : s->bound) {
    if (!g_variant_iter_next(triggers, "&s", &trigger)) {
      break;
    }
    shortcut.trigger = trigger;
  }
  s->watch = g_unix_fd_add(s->fd,
                           static_cast<GIOCondition>(G_IO_IN | G_IO_HUP |
                                                     G_IO_ERR),
                           OnChromeMessage, s);
  syslog(LOG_INFO, "global shortcuts: %s bound %zu", LogName(s),
         s->bound.size());
  Reply(invocation, kSuccess, ShortcutResults(s->bound));
}

void BindShortcuts(GlobalShortcuts* shortcuts,
                   GDBusMethodInvocation* invocation,
                   GVariant* parameters) {
  const gchar* handle = nullptr;
  const gchar* session_handle = nullptr;
  const gchar* parent_window = nullptr;
  g_autoptr(GVariant) list = nullptr;
  g_autoptr(GVariant) options = nullptr;
  g_variant_get(parameters, "(&o&o@a(sa{sv})&s@a{sv})", &handle,
                &session_handle, &list, &parent_window, &options);
  auto it = shortcuts->sessions.find(session_handle);
  if (it == shortcuts->sessions.end() || it->second->binding ||
      !shortcuts->system) {
    Reply(invocation, kFailed, nullptr);
    return;
  }
  ShortcutSession* s = it->second;
  int pair[2];
  if (socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, pair) < 0) {
    Reply(invocation, kFailed, nullptr);
    return;
  }
  // Binding again replaces what the session had.
  Unbind(s);
  s->bound.clear();
  s->pending.clear();
  GVariantBuilder request;
  g_variant_builder_init(&request, G_VARIANT_TYPE("a(ssuu)"));
  GVariantIter iter;
  g_variant_iter_init(&iter, list);
  const gchar* id = nullptr;
  GVariant* props = nullptr;
  while (g_variant_iter_next(&iter, "(&s@a{sv})", &id, &props)) {
    Shortcut shortcut;
    shortcut.id = id;
    const gchar* text = nullptr;
    if (g_variant_lookup(props, "description", "&s", &text)) {
      shortcut.description = text;
    }
    guint32 modifiers = 0;
    guint32 evdev = 0;
    if (g_variant_lookup(props, "preferred_trigger", "&s", &text) &&
        !ParseTrigger(text, &modifiers, &evdev)) {
      syslog(LOG_INFO, "global shortcuts: %s: unknown keys \"%s\" for %s",
             LogName(s), text, id);
    }
    g_variant_builder_add(&request, "(ssuu)", shortcut.id.c_str(),
                          shortcut.description.c_str(), modifiers, evdev);
    s->pending.push_back(std::move(shortcut));
    g_variant_unref(props);
  }
  s->binding = invocation;
  s->fd = pair[0];
  g_autoptr(GUnixFDList) fds = g_unix_fd_list_new();
  g_unix_fd_list_append(fds, pair[1], nullptr);
  close(pair[1]);
  std::string name = AppName(s->app_id);
  // The user decides: no timeout.
  g_dbus_connection_call_with_unix_fd_list(
      shortcuts->system, vm_tools::apps::kVmApplicationsServiceName,
      vm_tools::apps::kVmApplicationsServicePath,
      vm_tools::apps::kVmApplicationsServiceInterface, kBindMethod,
      g_variant_new("(ss@a(ssuu)h)", s->app_id.c_str(), name.c_str(),
                    g_variant_builder_end(&request), 0),
      G_VARIANT_TYPE("(bas)"), G_DBUS_CALL_FLAGS_NONE, G_MAXINT, fds, nullptr,
      OnBound, new BindCall{shortcuts, session_handle});
}

}  // namespace

GlobalShortcuts* GlobalShortcutsNew(GDBusConnection* session,
                                    GDBusConnection* system,
                                    GDBusInterfaceInfo* session_info) {
  GlobalShortcuts* shortcuts = new GlobalShortcuts;
  shortcuts->session = session;
  shortcuts->system = system;
  shortcuts->session_info = session_info;
  return shortcuts;
}

void OnGlobalShortcutsCall(GDBusConnection*,
                           const gchar*,
                           const gchar*,
                           const gchar*,
                           const gchar* method_name,
                           GVariant* parameters,
                           GDBusMethodInvocation* invocation,
                           gpointer data) {
  GlobalShortcuts* shortcuts = static_cast<GlobalShortcuts*>(data);
  if (g_strcmp0(method_name, "CreateSession") == 0) {
    CreateSession(shortcuts, invocation, parameters);
  } else if (g_strcmp0(method_name, "BindShortcuts") == 0) {
    BindShortcuts(shortcuts, invocation, parameters);
  } else if (g_strcmp0(method_name, "ListShortcuts") == 0) {
    const gchar* session_handle = nullptr;
    g_variant_get(parameters, "(&o&o)", nullptr, &session_handle);
    auto it = shortcuts->sessions.find(session_handle);
    if (it == shortcuts->sessions.end()) {
      Reply(invocation, kFailed, nullptr);
    } else {
      Reply(invocation, kSuccess, ShortcutResults(it->second->bound));
    }
  } else {
    g_dbus_method_invocation_return_error(invocation, G_DBUS_ERROR,
                                          G_DBUS_ERROR_UNKNOWN_METHOD,
                                          "unknown method %s", method_name);
  }
}

GVariant* OnGlobalShortcutsProperty(GDBusConnection*,
                                    const gchar*,
                                    const gchar*,
                                    const gchar*,
                                    const gchar* property_name,
                                    GError** error,
                                    gpointer) {
  if (g_strcmp0(property_name, "version") == 0) {
    return g_variant_new_uint32(1);
  }
  g_set_error(error, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_PROPERTY,
              "unknown property %s", property_name);
  return nullptr;
}
