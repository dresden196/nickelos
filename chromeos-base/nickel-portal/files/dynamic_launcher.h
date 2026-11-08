// Distributed under the terms of the GNU General Public License v2
//
// DynamicLauncher for xdg-desktop-portal-nickel (dynamic_launcher.cc).

#ifndef NICKEL_PORTAL_DYNAMIC_LAUNCHER_H_
#define NICKEL_PORTAL_DYNAMIC_LAUNCHER_H_

#include <gio/gio.h>

struct DynamicLauncher;

DynamicLauncher* DynamicLauncherNew(GDBusConnection* system);

// org.freedesktop.impl.portal.DynamicLauncher, exported by the caller on the
// portal's object with |launcher| as user data.
void OnDynamicLauncherCall(GDBusConnection* connection,
                           const gchar* sender,
                           const gchar* object_path,
                           const gchar* interface_name,
                           const gchar* method_name,
                           GVariant* parameters,
                           GDBusMethodInvocation* invocation,
                           gpointer launcher);
GVariant* OnDynamicLauncherProperty(GDBusConnection* connection,
                                    const gchar* sender,
                                    const gchar* object_path,
                                    const gchar* interface_name,
                                    const gchar* property_name,
                                    GError** error,
                                    gpointer launcher);

#endif  // NICKEL_PORTAL_DYNAMIC_LAUNCHER_H_
