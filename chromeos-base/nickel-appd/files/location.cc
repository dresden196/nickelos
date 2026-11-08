// Distributed under the terms of the GNU General Public License v2
//
// The location for Linux apps, for nickel-appd: its GeoClue agent.
//
// GeoClue (app-misc/geoclue) gives apps the location only while the user
// has an agent, and no more precisely than the agent's MaxAccuracyLevel.
// nickel-appd is that agent ("nickel-appd" in /etc/geoclue/geoclue.conf),
// and it follows ChromeOS's location switch in Privacy controls (Chrome
// patch 0069, NickelLocationAllowed): exact while apps may use the
// location, none while they may not, which stops every app at once.
// GeoClue starts when an app asks and stops a minute after the last one;
// the agent registers each time it appears and asks Chrome every few
// seconds while it runs.
//
// Wi-Fi: GeoClue only reads the networks wpa_supplicant has seen (its
// D-Bus policy leaves scanning to shill), and between shill's own scans
// that is often just the one the device is on: too few for a position, so
// only the internet address's, kilometres off. So while GeoClue runs and
// apps may use the location, the agent asks shill for a Wi-Fi scan
// (Manager.RequestScan, as ChromeOS's own network settings do) when it
// appears and every minute; GeoClue sees the networks come and refines.
//
// Who is asked: apps outside a sandbox get the location without a question
// (GeoClue counts them as the system's; the Location portal does too).
// Flatpak apps are asked once by the Location portal (ChromeOS's dialog,
// Chrome 0069); a Flatpak app that talks to GeoClue itself comes here
// (AuthorizeApp), and ChromeOS's dialog asks each time.

#include "location.h"

#include <gio/gdesktopappinfo.h>
#include <syslog.h>

#include <string>

#include <chromeos/dbus/vm_applications/dbus-constants.h>

namespace {

constexpr char kGeoClue[] = "org.freedesktop.GeoClue2";
constexpr char kManagerPath[] = "/org/freedesktop/GeoClue2/Manager";
constexpr char kManagerInterface[] = "org.freedesktop.GeoClue2.Manager";
constexpr char kAgentPath[] = "/org/freedesktop/GeoClue2/Agent";
constexpr char kAgentInterface[] = "org.freedesktop.GeoClue2.Agent";
constexpr char kAgentId[] = "nickel-appd";
// GClueAccuracyLevel.
constexpr guint32 kNone = 0;
constexpr guint32 kExact = 8;
constexpr guint kCheckSeconds = 3;
constexpr guint kScanSeconds = 60;
constexpr char kShill[] = "org.chromium.flimflam";
constexpr char kShillManagerInterface[] = "org.chromium.flimflam.Manager";

constexpr char kIntrospection[] = R"(<node>
  <interface name="org.freedesktop.GeoClue2.Agent">
    <method name="AuthorizeApp">
      <arg name="desktop_id" type="s" direction="in"/>
      <arg name="req_accuracy_level" type="u" direction="in"/>
      <arg name="authorized" type="b" direction="out"/>
      <arg name="allowed_accuracy_level" type="u" direction="out"/>
    </method>
    <property name="MaxAccuracyLevel" type="u" access="read"/>
  </interface>
</node>)";

struct Agent {
  GDBusConnection* system = nullptr;
  bool allowed = false;  // As Chrome last said.
  bool running = false;  // GeoClue is on the bus.
  bool checking = false;
  guint check = 0;
  guint scan = 0;
  bool scan_refused = false;  // Said once per GeoClue run (no Wi-Fi).
};

void CallChrome(Agent* agent,
                const char* method,
                GVariant* args,
                const char* reply_type,
                GAsyncReadyCallback callback,
                gpointer data) {
  g_dbus_connection_call(
      agent->system, vm_tools::apps::kVmApplicationsServiceName,
      vm_tools::apps::kVmApplicationsServicePath,
      vm_tools::apps::kVmApplicationsServiceInterface, method, args,
      G_VARIANT_TYPE(reply_type), G_DBUS_CALL_FLAGS_NONE,
      reply_type[1] == 's' ? G_MAXINT : -1, nullptr, callback, data);
}

void OnAgentAdded(GObject* source, GAsyncResult* result, gpointer data) {
  g_autoptr(GError) error = nullptr;
  g_autoptr(GVariant) reply = g_dbus_connection_call_finish(
      G_DBUS_CONNECTION(source), result, &error);
  if (!reply) {
    syslog(LOG_WARNING, "location: GeoClue AddAgent: %s", error->message);
  }
}

void OnScanRequested(GObject* source, GAsyncResult* result, gpointer data) {
  Agent* agent = static_cast<Agent*>(data);
  g_autoptr(GError) error = nullptr;
  g_autoptr(GVariant) reply = g_dbus_connection_call_finish(
      G_DBUS_CONNECTION(source), result, &error);
  if (!reply && !agent->scan_refused) {
    agent->scan_refused = true;
    syslog(LOG_INFO, "location: no Wi-Fi scan: %s", error->message);
  }
}

// A Wi-Fi scan by shill, while GeoClue runs and apps may use the location.
void RequestScan(Agent* agent) {
  if (!agent->running || !agent->allowed) {
    return;
  }
  g_dbus_connection_call(agent->system, kShill, "/", kShillManagerInterface,
                         "RequestScan", g_variant_new("(s)", "wifi"), nullptr,
                         G_DBUS_CALL_FLAGS_NONE, -1, nullptr, OnScanRequested,
                         agent);
}

gboolean OnScanTimer(gpointer data) {
  RequestScan(static_cast<Agent*>(data));
  return G_SOURCE_CONTINUE;
}

void SetAllowed(Agent* agent, bool allowed) {
  if (agent->allowed == allowed) {
    return;
  }
  agent->allowed = allowed;
  syslog(LOG_INFO, "location: %s for apps",
         allowed ? "allowed" : "turned off");
  GVariantBuilder changed;
  g_variant_builder_init(&changed, G_VARIANT_TYPE("a{sv}"));
  g_variant_builder_add(&changed, "{sv}", "MaxAccuracyLevel",
                        g_variant_new_uint32(allowed ? kExact : kNone));
  g_dbus_connection_emit_signal(
      agent->system, nullptr, kAgentPath, "org.freedesktop.DBus.Properties",
      "PropertiesChanged",
      g_variant_new("(sa{sv}as)", kAgentInterface, &changed, nullptr),
      nullptr);
  if (allowed) {
    RequestScan(agent);
  }
}

// Chrome's answer to NickelLocationAllowed; |register_after| when GeoClue
// has just appeared (it reads MaxAccuracyLevel once the agent is added).
struct Check {
  Agent* agent;
  bool register_after;
};

void OnChecked(GObject* source, GAsyncResult* result, gpointer data) {
  Check* check = static_cast<Check*>(data);
  Agent* agent = check->agent;
  agent->checking = false;
  g_autoptr(GError) error = nullptr;
  g_autoptr(GVariant) reply = g_dbus_connection_call_finish(
      G_DBUS_CONNECTION(source), result, &error);
  gboolean allowed = FALSE;
  if (reply) {
    g_variant_get(reply, "(b)", &allowed);
  } else {
    // No answer, no location.
    syslog(LOG_WARNING, "location: NickelLocationAllowed: %s",
           error->message);
  }
  SetAllowed(agent, allowed);
  if (check->register_after && agent->running) {
    g_dbus_connection_call(agent->system, kGeoClue, kManagerPath,
                           kManagerInterface, "AddAgent",
                           g_variant_new("(s)", kAgentId), nullptr,
                           G_DBUS_CALL_FLAGS_NONE, -1, nullptr, OnAgentAdded,
                           nullptr);
    RequestScan(agent);
  }
  delete check;
}

void CheckChrome(Agent* agent, bool register_after) {
  agent->checking = true;
  CallChrome(agent, "NickelLocationAllowed", nullptr, "(b)", OnChecked,
             new Check{agent, register_after});
}

gboolean OnCheckTimer(gpointer data) {
  Agent* agent = static_cast<Agent*>(data);
  if (!agent->checking) {
    CheckChrome(agent, false);
  }
  return G_SOURCE_CONTINUE;
}

void OnGeoClueAppeared(GDBusConnection*,
                       const gchar*,
                       const gchar*,
                       gpointer data) {
  Agent* agent = static_cast<Agent*>(data);
  agent->running = true;
  agent->scan_refused = false;
  CheckChrome(agent, true);
  if (!agent->check) {
    agent->check = g_timeout_add_seconds(kCheckSeconds, OnCheckTimer, agent);
  }
  if (!agent->scan) {
    agent->scan = g_timeout_add_seconds(kScanSeconds, OnScanTimer, agent);
  }
}

void OnGeoClueVanished(GDBusConnection*, const gchar*, gpointer data) {
  Agent* agent = static_cast<Agent*>(data);
  agent->running = false;
  if (agent->check) {
    g_source_remove(agent->check);
    agent->check = 0;
  }
  if (agent->scan) {
    g_source_remove(agent->scan);
    agent->scan = 0;
  }
}

struct Authorize {
  GDBusMethodInvocation* invocation;
  guint32 accuracy;
};

void OnAuthorizeAnswered(GObject* source, GAsyncResult* result, gpointer data) {
  Authorize* authorize = static_cast<Authorize*>(data);
  g_autoptr(GError) error = nullptr;
  g_autoptr(GVariant) reply = g_dbus_connection_call_finish(
      G_DBUS_CONNECTION(source), result, &error);
  const gchar* answer = "";
  if (reply) {
    g_variant_get(reply, "(&s)", &answer);
  } else {
    syslog(LOG_WARNING, "location: NickelAccessAsk: %s", error->message);
  }
  const bool allowed = g_strcmp0(answer, "allow") == 0;
  g_dbus_method_invocation_return_value(
      authorize->invocation,
      g_variant_new("(bu)", allowed, allowed ? authorize->accuracy : kNone));
  delete authorize;
}

void OnMethodCall(GDBusConnection*,
                  const gchar*,
                  const gchar*,
                  const gchar*,
                  const gchar* method_name,
                  GVariant* parameters,
                  GDBusMethodInvocation* invocation,
                  gpointer data) {
  Agent* agent = static_cast<Agent*>(data);
  if (g_strcmp0(method_name, "AuthorizeApp") != 0) {
    g_dbus_method_invocation_return_error(invocation, G_DBUS_ERROR,
                                          G_DBUS_ERROR_UNKNOWN_METHOD,
                                          "unknown method %s", method_name);
    return;
  }
  const gchar* desktop_id = nullptr;
  guint32 accuracy = kNone;
  g_variant_get(parameters, "(&su)", &desktop_id, &accuracy);
  if (!agent->allowed) {
    g_dbus_method_invocation_return_value(invocation,
                                          g_variant_new("(bu)", FALSE, kNone));
    return;
  }
  std::string name;
  const std::string file = std::string(desktop_id) + ".desktop";
  g_autoptr(GDesktopAppInfo) info = g_desktop_app_info_new(file.c_str());
  if (info) {
    name = g_app_info_get_display_name(G_APP_INFO(info));
  }
  // ChromeOS's location question (0069), the user decides: no timeout.
  CallChrome(agent, "NickelAccessAsk",
             g_variant_new("(sssssss)", desktop_id, name.c_str(), "", "", "",
                           "", "find-location-symbolic"),
             "(s)", OnAuthorizeAnswered, new Authorize{invocation, accuracy});
}

GVariant* OnGetProperty(GDBusConnection*,
                        const gchar*,
                        const gchar*,
                        const gchar*,
                        const gchar* property_name,
                        GError** error,
                        gpointer data) {
  Agent* agent = static_cast<Agent*>(data);
  if (g_strcmp0(property_name, "MaxAccuracyLevel") != 0) {
    g_set_error(error, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_PROPERTY,
                "unknown property %s", property_name);
    return nullptr;
  }
  return g_variant_new_uint32(agent->allowed ? kExact : kNone);
}

}  // namespace

void LocationAgentStart(GDBusConnection* system) {
  Agent* agent = new Agent;
  agent->system = system;
  g_autoptr(GError) error = nullptr;
  GDBusNodeInfo* node = g_dbus_node_info_new_for_xml(kIntrospection, nullptr);
  static const GDBusInterfaceVTable kVTable = {OnMethodCall, OnGetProperty,
                                               nullptr};
  if (!g_dbus_connection_register_object(system, kAgentPath,
                                         node->interfaces[0], &kVTable, agent,
                                         nullptr, &error)) {
    syslog(LOG_WARNING, "location: cannot export %s: %s", kAgentPath,
           error->message);
    return;
  }
  // Not started for this: GeoClue starts when an app asks.
  g_bus_watch_name_on_connection(system, kGeoClue, G_BUS_NAME_WATCHER_FLAGS_NONE,
                                 OnGeoClueAppeared, OnGeoClueVanished, agent,
                                 nullptr);
}
