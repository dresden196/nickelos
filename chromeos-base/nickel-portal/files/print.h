// Distributed under the terms of the GNU General Public License v2
//
// Print for xdg-desktop-portal-nickel (print.cc).

#ifndef NICKEL_PORTAL_PRINT_H_
#define NICKEL_PORTAL_PRINT_H_

#include <gio/gio.h>

struct Print;

Print* PrintNew(GDBusConnection* system);

// org.freedesktop.impl.portal.Print, exported by the caller on the portal's
// object with |print| as user data.
void OnPrintCall(GDBusConnection* connection,
                 const gchar* sender,
                 const gchar* object_path,
                 const gchar* interface_name,
                 const gchar* method_name,
                 GVariant* parameters,
                 GDBusMethodInvocation* invocation,
                 gpointer print);
GVariant* OnPrintProperty(GDBusConnection* connection,
                          const gchar* sender,
                          const gchar* object_path,
                          const gchar* interface_name,
                          const gchar* property_name,
                          GError** error,
                          gpointer print);

#endif  // NICKEL_PORTAL_PRINT_H_
