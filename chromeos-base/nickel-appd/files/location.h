// Distributed under the terms of the GNU General Public License v2

#ifndef NICKEL_APPD_LOCATION_H_
#define NICKEL_APPD_LOCATION_H_

#include <gio/gio.h>

// Becomes the signed-in user's GeoClue agent (location.cc) whenever GeoClue
// runs, on the system bus |system|.
void LocationAgentStart(GDBusConnection* system);

#endif  // NICKEL_APPD_LOCATION_H_
