// Distributed under the terms of the GNU General Public License v2
//
// Linux apps' tray icons on ChromeOS's shelf, for nickel-appd (tray.cc).

#ifndef NICKEL_APPD_TRAY_H_
#define NICKEL_APPD_TRAY_H_

#include <gio/gio.h>

#include <string>

struct Tray;

// Chrome's id for the app with this desktop file id (without ".desktop"),
// as nickel-appd registers it (nickel-appd.cc).
std::string NickelAppId(const std::string& desktop_file_id);

// Serves org.kde.StatusNotifierWatcher on |session| (the session bus, where
// Linux apps register their tray icons) and keeps Chrome (|system|) told
// about each app's: vm_applications' NickelTrayItem and
// NickelTrayItemRemoved.
Tray* TrayNew(GDBusConnection* session, GDBusConnection* system);

// What the user did on ChromeOS with the tray item |key| (nickel-appd's
// TrayEvent, called by Chrome): "activate" (a click on the app's shelf
// icon), "menu" (a click on its menu entry |item|) or "quit" (Close on the
// shelf), with an xdg-activation token for bringing a window forward.
void TrayEvent(Tray* tray,
               const std::string& key,
               const std::string& event,
               int item,
               const std::string& token);

#endif  // NICKEL_APPD_TRAY_H_
