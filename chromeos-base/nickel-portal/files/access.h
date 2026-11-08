// Distributed under the terms of the GNU General Public License v2
//
// Access for xdg-desktop-portal-nickel (access.cc).

#ifndef NICKEL_PORTAL_ACCESS_H_
#define NICKEL_PORTAL_ACCESS_H_

#include <gio/gio.h>

#include <string>

struct Access;

// For the other portals: asks the user in ChromeOS's dialog (Chrome's
// NickelAccessAsk) on behalf of |app_id|, with |title| and |text| and the
// buttons' labels ("" for Allow and Don't allow); |callback| gets whether
// they allowed it (a failed call is a no).
using AccessAnswer = void (*)(bool allowed, gpointer data);
void AccessAsk(GDBusConnection* system,
               const std::string& app_id,
               const std::string& title,
               const std::string& text,
               const std::string& grant_label,
               const std::string& deny_label,
               AccessAnswer callback,
               gpointer data);

// The name the user knows an app by (its desktop entry's), or its id.
std::string AccessAppName(const std::string& app_id);

Access* AccessNew(GDBusConnection* system);

// org.freedesktop.impl.portal.Access, exported by the caller on the portal's
// object with |access| as user data.
void OnAccessCall(GDBusConnection* connection,
                  const gchar* sender,
                  const gchar* object_path,
                  const gchar* interface_name,
                  const gchar* method_name,
                  GVariant* parameters,
                  GDBusMethodInvocation* invocation,
                  gpointer access);
GVariant* OnAccessProperty(GDBusConnection* connection,
                           const gchar* sender,
                           const gchar* object_path,
                           const gchar* interface_name,
                           const gchar* property_name,
                           GError** error,
                           gpointer access);

#endif  // NICKEL_PORTAL_ACCESS_H_
