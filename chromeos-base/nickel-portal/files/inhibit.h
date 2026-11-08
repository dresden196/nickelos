// Distributed under the terms of the GNU General Public License v2
//
// Inhibit and org.freedesktop.ScreenSaver for xdg-desktop-portal-nickel
// (inhibit.cc).

#ifndef NICKEL_PORTAL_INHIBIT_H_
#define NICKEL_PORTAL_INHIBIT_H_

#include <gio/gio.h>

struct Inhibit;

// Also owns org.freedesktop.ScreenSaver on |session| and exports it.
Inhibit* InhibitNew(GDBusConnection* session, GDBusConnection* system);

// org.freedesktop.impl.portal.Inhibit, exported by the caller on the
// portal's object with |inhibit| as user data.
void OnInhibitCall(GDBusConnection* connection,
                   const gchar* sender,
                   const gchar* object_path,
                   const gchar* interface_name,
                   const gchar* method_name,
                   GVariant* parameters,
                   GDBusMethodInvocation* invocation,
                   gpointer inhibit);
GVariant* OnInhibitProperty(GDBusConnection* connection,
                            const gchar* sender,
                            const gchar* object_path,
                            const gchar* interface_name,
                            const gchar* property_name,
                            GError** error,
                            gpointer inhibit);

#endif  // NICKEL_PORTAL_INHIBIT_H_
