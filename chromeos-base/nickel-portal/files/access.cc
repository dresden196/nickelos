// Distributed under the terms of the GNU General Public License v2
//
// Access (org.freedesktop.impl.portal.Access) for xdg-desktop-portal-nickel:
// xdg-desktop-portal's question whether an app may use something. On
// NickelOS that is the camera: the Camera portal, which xdg-desktop-portal
// offers only with an Access backend, asks before an app gets the webcams
// (PipeWire nodes, WirePlumber's V4L2 monitor). Chrome asks the user
// (VmApplicationsService.NickelAccessAsk, Chrome patch 0060) in ChromeOS's
// own dialog, and answers "allow", "deny" or "off": the camera is off in
// ChromeOS's privacy controls. xdg-desktop-portal remembers "allow" and
// "deny" for the app in its permission store, so each app is asked once;
// "off" (and a failed call) is answered with an error, which it does not
// remember, so the app is asked again once the camera is on.

#include "access.h"

#include <chromeos/dbus/vm_applications/dbus-constants.h>
#include <gio/gdesktopappinfo.h>
#include <syslog.h>

#include <string>

namespace {

constexpr char kAskMethod[] = "NickelAccessAsk";
constexpr char kNotAllowed[] = "org.freedesktop.portal.Error.NotAllowed";
// Portal responses.
constexpr guint32 kAllowed = 0;
constexpr guint32 kDenied = 1;

struct AskCall {
  GDBusMethodInvocation* invocation;
  std::string app_id;
};

}  // namespace

struct Access {
  GDBusConnection* system = nullptr;
};

namespace {

// The name the user knows the app by, for Chrome's dialog.
std::string AppName(const std::string& app_id) {
  if (app_id.empty()) {
    return "";
  }
  std::string desktop_id = app_id + ".desktop";
  g_autoptr(GDesktopAppInfo) info = g_desktop_app_info_new(desktop_id.c_str());
  return info ? g_app_info_get_display_name(G_APP_INFO(info)) : app_id;
}

std::string Lookup(GVariant* options, const char* key) {
  const gchar* value = nullptr;
  return g_variant_lookup(options, key, "&s", &value) ? value : "";
}

void Reply(GDBusMethodInvocation* invocation, guint32 response) {
  g_dbus_method_invocation_return_value(
      invocation, g_variant_new("(u@a{sv})", response,
                                g_variant_new_array(G_VARIANT_TYPE("{sv}"),
                                                    nullptr, 0)));
}

void OnAnswered(GObject* source, GAsyncResult* result, gpointer data) {
  AskCall* call = static_cast<AskCall*>(data);
  GError* error = nullptr;
  GVariant* reply =
      g_dbus_connection_call_finish(G_DBUS_CONNECTION(source), result, &error);
  std::string answer;
  if (reply) {
    const gchar* text = nullptr;
    g_variant_get(reply, "(&s)", &text);
    answer = text;
    g_variant_unref(reply);
  } else {
    syslog(LOG_WARNING, "access: %s: %s", kAskMethod, error->message);
    g_error_free(error);
  }
  syslog(LOG_INFO, "access: %s: %s", call->app_id.c_str(),
         answer.empty() ? "no answer" : answer.c_str());
  if (answer == "allow") {
    Reply(call->invocation, kAllowed);
  } else if (answer == "deny") {
    Reply(call->invocation, kDenied);
  } else {
    // Not remembered: asked again next time.
    g_dbus_method_invocation_return_dbus_error(
        call->invocation, kNotAllowed,
        answer == "off" ? "Turned off in ChromeOS's privacy controls"
                        : "Could not ask the user");
  }
  delete call;
}

// AccessDialog(o handle, s app_id, s parent_window, s title, s subtitle,
// s body, a{sv} options) -> (u response, a{sv} results).
void AccessDialog(Access* access,
                  GDBusMethodInvocation* invocation,
                  GVariant* parameters) {
  const gchar* app_id = nullptr;
  const gchar* title = nullptr;
  const gchar* subtitle = nullptr;
  const gchar* body = nullptr;
  g_autoptr(GVariant) options = nullptr;
  g_variant_get(parameters, "(&o&s&s&s&s&s@a{sv})", nullptr, &app_id, nullptr,
                &title, &subtitle, &body, &options);
  if (!access->system) {
    g_dbus_method_invocation_return_dbus_error(invocation, kNotAllowed,
                                               "No system bus");
    return;
  }
  std::string text = subtitle;
  if (*body) {
    text += text.empty() ? "" : "\n\n";
    text += body;
  }
  // The user decides: no timeout.
  g_dbus_connection_call(
      access->system, vm_tools::apps::kVmApplicationsServiceName,
      vm_tools::apps::kVmApplicationsServicePath,
      vm_tools::apps::kVmApplicationsServiceInterface, kAskMethod,
      g_variant_new("(sssssss)", app_id, AppName(app_id).c_str(), title,
                    text.c_str(), Lookup(options, "grant_label").c_str(),
                    Lookup(options, "deny_label").c_str(),
                    Lookup(options, "icon").c_str()),
      G_VARIANT_TYPE("(s)"), G_DBUS_CALL_FLAGS_NONE, G_MAXINT, nullptr,
      OnAnswered, new AskCall{invocation, app_id});
}

struct AskOther {
  AccessAnswer callback;
  gpointer data;
};

void OnOtherAnswered(GObject* source, GAsyncResult* result, gpointer data) {
  AskOther* ask = static_cast<AskOther*>(data);
  g_autoptr(GError) error = nullptr;
  g_autoptr(GVariant) reply =
      g_dbus_connection_call_finish(G_DBUS_CONNECTION(source), result, &error);
  const gchar* answer = "";
  if (reply) {
    g_variant_get(reply, "(&s)", &answer);
  } else {
    syslog(LOG_WARNING, "access: %s: %s", kAskMethod, error->message);
  }
  ask->callback(g_strcmp0(answer, "allow") == 0, ask->data);
  delete ask;
}

}  // namespace

void AccessAsk(GDBusConnection* system,
               const std::string& app_id,
               const std::string& title,
               const std::string& text,
               const std::string& grant_label,
               const std::string& deny_label,
               AccessAnswer callback,
               gpointer data) {
  if (!system) {
    callback(false, data);
    return;
  }
  // The user decides: no timeout. Chrome shows the words as they are given
  // (no icon: nothing but the camera has its own).
  g_dbus_connection_call(
      system, vm_tools::apps::kVmApplicationsServiceName,
      vm_tools::apps::kVmApplicationsServicePath,
      vm_tools::apps::kVmApplicationsServiceInterface, kAskMethod,
      g_variant_new("(sssssss)", app_id.c_str(), AppName(app_id).c_str(),
                    title.c_str(), text.c_str(), grant_label.c_str(),
                    deny_label.c_str(), ""),
      G_VARIANT_TYPE("(s)"), G_DBUS_CALL_FLAGS_NONE, G_MAXINT, nullptr,
      OnOtherAnswered, new AskOther{callback, data});
}

std::string AccessAppName(const std::string& app_id) {
  return AppName(app_id);
}

Access* AccessNew(GDBusConnection* system) {
  Access* access = new Access;
  access->system = system;
  return access;
}

void OnAccessCall(GDBusConnection*,
                  const gchar*,
                  const gchar*,
                  const gchar*,
                  const gchar* method_name,
                  GVariant* parameters,
                  GDBusMethodInvocation* invocation,
                  gpointer data) {
  Access* access = static_cast<Access*>(data);
  if (g_strcmp0(method_name, "AccessDialog") == 0) {
    AccessDialog(access, invocation, parameters);
  } else {
    g_dbus_method_invocation_return_error(invocation, G_DBUS_ERROR,
                                          G_DBUS_ERROR_UNKNOWN_METHOD,
                                          "unknown method %s", method_name);
  }
}

GVariant* OnAccessProperty(GDBusConnection*,
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
