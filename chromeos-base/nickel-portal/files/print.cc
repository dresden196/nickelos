// Distributed under the terms of the GNU General Public License v2
//
// Print (org.freedesktop.impl.portal.Print) for xdg-desktop-portal-nickel:
// Flatpak apps print through ChromeOS's own print preview, with its printers
// (CUPS, the ones in Settings > Printers) and "Save as PDF".
//
// The portal asks twice. PreparePrint is where a desktop shows its print
// dialog: here it answers at once with the app's own page setup and settings
// (PDF output), so the app lays the pages out as it likes. Print hands over
// the laid-out document (PDF): Chrome opens it in ChromeOS's print preview
// (VmApplicationsService.NickelPrint, Chrome patch 0064), where the user
// picks the printer, the pages and the copies.

#include "print.h"

#include <chromeos/dbus/vm_applications/dbus-constants.h>
#include <gio/gdesktopappinfo.h>
#include <gio/gunixfdlist.h>
#include <syslog.h>
#include <unistd.h>

#include <string>

namespace {

constexpr char kPrintMethod[] = "NickelPrint";
// Portal responses.
constexpr guint32 kSuccess = 0;
constexpr guint32 kFailed = 2;

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

struct Print {
  GDBusConnection* system = nullptr;
  guint32 next_token = 1;
};

namespace {

// PreparePrint(o handle, s app_id, s parent_window, s title, a{sv} settings,
// a{sv} page_setup, a{sv} options) -> (u response, a{sv} results).
void PreparePrint(Print* print,
                  GDBusMethodInvocation* invocation,
                  GVariant* parameters) {
  g_autoptr(GVariant) settings = nullptr;
  g_autoptr(GVariant) page_setup = nullptr;
  g_variant_get(parameters, "(&o&s&s&s@a{sv}@a{sv}@a{sv})", nullptr, nullptr,
                nullptr, nullptr, &settings, &page_setup, nullptr);
  // The app's settings, printing to a PDF (what ChromeOS's print preview
  // takes).
  GVariantBuilder out_settings;
  g_variant_builder_init(&out_settings, G_VARIANT_TYPE("a{sv}"));
  GVariantIter iter;
  g_variant_iter_init(&iter, settings);
  const gchar* key = nullptr;
  GVariant* value = nullptr;
  while (g_variant_iter_next(&iter, "{&sv}", &key, &value)) {
    if (g_strcmp0(key, "output-file-format") != 0) {
      g_variant_builder_add(&out_settings, "{sv}", key, value);
    }
    g_variant_unref(value);
  }
  g_variant_builder_add(&out_settings, "{sv}", "output-file-format",
                        g_variant_new_string("pdf"));
  GVariantBuilder results;
  g_variant_builder_init(&results, G_VARIANT_TYPE("a{sv}"));
  g_variant_builder_add(&results, "{sv}", "settings",
                        g_variant_builder_end(&out_settings));
  g_variant_builder_add(&results, "{sv}", "page-setup", page_setup);
  g_variant_builder_add(&results, "{sv}", "token",
                        g_variant_new_uint32(print->next_token++));
  Reply(invocation, kSuccess, g_variant_builder_end(&results));
}

struct PrintCall {
  GDBusMethodInvocation* invocation;
};

// Print(o handle, s app_id, s parent_window, s title, h fd, a{sv} options)
// -> (u response, a{sv} results).
void PrintDocument(Print* print,
                   GDBusMethodInvocation* invocation,
                   GVariant* parameters) {
  const gchar* app_id = nullptr;
  const gchar* title = nullptr;
  gint32 fd_index = -1;
  g_variant_get(parameters, "(&o&s&s&sh@a{sv})", nullptr, &app_id, nullptr,
                &title, &fd_index, nullptr);
  GDBusMessage* message = g_dbus_method_invocation_get_message(invocation);
  GUnixFDList* in_fds = g_dbus_message_get_unix_fd_list(message);
  int fd = in_fds ? g_unix_fd_list_get(in_fds, fd_index, nullptr) : -1;
  if (fd < 0 || !print->system) {
    if (fd >= 0) {
      close(fd);
    }
    Reply(invocation, kFailed, nullptr);
    return;
  }
  g_autoptr(GUnixFDList) fds = g_unix_fd_list_new();
  g_unix_fd_list_append(fds, fd, nullptr);
  close(fd);
  const std::string name = AppName(app_id);
  syslog(LOG_INFO, "print: %s: \"%s\"", app_id, title);
  g_dbus_connection_call_with_unix_fd_list(
      print->system, vm_tools::apps::kVmApplicationsServiceName,
      vm_tools::apps::kVmApplicationsServicePath,
      vm_tools::apps::kVmApplicationsServiceInterface, kPrintMethod,
      g_variant_new("(ssh)", name.c_str(), title, 0), G_VARIANT_TYPE("(b)"),
      G_DBUS_CALL_FLAGS_NONE, -1, fds, nullptr,
      [](GObject* source, GAsyncResult* result, gpointer data) {
        PrintCall* call = static_cast<PrintCall*>(data);
        GError* error = nullptr;
        GVariant* reply = g_dbus_connection_call_with_unix_fd_list_finish(
            G_DBUS_CONNECTION(source), nullptr, result, &error);
        gboolean shown = FALSE;
        if (reply) {
          g_variant_get(reply, "(b)", &shown);
          g_variant_unref(reply);
        } else {
          syslog(LOG_WARNING, "print: %s: %s", kPrintMethod, error->message);
          g_error_free(error);
        }
        // Printing itself is ChromeOS's from here: the app is done.
        Reply(call->invocation, shown ? kSuccess : kFailed, nullptr);
        delete call;
      },
      new PrintCall{invocation});
}

}  // namespace

Print* PrintNew(GDBusConnection* system) {
  Print* print = new Print;
  print->system = system;
  return print;
}

void OnPrintCall(GDBusConnection*,
                 const gchar*,
                 const gchar*,
                 const gchar*,
                 const gchar* method_name,
                 GVariant* parameters,
                 GDBusMethodInvocation* invocation,
                 gpointer data) {
  Print* print = static_cast<Print*>(data);
  if (g_strcmp0(method_name, "PreparePrint") == 0) {
    PreparePrint(print, invocation, parameters);
  } else if (g_strcmp0(method_name, "Print") == 0) {
    PrintDocument(print, invocation, parameters);
  } else {
    g_dbus_method_invocation_return_error(invocation, G_DBUS_ERROR,
                                          G_DBUS_ERROR_UNKNOWN_METHOD,
                                          "unknown method %s", method_name);
  }
}

GVariant* OnPrintProperty(GDBusConnection*,
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
