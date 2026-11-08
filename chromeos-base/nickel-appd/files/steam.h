// Distributed under the terms of the GNU General Public License v2

#ifndef NICKEL_APPD_STEAM_H_
#define NICKEL_APPD_STEAM_H_

#include <glib.h>

#include <string>

// Steam's installed games in the launcher and on the shelf (steam.cc).
struct SteamGames;

// Writes the games' desktop files and watches Steam's libraries from then
// on; the desktop files' changes reach the app list like any app's.
SteamGames* SteamGamesNew();
// Again, now: when the installed apps change (Steam itself installed or
// removed).
void SteamGamesSync(SteamGames* games);

// The Steam game an app's command line runs (steam://rungameid/<id>), "" if
// none.
std::string SteamGameOfExec(const char* exec);
// The window class Proton gives the game's windows: steam_app_<id>.
std::string SteamWindowClass(const std::string& game);

// What GetAppDetails tells about game |game|: its size on disk and the
// size of its Proton prefix (compatdata), 0 if not found.
void SteamGameSizes(const std::string& game, guint64* app_size,
                    guint64* data_size);

#endif  // NICKEL_APPD_STEAM_H_
