// Distributed under the terms of the GNU General Public License v2
//
// Background for xdg-desktop-portal-nickel (background.cc).

#ifndef NICKEL_PORTAL_BACKGROUND_H_
#define NICKEL_PORTAL_BACKGROUND_H_

#include <gio/gio.h>

struct Background;

Background* BackgroundNew();

// org.freedesktop.impl.portal.Background, exported by the caller on the
// portal's object with |background| as user data.
void OnBackgroundCall(GDBusConnection* connection,
                      const gchar* sender,
                      const gchar* object_path,
                      const gchar* interface_name,
                      const gchar* method_name,
                      GVariant* parameters,
                      GDBusMethodInvocation* invocation,
                      gpointer background);

#endif  // NICKEL_PORTAL_BACKGROUND_H_
