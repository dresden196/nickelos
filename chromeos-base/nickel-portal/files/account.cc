// Distributed under the terms of the GNU General Public License v2
//
// Account (org.freedesktop.impl.portal.Account) for
// xdg-desktop-portal-nickel: an app asks for the user's name and picture
// (GetUserInformation), to greet them or fill in a profile. ChromeOS's own
// dialog asks the user first (access.cc's AccessAsk), with the app's reason
// if it gives one; if they share, the app gets the Linux login (id), the
// account's full name (the passwd entry's, as NickelOS sets it from the
// ChromeOS account) and its picture: the copy Chrome keeps of the account
// picture, /home/chronos/<login>@nickel.localhost.png (a file: URI; for a
// Flatpak, xdg-desktop-portal hands it over through the document portal).

#include "account.h"

#include <pwd.h>
#include <syslog.h>
#include <unistd.h>

#include <string>

#include "access.h"

namespace {

// Portal responses.
constexpr guint32 kSuccess = 0;
constexpr guint32 kCanceled = 1;

struct Ask {
  GDBusMethodInvocation* invocation;
  std::string app_id;
};

void Reply(GDBusMethodInvocation* invocation,
           guint32 response,
           GVariantBuilder* results) {
  GVariant* dict = results ? g_variant_builder_end(results)
                           : g_variant_new_array(G_VARIANT_TYPE("{sv}"),
                                                 nullptr, 0);
  g_dbus_method_invocation_return_value(
      invocation, g_variant_new("(u@a{sv})", response, dict));
}

void OnAnswered(bool allowed, gpointer data) {
  Ask* ask = static_cast<Ask*>(data);
  syslog(LOG_INFO, "account: %s: %s", ask->app_id.c_str(),
         allowed ? "shared" : "not shared");
  if (!allowed) {
    Reply(ask->invocation, kCanceled, nullptr);
    delete ask;
    return;
  }
  const struct passwd* user = getpwuid(getuid());
  const std::string login = user ? user->pw_name : g_get_user_name();
  std::string name = user && user->pw_gecos ? user->pw_gecos : "";
  // The full name is the GECOS field's first part.
  name = name.substr(0, name.find(','));
  if (name.empty()) {
    name = g_get_real_name();
  }
  GVariantBuilder results;
  g_variant_builder_init(&results, G_VARIANT_TYPE("a{sv}"));
  g_variant_builder_add(&results, "{sv}", "id",
                        g_variant_new_string(login.c_str()));
  g_variant_builder_add(&results, "{sv}", "name",
                        g_variant_new_string(name.c_str()));
  const std::string picture =
      "/home/chronos/" + login + "@nickel.localhost.png";
  if (g_file_test(picture.c_str(), G_FILE_TEST_IS_REGULAR)) {
    g_autofree gchar* uri =
        g_filename_to_uri(picture.c_str(), nullptr, nullptr);
    if (uri) {
      g_variant_builder_add(&results, "{sv}", "image",
                            g_variant_new_string(uri));
    }
  }
  Reply(ask->invocation, kSuccess, &results);
  delete ask;
}

}  // namespace

struct Account {
  GDBusConnection* system = nullptr;
};

Account* AccountNew(GDBusConnection* system) {
  Account* account = new Account;
  account->system = system;
  return account;
}

// GetUserInformation(o handle, s app_id, s window, a{sv} options{reason})
//   -> (u response, a{sv} results{id, name, image})
void OnAccountCall(GDBusConnection*,
                   const gchar*,
                   const gchar*,
                   const gchar*,
                   const gchar* method_name,
                   GVariant* parameters,
                   GDBusMethodInvocation* invocation,
                   gpointer data) {
  if (g_strcmp0(method_name, "GetUserInformation") != 0) {
    g_dbus_method_invocation_return_error(invocation, G_DBUS_ERROR,
                                          G_DBUS_ERROR_UNKNOWN_METHOD,
                                          "unknown method %s", method_name);
    return;
  }
  Account* account = static_cast<Account*>(data);
  const gchar* app_id = nullptr;
  g_autoptr(GVariant) options = nullptr;
  g_variant_get(parameters, "(&o&s&s@a{sv})", nullptr, &app_id, nullptr,
                &options);
  const gchar* reason = nullptr;
  g_variant_lookup(options, "reason", "&s", &reason);
  const std::string name = AccessAppName(app_id);
  const std::string app = name.empty() ? "An app" : name;
  std::string text = app + " wants your name and account picture.";
  if (reason && *reason) {
    text += "\n\n";
    text += reason;
  }
  AccessAsk(account->system, app_id,
            "Share your name and picture with " +
                (name.empty() ? std::string("this app") : name) + "?",
            text, "Share", "Don't share", OnAnswered,
            new Ask{invocation, app_id});
}
