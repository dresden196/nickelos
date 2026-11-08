// Distributed under the terms of the GNU General Public License v2
//
// GlobalShortcuts for xdg-desktop-portal-nickel (globalshortcuts.cc).

#ifndef NICKEL_PORTAL_GLOBALSHORTCUTS_H_
#define NICKEL_PORTAL_GLOBALSHORTCUTS_H_

#include <gio/gio.h>

struct GlobalShortcuts;

// |session_info| describes org.freedesktop.impl.portal.Session, which each
// session exports.
GlobalShortcuts* GlobalShortcutsNew(GDBusConnection* session,
                                    GDBusConnection* system,
                                    GDBusInterfaceInfo* session_info);

// org.freedesktop.impl.portal.GlobalShortcuts, exported by the caller on the
// portal's object with |shortcuts| as user data.
void OnGlobalShortcutsCall(GDBusConnection* connection,
                           const gchar* sender,
                           const gchar* object_path,
                           const gchar* interface_name,
                           const gchar* method_name,
                           GVariant* parameters,
                           GDBusMethodInvocation* invocation,
                           gpointer shortcuts);
GVariant* OnGlobalShortcutsProperty(GDBusConnection* connection,
                                    const gchar* sender,
                                    const gchar* object_path,
                                    const gchar* interface_name,
                                    const gchar* property_name,
                                    GError** error,
                                    gpointer shortcuts);

#endif  // NICKEL_PORTAL_GLOBALSHORTCUTS_H_
