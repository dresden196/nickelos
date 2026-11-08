// Distributed under the terms of the GNU General Public License v2
//
// Account for xdg-desktop-portal-nickel (account.cc).

#ifndef NICKEL_PORTAL_ACCOUNT_H_
#define NICKEL_PORTAL_ACCOUNT_H_

#include <gio/gio.h>

struct Account;

Account* AccountNew(GDBusConnection* system);

// org.freedesktop.impl.portal.Account, exported by the caller on the
// portal's object with |account| as user data.
void OnAccountCall(GDBusConnection* connection,
                   const gchar* sender,
                   const gchar* object_path,
                   const gchar* interface_name,
                   const gchar* method_name,
                   GVariant* parameters,
                   GDBusMethodInvocation* invocation,
                   gpointer account);

#endif  // NICKEL_PORTAL_ACCOUNT_H_
