// Distributed under the terms of the GNU General Public License v2

#ifndef NICKEL_APPD_AUTOSTART_H_
#define NICKEL_APPD_AUTOSTART_H_

#include <gio/gio.h>

// Starts the session's autostart entries (autostart.cc) once the display is
// up, once per sign-in: |session| is the session bus, whose GUID marks the
// sign-in they were started in (nickel-appd may be restarted).
void AutostartRun(GDBusConnection* session);

#endif  // NICKEL_APPD_AUTOSTART_H_
