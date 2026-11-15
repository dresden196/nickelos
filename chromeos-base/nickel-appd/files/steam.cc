// Distributed under the terms of the GNU General Public License v2
//
// Steam's installed games as apps, for nickel-appd.
//
// On other Linux systems Steam puts a desktop file for each game it
// installs in ~/.local/share/applications (Exec=steam
// steam://rungameid/<id>, Icon=steam_icon_<id>), so games are in the app
// menu. Steam's Flatpak, the one NickelOS users get from Bazaar, writes
// them inside its sandbox (~/.var/app/com.valvesoftware.Steam/.local/
// share/applications), which no one outside reads; so a game was in
// neither ChromeOS's launcher nor, while it ran, on the shelf by name: its
// windows (Proton's: WM_CLASS steam_app_<id>) matched no app, and the
// shelf showed the generic icon with no name.
//
// So nickel-appd writes steam_app_<id>.desktop in ~/.local/share/
// applications for each game a Steam (the Flatpak's or one from Portage)
// has installed: the game's name, Steam's icon for it, the command that
// asks that Steam to start it, and StartupWMClass=steam_app_<id> for the
// shelf. X-NickelOS-Steam=<id> marks the files it wrote: they go when the
// game does. A game whose desktop file Steam wrote there itself (Steam from
// Portage) gets none; the app list gives that one its window class.
//
// And a shortcut on the desktop for each game installed from then on, as
// Steam makes one elsewhere (it asks xdg-user-dir where the desktop is, and
// writes <name>.desktop there too; the Flatpak's sandbox can't): the same
// launcher, named after the game, executable as Steam's are (the desktop
// starts only those). Each game gets one once: a shortcut deleted stays
// gone, and the games already installed when nickel-appd first did this
// got none (~/.local/share/nickel-appd/steam-desktop lists the games
// handled). It goes with the game, and a game installed again gets a new
// one. A Steam that wrote its own shortcut there gets none from us.
//
// What is a game: an app Steam has fully installed (appmanifest_<id>.acf,
// StateFlags with 4), in any of its libraries (libraryfolders.vdf), that
// is not one of Steam's own tools (Proton, the Steam Linux Runtimes,
// Steamworks Common Redistributables), which Steam installs the same way.

#include "steam.h"

#include <gio/gdesktopappinfo.h>
#include <gio/gio.h>
#include <glib/gstdio.h>
#include <syslog.h>

#include <cstring>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr char kDesktopGroup[] = "Desktop Entry";
constexpr char kOurKey[] = "X-NickelOS-Steam";
constexpr char kRunGame[] = "steam://rungameid/";
constexpr char kFlatpakSteam[] = "com.valvesoftware.Steam";
// Steamworks Common Redistributables.
constexpr char kRedistributables[] = "228980";
constexpr guint kSettleSeconds = 3;

// A Steam: where it keeps its data (its icons are below it, Steam itself
// in Steam/), and whether it is the Flatpak.
struct Install {
  std::string data_dir;
  bool flatpak;
};

struct Game {
  std::string name;
  const Install* install;
  std::string library;  // The library folder it is in.
};

// Steam's text KeyValues (VDF): "key" "value" and "key" { ... }. Each value
// with its keys from the top joined by '/', e.g. "AppState/name".
std::vector<std::pair<std::string, std::string>> ParseVdf(
    const std::string& text) {
  std::vector<std::pair<std::string, std::string>> values;
  std::vector<std::string> path;
  std::string key;
  bool have_key = false;
  size_t i = 0;
  while (i < text.size()) {
    const char c = text[i];
    if (c == '"') {
      std::string token;
      ++i;
      while (i < text.size() && text[i] != '"') {
        if (text[i] == '\\' && i + 1 < text.size()) {
          ++i;
        }
        token += text[i++];
      }
      ++i;
      if (!have_key) {
        key = token;
        have_key = true;
      } else {
        std::string full;
        for (const auto& p : path) {
          full += p + "/";
        }
        values.emplace_back(full + key, token);
        have_key = false;
      }
    } else if (c == '{') {
      path.push_back(have_key ? key : "");
      have_key = false;
      ++i;
    } else if (c == '}') {
      if (!path.empty()) {
        path.pop_back();
      }
      have_key = false;
      ++i;
    } else if (c == '/' && i + 1 < text.size() && text[i + 1] == '/') {
      while (i < text.size() && text[i] != '\n') {
        ++i;
      }
    } else {
      ++i;
    }
  }
  return values;
}

std::string ReadText(const std::string& path) {
  gchar* contents = nullptr;
  if (!g_file_get_contents(path.c_str(), &contents, nullptr, nullptr)) {
    return "";
  }
  std::string text = contents;
  g_free(contents);
  return text;
}

std::vector<Install> Installs() {
  const std::string home = g_get_home_dir();
  return {{home + "/.var/app/" + kFlatpakSteam + "/.local/share", true},
          {g_get_user_data_dir(), false}};
}

// The library folders of the Steam whose data is in |install|; none if that
// Steam is not there.
std::vector<std::string> Libraries(const Install& install) {
  std::vector<std::string> libraries;
  const std::string steam = install.data_dir + "/Steam";
  if (!g_file_test((steam + "/steamapps").c_str(), G_FILE_TEST_IS_DIR)) {
    return libraries;
  }
  libraries.push_back(steam);
  for (const auto& [key, value] :
       ParseVdf(ReadText(steam + "/steamapps/libraryfolders.vdf"))) {
    if (g_str_has_suffix(key.c_str(), "/path") && !value.empty()) {
      bool known = false;
      for (const auto& l : libraries) {
        gchar* a = g_canonicalize_filename(l.c_str(), nullptr);
        gchar* b = g_canonicalize_filename(value.c_str(), nullptr);
        known = known || g_strcmp0(a, b) == 0;
        g_free(a);
        g_free(b);
      }
      if (!known) {
        libraries.push_back(value);
      }
    }
  }
  return libraries;
}

bool IsTool(const std::string& id, const std::string& name) {
  return id == kRedistributables || g_str_has_prefix(name.c_str(), "Proton") ||
         g_str_has_prefix(name.c_str(), "Steam Linux Runtime") ||
         g_str_has_prefix(name.c_str(), "Steamworks Common");
}

// Every installed game, by id; the first Steam that has it wins.
std::map<std::string, Game> FindGames(const std::vector<Install>& installs,
                                      std::set<std::string>* watched) {
  std::map<std::string, Game> games;
  for (const Install& install : installs) {
    for (const std::string& library : Libraries(install)) {
      const std::string apps = library + "/steamapps";
      watched->insert(apps);
      GDir* dir = g_dir_open(apps.c_str(), 0, nullptr);
      const gchar* entry = nullptr;
      while (dir && (entry = g_dir_read_name(dir))) {
        if (!g_str_has_prefix(entry, "appmanifest_") ||
            !g_str_has_suffix(entry, ".acf")) {
          continue;
        }
        std::string id, name;
        guint64 flags = 0;
        for (const auto& [key, value] : ParseVdf(ReadText(apps + "/" + entry))) {
          if (key == "AppState/appid") {
            id = value;
          } else if (key == "AppState/name") {
            name = value;
          } else if (key == "AppState/StateFlags") {
            flags = g_ascii_strtoull(value.c_str(), nullptr, 10);
          }
        }
        if (id.empty() || name.empty() || !(flags & 4) || IsTool(id, name) ||
            games.count(id)) {
          continue;
        }
        games[id] = Game{name, &install, library};
      }
      if (dir) {
        g_dir_close(dir);
      }
    }
  }
  return games;
}

// Steam's own icon for the game (it writes them for its desktop files), the
// largest; else Steam's icon.
std::string GameIcon(const Install& install, const std::string& id) {
  static const char* const kSizes[] = {"256x256", "128x128", "96x96",
                                       "64x64",   "48x48",   "32x32"};
  for (const char* size : kSizes) {
    std::string path = install.data_dir + "/icons/hicolor/" + size +
                       "/apps/steam_icon_" + id + ".png";
    if (g_file_test(path.c_str(), G_FILE_TEST_IS_REGULAR)) {
      return path;
    }
  }
  return install.flatpak ? kFlatpakSteam : "steam";
}

// The command line that asks |install|'s Steam to open |url|.
std::string SteamCommand(const Install& install, const std::string& url) {
  if (install.flatpak) {
    return std::string("flatpak run ") + kFlatpakSteam + " " + url;
  }
  // Steam's own desktop file says how to start it with a URL (%U).
  std::string command = "steam %U";
  if (GDesktopAppInfo* steam = g_desktop_app_info_new("steam.desktop")) {
    if (gchar* exec = g_desktop_app_info_get_string(steam, "Exec")) {
      command = exec;
      g_free(exec);
    }
    g_object_unref(steam);
  }
  for (const char* code : {"%U", "%u", "%F", "%f"}) {
    size_t at = command.find(code);
    if (at != std::string::npos) {
      return command.replace(at, 2, url);
    }
  }
  return command + " " + url;
}

std::string DesktopFileFor(const Game& game, const std::string& id) {
  GKeyFile* kf = g_key_file_new();
  g_key_file_set_string(kf, kDesktopGroup, "Type", "Application");
  g_key_file_set_string(kf, kDesktopGroup, "Name", game.name.c_str());
  g_key_file_set_string(kf, kDesktopGroup, "Comment", "Play this game on Steam");
  g_key_file_set_string(kf, kDesktopGroup, "Exec",
                        SteamCommand(*game.install, kRunGame + id).c_str());
  g_key_file_set_string(kf, kDesktopGroup, "Icon",
                        GameIcon(*game.install, id).c_str());
  g_key_file_set_string(kf, kDesktopGroup, "Categories", "Game;");
  g_key_file_set_string(kf, kDesktopGroup, "StartupWMClass",
                        SteamWindowClass(id).c_str());
  g_key_file_set_boolean(kf, kDesktopGroup, "Terminal", FALSE);
  g_key_file_set_string(kf, kDesktopGroup, kOurKey, id.c_str());
  gchar* data = g_key_file_to_data(kf, nullptr, nullptr);
  std::string text = data;
  g_free(data);
  g_key_file_free(kf);
  return text;
}

// The desktop folder (XDG_DESKTOP_DIR from ~/.config/user-dirs.dirs, as
// xdg-user-dir answers), or "" if the user has none.
std::string DesktopDir() {
  const char* dir = g_get_user_special_dir(G_USER_DIRECTORY_DESKTOP);
  if (!dir || g_strcmp0(dir, g_get_home_dir()) == 0) {
    return "";
  }
  return dir;
}

// The games whose desktop shortcut was handled; whether the list existed.
bool ReadHandled(const std::string& path, std::set<std::string>* handled) {
  gchar* contents = nullptr;
  if (!g_file_get_contents(path.c_str(), &contents, nullptr, nullptr)) {
    return false;
  }
  gchar** lines = g_strsplit(contents, "\n", -1);
  for (gchar** l = lines; *l; ++l) {
    if (**l) {
      handled->insert(*l);
    }
  }
  g_strfreev(lines);
  g_free(contents);
  return true;
}

void WriteHandled(const std::string& path,
                  const std::set<std::string>& handled) {
  std::string text;
  for (const std::string& id : handled) {
    text += id + "\n";
  }
  gchar* dir = g_path_get_dirname(path.c_str());
  g_mkdir_with_parents(dir, 0700);
  g_free(dir);
  if (ReadText(path) != text) {
    g_file_set_contents(path.c_str(), text.data(), text.size(), nullptr);
  }
}

// A file name for the game's shortcut: its name, without what a file name
// can't hold.
std::string ShortcutName(const std::string& name) {
  std::string file;
  for (char c : name) {
    file += (c == '/' || c == '\\') ? '-' : c;
  }
  while (!file.empty() && (file[0] == '.' || file[0] == ' ')) {
    file.erase(0, 1);
  }
  return file.empty() ? "" : file + ".desktop";
}

// The desktop shortcuts: ours by game (and where), and the games a Steam
// put one there for itself.
void ScanDesktop(const std::string& dir,
                 std::map<std::string, std::string>* ours,
                 std::set<std::string>* steams_own) {
  GDir* d = g_dir_open(dir.c_str(), 0, nullptr);
  const gchar* entry = nullptr;
  while (d && (entry = g_dir_read_name(d))) {
    if (!g_str_has_suffix(entry, ".desktop")) {
      continue;
    }
    const std::string path = dir + "/" + entry;
    GKeyFile* kf = g_key_file_new();
    if (g_key_file_load_from_file(kf, path.c_str(), G_KEY_FILE_NONE,
                                  nullptr)) {
      if (gchar* id = g_key_file_get_string(kf, kDesktopGroup, kOurKey,
                                            nullptr)) {
        (*ours)[id] = path;
        g_free(id);
      } else if (gchar* exec = g_key_file_get_string(kf, kDesktopGroup, "Exec",
                                                     nullptr)) {
        const std::string game = SteamGameOfExec(exec);
        if (!game.empty()) {
          steams_own->insert(game);
        }
        g_free(exec);
      }
    }
    g_key_file_free(kf);
  }
  if (d) {
    g_dir_close(d);
  }
}

// The desktop's shortcuts for the games: a new game gets one, a game that
// is gone loses ours (see the top of this file).
void SyncDesktop(const std::map<std::string, Game>& games) {
  const std::string dir = DesktopDir();
  if (dir.empty()) {
    return;
  }
  const std::string list =
      std::string(g_get_user_data_dir()) + "/nickel-appd/steam-desktop";
  std::set<std::string> handled;
  const bool first = !ReadHandled(list, &handled);
  std::map<std::string, std::string> ours;
  std::set<std::string> steams_own;
  ScanDesktop(dir, &ours, &steams_own);

  for (auto it = handled.begin(); it != handled.end();) {
    if (games.count(*it)) {
      ++it;
      continue;
    }
    // Uninstalled: its shortcut goes, and installing it again makes one.
    if (auto own = ours.find(*it); own != ours.end()) {
      g_unlink(own->second.c_str());
      syslog(LOG_INFO, "steam: removed %s", own->second.c_str());
    }
    it = handled.erase(it);
  }
  for (const auto& [id, game] : games) {
    if (!handled.insert(id).second || first || steams_own.count(id) ||
        ours.count(id)) {
      continue;
    }
    const std::string name = ShortcutName(game.name);
    if (name.empty()) {
      continue;
    }
    const std::string path = dir + "/" + name;
    if (g_file_test(path.c_str(), G_FILE_TEST_EXISTS)) {
      continue;
    }
    const std::string text = DesktopFileFor(game, id);
    g_mkdir_with_parents(dir.c_str(), 0755);
    GError* error = nullptr;
    if (!g_file_set_contents(path.c_str(), text.data(), text.size(), &error)) {
      syslog(LOG_WARNING, "steam: cannot write %s: %s", path.c_str(),
             error->message);
      g_error_free(error);
      continue;
    }
    // Executable, as Steam's own: the desktop starts only such launchers.
    g_chmod(path.c_str(), 0755);
    syslog(LOG_INFO, "steam: %s on the desktop (%s)", game.name.c_str(),
           path.c_str());
  }
  WriteHandled(list, handled);
}

}  // namespace

struct SteamGames {
  std::map<std::string, GFileMonitor*> monitors;
  guint settle = 0;
};

namespace {

void Sync(SteamGames* steam);

gboolean OnSettled(gpointer data) {
  SteamGames* steam = static_cast<SteamGames*>(data);
  steam->settle = 0;
  Sync(steam);
  return G_SOURCE_REMOVE;
}

void OnLibraryChanged(GFileMonitor*,
                      GFile* file,
                      GFile*,
                      GFileMonitorEvent,
                      gpointer data) {
  SteamGames* steam = static_cast<SteamGames*>(data);
  gchar* name = g_file_get_basename(file);
  const bool manifest = name && (g_str_has_prefix(name, "appmanifest_") ||
                                 g_str_equal(name, "libraryfolders.vdf"));
  g_free(name);
  if (!manifest) {
    return;
  }
  // Steam rewrites a manifest many times while it downloads.
  if (steam->settle) {
    g_source_remove(steam->settle);
  }
  steam->settle = g_timeout_add_seconds(kSettleSeconds, OnSettled, steam);
}

void Watch(SteamGames* steam, const std::set<std::string>& dirs) {
  for (auto it = steam->monitors.begin(); it != steam->monitors.end();) {
    if (dirs.count(it->first)) {
      ++it;
      continue;
    }
    g_file_monitor_cancel(it->second);
    g_object_unref(it->second);
    it = steam->monitors.erase(it);
  }
  for (const std::string& dir : dirs) {
    if (steam->monitors.count(dir)) {
      continue;
    }
    GFile* file = g_file_new_for_path(dir.c_str());
    GFileMonitor* monitor =
        g_file_monitor_directory(file, G_FILE_MONITOR_NONE, nullptr, nullptr);
    g_object_unref(file);
    if (monitor) {
      g_signal_connect(monitor, "changed", G_CALLBACK(OnLibraryChanged), steam);
      steam->monitors[dir] = monitor;
    }
  }
}

void Sync(SteamGames* steam) {
  static const std::vector<Install> installs = Installs();
  std::set<std::string> watched;
  std::map<std::string, Game> games = FindGames(installs, &watched);
  Watch(steam, watched);

  const std::string dir = std::string(g_get_user_data_dir()) + "/applications";
  // What is in the applications folder: ours by game, and the games Steam
  // wrote a desktop file for itself.
  std::map<std::string, std::string> ours;
  std::set<std::string> steams_own;
  GDir* d = g_dir_open(dir.c_str(), 0, nullptr);
  const gchar* entry = nullptr;
  while (d && (entry = g_dir_read_name(d))) {
    if (!g_str_has_suffix(entry, ".desktop")) {
      continue;
    }
    std::string path = dir + "/" + entry;
    GKeyFile* kf = g_key_file_new();
    if (g_key_file_load_from_file(kf, path.c_str(), G_KEY_FILE_NONE,
                                  nullptr)) {
      if (gchar* id = g_key_file_get_string(kf, kDesktopGroup, kOurKey,
                                            nullptr)) {
        ours[id] = path;
        g_free(id);
      } else if (gchar* exec = g_key_file_get_string(kf, kDesktopGroup, "Exec",
                                                     nullptr)) {
        std::string game = SteamGameOfExec(exec);
        if (!game.empty()) {
          steams_own.insert(game);
        }
        g_free(exec);
      }
    }
    g_key_file_free(kf);
  }
  if (d) {
    g_dir_close(d);
  }

  for (const auto& [id, path] : ours) {
    if (!games.count(id) || steams_own.count(id)) {
      g_unlink(path.c_str());
      syslog(LOG_INFO, "steam: removed %s", path.c_str());
    }
  }
  if (!games.empty()) {
    g_mkdir_with_parents(dir.c_str(), 0700);
  }
  for (const auto& [id, game] : games) {
    if (steams_own.count(id)) {
      continue;
    }
    const std::string path = dir + "/steam_app_" + id + ".desktop";
    const std::string text = DesktopFileFor(game, id);
    if (ReadText(path) == text) {
      continue;
    }
    GError* error = nullptr;
    if (!g_file_set_contents(path.c_str(), text.data(), text.size(), &error)) {
      syslog(LOG_WARNING, "steam: cannot write %s: %s", path.c_str(),
             error->message);
      g_error_free(error);
    } else {
      syslog(LOG_INFO, "steam: %s (%s)", game.name.c_str(), path.c_str());
    }
  }
  SyncDesktop(games);
}

}  // namespace

SteamGames* SteamGamesNew() {
  SteamGames* steam = new SteamGames;
  Sync(steam);
  return steam;
}

void SteamGamesSync(SteamGames* games) {
  Sync(games);
}

std::string SteamGameOfExec(const char* exec) {
  const char* at = exec ? strstr(exec, kRunGame) : nullptr;
  if (!at) {
    return "";
  }
  std::string id;
  for (const char* c = at + strlen(kRunGame); g_ascii_isdigit(*c); ++c) {
    id += *c;
  }
  return id;
}

std::string SteamWindowClass(const std::string& game) {
  return "steam_app_" + game;
}

void SteamGameSizes(const std::string& game,
                    guint64* app_size,
                    guint64* data_size) {
  *app_size = 0;
  *data_size = 0;
  for (const Install& install : Installs()) {
    for (const std::string& library : Libraries(install)) {
      const std::string apps = library + "/steamapps";
      const std::string manifest = apps + "/appmanifest_" + game + ".acf";
      if (!g_file_test(manifest.c_str(), G_FILE_TEST_IS_REGULAR)) {
        continue;
      }
      for (const auto& [key, value] : ParseVdf(ReadText(manifest))) {
        if (key == "AppState/SizeOnDisk") {
          *app_size = g_ascii_strtoull(value.c_str(), nullptr, 10);
        }
      }
      // Proton's Windows prefix for the game: its saves and settings.
      const std::string prefix = apps + "/compatdata/" + game;
      if (g_file_test(prefix.c_str(), G_FILE_TEST_IS_DIR)) {
        gchar* out = nullptr;
        const gchar* argv[] = {"du", "-sb", prefix.c_str(), nullptr};
        if (g_spawn_sync(nullptr, const_cast<gchar**>(argv), nullptr,
                         static_cast<GSpawnFlags>(G_SPAWN_SEARCH_PATH |
                                                  G_SPAWN_STDERR_TO_DEV_NULL),
                         nullptr, nullptr, &out, nullptr, nullptr, nullptr)) {
          *data_size = g_ascii_strtoull(out ? out : "", nullptr, 10);
        }
        g_free(out);
      }
      return;
    }
  }
}
