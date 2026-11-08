// Distributed under the terms of the GNU General Public License v2
//
// Notifications for xdg-desktop-portal-nickel (notifications.cc).

#ifndef NICKEL_PORTAL_NOTIFICATIONS_H_
#define NICKEL_PORTAL_NOTIFICATIONS_H_

#include <gio/gio.h>

struct Notifications;

// Owns org.freedesktop.Notifications on |session| and serves it, and keeps
// the notifications of the Notification portal; both become ChromeOS
// notifications through Chrome (|system|).
Notifications* NotificationsNew(GDBusConnection* session,
                                GDBusConnection* system);

// org.freedesktop.impl.portal.Notification, exported by the caller on the
// portal's object with |notifications| as user data.
void OnNotificationPortalCall(GDBusConnection* connection,
                              const gchar* sender,
                              const gchar* object_path,
                              const gchar* interface_name,
                              const gchar* method_name,
                              GVariant* parameters,
                              GDBusMethodInvocation* invocation,
                              gpointer notifications);
GVariant* OnNotificationPortalProperty(GDBusConnection* connection,
                                       const gchar* sender,
                                       const gchar* object_path,
                                       const gchar* interface_name,
                                       const gchar* property_name,
                                       GError** error,
                                       gpointer notifications);

#endif  // NICKEL_PORTAL_NOTIFICATIONS_H_
