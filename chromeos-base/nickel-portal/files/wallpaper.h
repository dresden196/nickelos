// Distributed under the terms of the GNU General Public License v2
//
// Wallpaper for xdg-desktop-portal-nickel (wallpaper.cc).

#ifndef NICKEL_PORTAL_WALLPAPER_H_
#define NICKEL_PORTAL_WALLPAPER_H_

#include <gio/gio.h>

struct Wallpaper;

Wallpaper* WallpaperNew(GDBusConnection* system);

// org.freedesktop.impl.portal.Wallpaper, exported by the caller on the
// portal's object with |wallpaper| as user data.
void OnWallpaperCall(GDBusConnection* connection,
                     const gchar* sender,
                     const gchar* object_path,
                     const gchar* interface_name,
                     const gchar* method_name,
                     GVariant* parameters,
                     GDBusMethodInvocation* invocation,
                     gpointer wallpaper);
GVariant* OnWallpaperProperty(GDBusConnection* connection,
                              const gchar* sender,
                              const gchar* object_path,
                              const gchar* interface_name,
                              const gchar* property_name,
                              GError** error,
                              gpointer wallpaper);

#endif  // NICKEL_PORTAL_WALLPAPER_H_
