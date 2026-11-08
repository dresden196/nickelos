// Distributed under the terms of the GNU General Public License v2

#ifndef NICKEL_PORTAL_SCREENSHOT_H_
#define NICKEL_PORTAL_SCREENSHOT_H_

#include <gio/gio.h>

struct Screenshot;

Screenshot* ScreenshotNew(GDBusConnection* system);

void OnScreenshotCall(GDBusConnection* connection,
                      const gchar* sender,
                      const gchar* object_path,
                      const gchar* interface_name,
                      const gchar* method_name,
                      GVariant* parameters,
                      GDBusMethodInvocation* invocation,
                      gpointer screenshot);
GVariant* OnScreenshotProperty(GDBusConnection* connection,
                               const gchar* sender,
                               const gchar* object_path,
                               const gchar* interface_name,
                               const gchar* property_name,
                               GError** error,
                               gpointer screenshot);

#endif  // NICKEL_PORTAL_SCREENSHOT_H_
