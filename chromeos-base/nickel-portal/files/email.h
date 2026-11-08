// Distributed under the terms of the GNU General Public License v2
//
// Email for xdg-desktop-portal-nickel (email.cc).

#ifndef NICKEL_PORTAL_EMAIL_H_
#define NICKEL_PORTAL_EMAIL_H_

#include <gio/gio.h>

struct Email;

Email* EmailNew();

// org.freedesktop.impl.portal.Email, exported by the caller on the portal's
// object with |email| as user data.
void OnEmailCall(GDBusConnection* connection,
                 const gchar* sender,
                 const gchar* object_path,
                 const gchar* interface_name,
                 const gchar* method_name,
                 GVariant* parameters,
                 GDBusMethodInvocation* invocation,
                 gpointer email);

#endif  // NICKEL_PORTAL_EMAIL_H_
