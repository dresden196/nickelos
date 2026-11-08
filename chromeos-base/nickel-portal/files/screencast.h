// Distributed under the terms of the GNU General Public License v2
//
// ScreenCast for xdg-desktop-portal-nickel (screencast.cc).

#ifndef NICKEL_PORTAL_SCREENCAST_H_
#define NICKEL_PORTAL_SCREENCAST_H_

#include <gio/gio.h>

struct ScreenCast;

// |session_info| describes org.freedesktop.impl.portal.Session, which each
// session exports.
ScreenCast* ScreenCastNew(GDBusConnection* session,
                          GDBusConnection* system,
                          GDBusInterfaceInfo* session_info);

// org.freedesktop.impl.portal.ScreenCast's methods and properties.
void ScreenCastMethodCall(ScreenCast* cast,
                          const gchar* method_name,
                          GVariant* parameters,
                          GDBusMethodInvocation* invocation);
GVariant* ScreenCastGetProperty(ScreenCast* cast, const gchar* property_name);

// org.freedesktop.impl.portal.RemoteDesktop's, on the same sessions.
void RemoteDesktopMethodCall(ScreenCast* cast,
                             const gchar* method_name,
                             GVariant* parameters,
                             GDBusMethodInvocation* invocation);
GVariant* RemoteDesktopGetProperty(ScreenCast* cast,
                                   const gchar* property_name);

// org.freedesktop.impl.portal.Clipboard, for RemoteDesktop sessions.
void ClipboardMethodCall(ScreenCast* cast,
                         const gchar* method_name,
                         GVariant* parameters,
                         GDBusMethodInvocation* invocation);
GVariant* ClipboardGetProperty(ScreenCast* cast, const gchar* property_name);

#endif  // NICKEL_PORTAL_SCREENCAST_H_
