// Distributed under the terms of the GNU General Public License v2
//
// Email (org.freedesktop.impl.portal.Email) for xdg-desktop-portal-nickel:
// an app asks to write an email (ComposeEmail, "Send by email" in a photo
// or document app): addresses, subject, body and files to attach, which
// xdg-desktop-portal turns into paths. The default mail app
// (x-scheme-handler/mailto) opens a new message with them: a Linux mail app
// if the user chose one, or ChromeOS's own handler, which opens the mailto:
// link in Chrome, on the web mail site registered for it. Thunderbird gets
// them through its -compose command line (it takes no attachments from
// mailto: links); other mail apps through a mailto: link with an attach=
// for each file (Evolution, Geary, Claws Mail and KMail take them). A web
// mail site can't be handed files: they are left out (logged).

#include "email.h"

#include <syslog.h>

#include <cstring>
#include <string>
#include <vector>

namespace {

// Portal responses.
constexpr guint32 kSuccess = 0;
constexpr guint32 kFailed = 2;
// ChromeOS's own handler (nickel-open), which opens links in Chrome.
constexpr char kChromeOSHandler[] = "org.nickelos.ChromeOS.desktop";

void Reply(GDBusMethodInvocation* invocation, guint32 response) {
  g_dbus_method_invocation_return_value(
      invocation,
      g_variant_new("(u@a{sv})", response,
                    g_variant_new_array(G_VARIANT_TYPE("{sv}"), nullptr, 0)));
}

std::string Lookup(GVariant* options, const char* key) {
  const gchar* value = nullptr;
  return g_variant_lookup(options, key, "&s", &value) ? value : "";
}

std::vector<std::string> LookupList(GVariant* options, const char* key) {
  std::vector<std::string> list;
  g_autofree const gchar** values = nullptr;
  if (g_variant_lookup(options, key, "^a&s", &values)) {
    for (const gchar** value = values; value && *value; ++value) {
      list.emplace_back(*value);
    }
  }
  return list;
}

std::string Join(const std::vector<std::string>& items,
                 const std::string& separator) {
  std::string joined;
  for (const std::string& item : items) {
    joined += (joined.empty() ? "" : separator) + item;
  }
  return joined;
}

// For a mailto: link; |allowed| are kept as they are ("@" in addresses).
std::string Escape(const std::string& text, const char* allowed = nullptr) {
  g_autofree gchar* escaped = g_uri_escape_string(text.c_str(), allowed, FALSE);
  return escaped;
}

std::string FileUri(const std::string& path) {
  g_autofree gchar* uri = g_filename_to_uri(path.c_str(), nullptr, nullptr);
  return uri ? uri : "";
}

struct Message {
  std::vector<std::string> to;
  std::vector<std::string> cc;
  std::vector<std::string> bcc;
  std::string subject;
  std::string body;
  std::vector<std::string> attachments;
};

// Thunderbird's -compose "to='…',cc='…',subject='…',body='…',attachment='…'".
std::string ComposeArgument(const Message& message) {
  std::vector<std::string> fields;
  auto field = [&fields](const char* key, const std::string& value) {
    if (!value.empty()) {
      fields.push_back(std::string(key) + "='" + value + "'");
    }
  };
  field("to", Join(message.to, ","));
  field("cc", Join(message.cc, ","));
  field("bcc", Join(message.bcc, ","));
  field("subject", message.subject);
  field("body", message.body);
  std::vector<std::string> files;
  for (const std::string& path : message.attachments) {
    files.push_back(FileUri(path));
  }
  field("attachment", Join(files, ","));
  return Join(fields, ",");
}

std::string MailtoUri(const Message& message, bool with_attachments) {
  std::vector<std::string> to;
  for (const std::string& address : message.to) {
    to.push_back(Escape(address, "@"));
  }
  std::vector<std::string> query;
  for (const std::string& address : message.cc) {
    query.push_back("cc=" + Escape(address, "@"));
  }
  for (const std::string& address : message.bcc) {
    query.push_back("bcc=" + Escape(address, "@"));
  }
  if (!message.subject.empty()) {
    query.push_back("subject=" + Escape(message.subject));
  }
  if (!message.body.empty()) {
    query.push_back("body=" + Escape(message.body));
  }
  if (with_attachments) {
    for (const std::string& path : message.attachments) {
      query.push_back("attach=" + Escape(path));
    }
  }
  std::string uri = "mailto:" + Join(to, ",");
  if (!query.empty()) {
    uri += "?" + Join(query, "&");
  }
  return uri;
}

}  // namespace

struct Email {};

Email* EmailNew() {
  return new Email;
}

// ComposeEmail(o handle, s app_id, s parent_window, a{sv} options{address,
// addresses, cc, bcc, subject, body, attachments}) -> (u response, a{sv})
void OnEmailCall(GDBusConnection*,
                 const gchar*,
                 const gchar*,
                 const gchar*,
                 const gchar* method_name,
                 GVariant* parameters,
                 GDBusMethodInvocation* invocation,
                 gpointer) {
  if (g_strcmp0(method_name, "ComposeEmail") != 0) {
    g_dbus_method_invocation_return_error(invocation, G_DBUS_ERROR,
                                          G_DBUS_ERROR_UNKNOWN_METHOD,
                                          "unknown method %s", method_name);
    return;
  }
  const gchar* app_id = nullptr;
  g_autoptr(GVariant) options = nullptr;
  g_variant_get(parameters, "(&o&s&s@a{sv})", nullptr, &app_id, nullptr,
                &options);
  Message message;
  if (!Lookup(options, "address").empty()) {
    message.to.push_back(Lookup(options, "address"));
  }
  for (const std::string& address : LookupList(options, "addresses")) {
    message.to.push_back(address);
  }
  message.cc = LookupList(options, "cc");
  message.bcc = LookupList(options, "bcc");
  message.subject = Lookup(options, "subject");
  message.body = Lookup(options, "body");
  message.attachments = LookupList(options, "attachments");

  g_autoptr(GAppInfo) handler =
      g_app_info_get_default_for_uri_scheme("mailto");
  if (!handler) {
    syslog(LOG_WARNING, "email: %s: no mail app", app_id);
    Reply(invocation, kFailed);
    return;
  }
  const char* id = g_app_info_get_id(handler);
  const char* executable = g_app_info_get_executable(handler);
  g_autofree gchar* program =
      executable ? g_path_get_basename(executable) : g_strdup("");
  g_autoptr(GError) error = nullptr;
  bool launched = false;
  if (strstr(program, "thunderbird")) {
    const std::string compose = ComposeArgument(message);
    const gchar* argv[] = {executable, "-compose", compose.c_str(), nullptr};
    launched = g_spawn_async(nullptr, const_cast<gchar**>(argv), nullptr,
                             G_SPAWN_SEARCH_PATH, nullptr, nullptr, nullptr,
                             &error);
  } else {
    const bool web = g_strcmp0(id, kChromeOSHandler) == 0;
    if (web && !message.attachments.empty()) {
      syslog(LOG_INFO,
             "email: %s: %zu attachments left out (web mail takes none)",
             app_id, message.attachments.size());
    }
    const std::string uri = MailtoUri(message, !web);
    GList uris = {const_cast<gchar*>(uri.c_str()), nullptr, nullptr};
    g_autoptr(GAppLaunchContext) context = g_app_launch_context_new();
    launched = g_app_info_launch_uris(handler, &uris, context, &error);
  }
  if (!launched) {
    syslog(LOG_WARNING, "email: %s: %s", id ? id : program,
           error ? error->message : "not started");
    Reply(invocation, kFailed);
    return;
  }
  syslog(LOG_INFO, "email: %s: a new message in %s", app_id,
         id ? id : program);
  Reply(invocation, kSuccess);
}
