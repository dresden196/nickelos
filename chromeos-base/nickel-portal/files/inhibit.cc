// Distributed under the terms of the GNU General Public License v2
//
// Inhibit (org.freedesktop.impl.portal.Inhibit) and org.freedesktop.ScreenSaver
// for xdg-desktop-portal-nickel: apps keeping the screen on (a video plays, a
// game runs) or the device awake (a download). Sandboxed apps ask the
// portal; native ones, and Flatpaks allowed to talk to it (SDL games,
// Electron apps, mpv, VLC), ask org.freedesktop.ScreenSaver, which this also
// owns. Chrome holds a ChromeOS wake lock for each
// (VmApplicationsService.NickelInhibit, Chrome patch 0063) for as long as the
// socket it is given stays open: closed when the app ends the inhibition,
// when the app leaves the bus, or when this process dies.
//
// Portal flags: idle (8) keeps the screen on, suspend (4) the device awake;
// logout (1) and user switch (2) are not ChromeOS's to hold up and are
// ignored. org.freedesktop.ScreenSaver.Inhibit means idle.
//
// GTK also asks for a monitor of the session's state (CreateMonitor): the
// session is always running and the screen saver never active.

#include "inhibit.h"

#include <chromeos/dbus/vm_applications/dbus-constants.h>
#include <gio/gdesktopappinfo.h>
#include <gio/gunixfdlist.h>
#include <sys/socket.h>
#include <syslog.h>
#include <unistd.h>

#include <map>
#include <string>

namespace {

constexpr char kInhibitMethod[] = "NickelInhibit";
constexpr char kScreenSaverName[] = "org.freedesktop.ScreenSaver";
constexpr char kScreenSaverInterface[] = "org.freedesktop.ScreenSaver";
// Both are used: KDE's path and the spec's.
constexpr const char* kScreenSaverPaths[] = {"/org/freedesktop/ScreenSaver",
                                             "/ScreenSaver"};
// Portal flags.
constexpr guint32 kFlagSuspend = 4;
constexpr guint32 kFlagIdle = 8;
// Portal responses.
constexpr guint32 kSuccess = 0;

constexpr char kIntrospection[] =
    "<node>"
    "  <interface name='org.freedesktop.impl.portal.Request'>"
    "    <method name='Close'/>"
    "  </interface>"
    "  <interface name='org.freedesktop.impl.portal.Session'>"
    "    <method name='Close'/>"
    "    <signal name='Closed'/>"
    "    <property name='version' type='u' access='read'/>"
    "  </interface>"
    "  <interface name='org.freedesktop.ScreenSaver'>"
    "    <method name='Inhibit'>"
    "      <arg type='s' name='application_name' direction='in'/>"
    "      <arg type='s' name='reason_for_inhibit' direction='in'/>"
    "      <arg type='u' name='cookie' direction='out'/>"
    "    </method>"
    "    <method name='UnInhibit'>"
    "      <arg type='u' name='cookie' direction='in'/>"
    "    </method>"
    "    <method name='GetActive'>"
    "      <arg type='b' name='active' direction='out'/>"
    "    </method>"
    "    <method name='SimulateUserActivity'/>"
    "  </interface>"
    "</node>";

// One inhibition: Chrome's wake lock lasts while |fd| is open.
struct Hold {
  int fd = -1;
  guint object = 0;     // the portal's request object, if any
  std::string sender;   // org.freedesktop.ScreenSaver's caller, if any
};

}  // namespace

struct Inhibit {
  GDBusConnection* session = nullptr;
  GDBusConnection* system = nullptr;
  GDBusNodeInfo* node = nullptr;
  // Portal inhibitions by request handle; ScreenSaver ones by cookie.
  std::map<std::string, Hold> requests;
  std::map<guint32, Hold> cookies;
  guint32 next_cookie = 1;
  // ScreenSaver callers watched for leaving the bus, by unique name.
  std::map<std::string, guint> watches;
  // Monitors (session objects), by session handle.
  std::map<std::string, guint> monitors;
};

namespace {

std::string AppName(const std::string& app_id) {
  if (app_id.empty()) {
    return "";
  }
  std::string desktop_id = app_id + ".desktop";
  g_autoptr(GDesktopAppInfo) info = g_desktop_app_info_new(desktop_id.c_str());
  return info ? g_app_info_get_display_name(G_APP_INFO(info)) : app_id;
}

void Release(Hold* hold) {
  if (hold->fd >= 0) {
    close(hold->fd);
    hold->fd = -1;
  }
}

// Asks Chrome for a wake lock; returns our end of its socket, -1 if none.
// The call is asynchronous: Chrome takes the lock when it gets it, and drops
// it when the socket closes, whichever comes first.
int TakeWakeLock(Inhibit* inhibit,
                 const std::string& app_id,
                 const std::string& app_name,
                 const std::string& reason,
                 guint32 flags) {
  if (!inhibit->system) {
    return -1;
  }
  int pair[2];
  if (socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, pair) < 0) {
    return -1;
  }
  g_autoptr(GUnixFDList) fds = g_unix_fd_list_new();
  g_unix_fd_list_append(fds, pair[1], nullptr);
  close(pair[1]);
  g_dbus_connection_call_with_unix_fd_list(
      inhibit->system, vm_tools::apps::kVmApplicationsServiceName,
      vm_tools::apps::kVmApplicationsServicePath,
      vm_tools::apps::kVmApplicationsServiceInterface, kInhibitMethod,
      g_variant_new("(sssuh)", app_id.c_str(), app_name.c_str(),
                    reason.c_str(), flags, 0),
      nullptr, G_DBUS_CALL_FLAGS_NONE, -1, fds, nullptr,
      [](GObject* source, GAsyncResult* result, gpointer) {
        GError* error = nullptr;
        GVariant* reply = g_dbus_connection_call_with_unix_fd_list_finish(
            G_DBUS_CONNECTION(source), nullptr, result, &error);
        if (reply) {
          g_variant_unref(reply);
        } else {
          syslog(LOG_WARNING, "inhibit: %s: %s", kInhibitMethod,
                 error->message);
          g_error_free(error);
        }
      },
      nullptr);
  syslog(LOG_INFO, "inhibit: %s (%s)%s%s",
         app_name.empty() ? app_id.c_str() : app_name.c_str(), reason.c_str(),
         (flags & kFlagIdle) ? " idle" : "",
         (flags & kFlagSuspend) ? " suspend" : "");
  return pair[0];
}

// --- Portal request objects: Close ends the inhibition. -------------------

void OnRequestCall(GDBusConnection*,
                   const gchar*,
                   const gchar* object_path,
                   const gchar*,
                   const gchar*,
                   GVariant*,
                   GDBusMethodInvocation* invocation,
                   gpointer data) {
  Inhibit* inhibit = static_cast<Inhibit*>(data);
  auto it = inhibit->requests.find(object_path);
  if (it != inhibit->requests.end()) {
    Release(&it->second);
    g_dbus_connection_unregister_object(inhibit->session, it->second.object);
    inhibit->requests.erase(it);
  }
  g_dbus_method_invocation_return_value(invocation, nullptr);
}

// Inhibit(o handle, s app_id, s window, u flags, a{sv} options).
void PortalInhibit(Inhibit* inhibit,
                   GDBusMethodInvocation* invocation,
                   GVariant* parameters) {
  const gchar* handle = nullptr;
  const gchar* app_id = nullptr;
  guint32 flags = 0;
  g_autoptr(GVariant) options = nullptr;
  g_variant_get(parameters, "(&o&s&su@a{sv})", &handle, &app_id, nullptr,
                &flags, &options);
  const gchar* reason = "";
  g_variant_lookup(options, "reason", "&s", &reason);
  if (flags & (kFlagIdle | kFlagSuspend)) {
    Hold hold;
    hold.fd = TakeWakeLock(inhibit, app_id, AppName(app_id), reason, flags);
    static const GDBusInterfaceVTable kRequestVTable = {OnRequestCall,
                                                        nullptr, nullptr};
    hold.object = g_dbus_connection_register_object(
        inhibit->session, handle,
        g_dbus_node_info_lookup_interface(
            inhibit->node, "org.freedesktop.impl.portal.Request"),
        &kRequestVTable, inhibit, nullptr, nullptr);
    if (hold.object) {
      inhibit->requests[handle] = hold;
    } else {
      Release(&hold);
    }
  }
  g_dbus_method_invocation_return_value(invocation, nullptr);
}

// --- Monitors ---------------------------------------------------------------

void OnSessionCall(GDBusConnection*,
                   const gchar*,
                   const gchar* object_path,
                   const gchar*,
                   const gchar*,
                   GVariant*,
                   GDBusMethodInvocation* invocation,
                   gpointer data) {
  Inhibit* inhibit = static_cast<Inhibit*>(data);
  auto it = inhibit->monitors.find(object_path);
  if (it != inhibit->monitors.end()) {
    g_dbus_connection_unregister_object(inhibit->session, it->second);
    inhibit->monitors.erase(it);
  }
  g_dbus_method_invocation_return_value(invocation, nullptr);
}

GVariant* OnSessionProperty(GDBusConnection*,
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

// CreateMonitor(o handle, o session_handle, s app_id, s window) -> u.
void CreateMonitor(Inhibit* inhibit,
                   GDBusMethodInvocation* invocation,
                   GVariant* parameters) {
  const gchar* session_handle = nullptr;
  g_variant_get(parameters, "(&o&o&s&s)", nullptr, &session_handle, nullptr,
                nullptr);
  static const GDBusInterfaceVTable kSessionVTable = {
      OnSessionCall, OnSessionProperty, nullptr};
  guint object = g_dbus_connection_register_object(
      inhibit->session, session_handle,
      g_dbus_node_info_lookup_interface(
          inhibit->node, "org.freedesktop.impl.portal.Session"),
      &kSessionVTable, inhibit, nullptr, nullptr);
  if (object) {
    inhibit->monitors[session_handle] = object;
  }
  g_dbus_method_invocation_return_value(invocation,
                                        g_variant_new("(u)", kSuccess));
  // The state, once: always running, the screen saver never on.
  GVariantBuilder state;
  g_variant_builder_init(&state, G_VARIANT_TYPE("a{sv}"));
  g_variant_builder_add(&state, "{sv}", "screensaver-active",
                        g_variant_new_boolean(FALSE));
  g_variant_builder_add(&state, "{sv}", "session-state",
                        g_variant_new_uint32(1));
  g_dbus_connection_emit_signal(
      inhibit->session, nullptr, "/org/freedesktop/portal/desktop",
      "org.freedesktop.impl.portal.Inhibit", "StateChanged",
      g_variant_new("(oa{sv})", session_handle, &state), nullptr);
}

// --- org.freedesktop.ScreenSaver ---------------------------------------------

void DropSender(Inhibit* inhibit, const std::string& sender) {
  for (auto it = inhibit->cookies.begin(); it != inhibit->cookies.end();) {
    if (it->second.sender == sender) {
      Release(&it->second);
      it = inhibit->cookies.erase(it);
    } else {
      ++it;
    }
  }
  auto watch = inhibit->watches.find(sender);
  if (watch != inhibit->watches.end()) {
    g_bus_unwatch_name(watch->second);
    inhibit->watches.erase(watch);
  }
}

void OnScreenSaverCall(GDBusConnection*,
                       const gchar* sender,
                       const gchar*,
                       const gchar*,
                       const gchar* method_name,
                       GVariant* parameters,
                       GDBusMethodInvocation* invocation,
                       gpointer data) {
  Inhibit* inhibit = static_cast<Inhibit*>(data);
  if (g_strcmp0(method_name, "Inhibit") == 0) {
    const gchar* app = nullptr;
    const gchar* reason = nullptr;
    g_variant_get(parameters, "(&s&s)", &app, &reason);
    Hold hold;
    hold.fd = TakeWakeLock(inhibit, app, app, reason, kFlagIdle);
    hold.sender = sender;
    const guint32 cookie = inhibit->next_cookie++;
    inhibit->cookies[cookie] = hold;
    // An app that quits or crashes without UnInhibit lets go too.
    if (!inhibit->watches.count(sender)) {
      inhibit->watches[sender] = g_bus_watch_name_on_connection(
          inhibit->session, sender, G_BUS_NAME_WATCHER_FLAGS_NONE, nullptr,
          [](GDBusConnection*, const gchar* name, gpointer data) {
            DropSender(static_cast<Inhibit*>(data), name);
          },
          inhibit, nullptr);
    }
    g_dbus_method_invocation_return_value(invocation,
                                          g_variant_new("(u)", cookie));
  } else if (g_strcmp0(method_name, "UnInhibit") == 0) {
    guint32 cookie = 0;
    g_variant_get(parameters, "(u)", &cookie);
    auto it = inhibit->cookies.find(cookie);
    if (it != inhibit->cookies.end() && it->second.sender == sender) {
      Release(&it->second);
      inhibit->cookies.erase(it);
    }
    g_dbus_method_invocation_return_value(invocation, nullptr);
  } else if (g_strcmp0(method_name, "GetActive") == 0) {
    g_dbus_method_invocation_return_value(invocation,
                                          g_variant_new("(b)", FALSE));
  } else if (g_strcmp0(method_name, "SimulateUserActivity") == 0) {
    g_dbus_method_invocation_return_value(invocation, nullptr);
  } else {
    g_dbus_method_invocation_return_error(invocation, G_DBUS_ERROR,
                                          G_DBUS_ERROR_UNKNOWN_METHOD,
                                          "unknown method %s", method_name);
  }
}

}  // namespace

Inhibit* InhibitNew(GDBusConnection* session, GDBusConnection* system) {
  Inhibit* inhibit = new Inhibit;
  inhibit->session = session;
  inhibit->system = system;
  inhibit->node = g_dbus_node_info_new_for_xml(kIntrospection, nullptr);
  static const GDBusInterfaceVTable kScreenSaverVTable = {
      OnScreenSaverCall, nullptr, nullptr};
  for (const char* path : kScreenSaverPaths) {
    g_dbus_connection_register_object(
        session, path,
        g_dbus_node_info_lookup_interface(inhibit->node,
                                          kScreenSaverInterface),
        &kScreenSaverVTable, inhibit, nullptr, nullptr);
  }
  g_bus_own_name_on_connection(session, kScreenSaverName,
                               G_BUS_NAME_OWNER_FLAGS_NONE, nullptr, nullptr,
                               nullptr, nullptr);
  return inhibit;
}

void OnInhibitCall(GDBusConnection*,
                   const gchar*,
                   const gchar*,
                   const gchar*,
                   const gchar* method_name,
                   GVariant* parameters,
                   GDBusMethodInvocation* invocation,
                   gpointer data) {
  Inhibit* inhibit = static_cast<Inhibit*>(data);
  if (g_strcmp0(method_name, "Inhibit") == 0) {
    PortalInhibit(inhibit, invocation, parameters);
  } else if (g_strcmp0(method_name, "CreateMonitor") == 0) {
    CreateMonitor(inhibit, invocation, parameters);
  } else if (g_strcmp0(method_name, "QueryEndResponse") == 0) {
    g_dbus_method_invocation_return_value(invocation, nullptr);
  } else {
    g_dbus_method_invocation_return_error(invocation, G_DBUS_ERROR,
                                          G_DBUS_ERROR_UNKNOWN_METHOD,
                                          "unknown method %s", method_name);
  }
}

GVariant* OnInhibitProperty(GDBusConnection*,
                            const gchar*,
                            const gchar*,
                            const gchar*,
                            const gchar* property_name,
                            GError** error,
                            gpointer) {
  if (g_strcmp0(property_name, "version") == 0) {
    return g_variant_new_uint32(3);
  }
  g_set_error(error, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_PROPERTY,
              "unknown property %s", property_name);
  return nullptr;
}
