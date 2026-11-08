title: Steam and games
order: 70
summary: Gaming on NickelOS. Steam from Flathub, Proton, installed games in the launcher and on the shelf, controllers and Steam Input, mouse look, push-to-talk, Wine, and where to keep a game library.
---
# Steam and games

Steam, Proton and Windows games run on NickelOS the way they do on other Linux distributions, on your GPU, with your controllers. This page covers what NickelOS adds on top.

## Steam

Install **Steam** from Bazaar. It's Valve's own Flatpak (`com.valvesoftware.Steam`). When it installs, Flatpak also pulls in Steam's 32-bit runtime and, on NVIDIA, the GL libraries that match your driver, 32-bit included. You don't have to set any of that up yourself.

Windows games run through **Proton**, as on any Linux PC. Turn it on for every game in Steam's settings, under Compatibility, or per game in its properties.

Your games and their Proton prefixes live in Steam's Flatpak folder, `~/.var/app/com.valvesoftware.Steam/`, inside your home.

## Your games in the launcher

Every game Steam has installed shows up in the launcher with its own icon, like an app. Click it and Steam starts the game. While the game runs, its window is on the shelf with the game's name and icon.

This is nickel-appd's doing. Steam's Flatpak writes its own game shortcuts inside its sandbox, where nothing else looks. So nickel-appd reads Steam's libraries itself (`libraryfolders.vdf`, and each game's `appmanifest`), and writes a desktop file for each installed game:

- `~/.local/share/applications/steam_app_<id>.desktop`
- the game's name, and Steam's own icon for it
- `flatpak run com.valvesoftware.Steam steam://rungameid/<id>` to start it
- `StartupWMClass=steam_app_<id>`, the window class Proton gives every game, so the running game's window lands on the right shelf icon

Proton, the Steam Linux Runtimes and Steamworks redistributables aren't games, so they're left out. nickel-appd follows Steam's libraries while you're signed in, so new games appear as they finish installing and leave when they're uninstalled.

**Uninstall** on a game in the launcher opens Steam's uninstall dialog. App info (Settings > Apps > Manage your apps) shows the game's size and the size of its Proton prefix.

Steam installed from Gentoo gets the same treatment (see [Gentoo and Portage](/help/gentoo/#steam-from-gentoo)).

## Controllers

- Valve's udev rules for controllers (`steam-devices`) come with NickelOS, so Steam and SDL games see controllers the way they do on other distributions.
- The signed-in user gets `/dev/uinput` and the controllers' `hidraw` devices through elogind's seat, which Steam Input needs to present your controller to games.

## Mouse look

X11 games turn the camera by warping the pointer. Xwayland can only do that while the game holds a Wayland pointer lock, and ChromeOS normally only lets Android apps lock the pointer. NickelOS lets Linux apps lock it too, so mouse look works the way it should. Raw mouse motion comes through `zwp_relative_pointer_manager_v1`.

## The screen stays on

Games, Steam and video players ask the desktop to keep the screen on (through the Inhibit portal or `org.freedesktop.ScreenSaver`, which SDL, Steam and mpv use). NickelOS turns that into a ChromeOS wake lock, the same thing a playing video tab holds. When the game quits or crashes, the lock goes with it.

## Push-to-talk and other shortcut keys

Voice chat apps that want a key while you're in a game:

- **Apps that use the GlobalShortcuts portal** ask once. ChromeOS shows "Allow keyboard shortcuts?" with the keys, which you can change. Shortcuts already taken by ChromeOS or another app stay theirs. The shortcut reaches the app even while another window has the focus, and never while the screen is locked.
- **X11 apps that watch the keyboard themselves**, like the official Discord client, are covered by **Settings > Apps > Linux apps > Shortcut keys for older Linux apps**, on by default. See [Typing and keyboard](/help/input/#shortcut-keys-for-older-apps) for exactly which keys they see.

## How games draw

Games run on X11 through Xwayland, or natively on Wayland, both on the GPU (see [Graphics](/help/graphics/)):

- OpenGL and Vulkan through Mesa on Intel and AMD, or NVIDIA's own driver on NVIDIA.
- Direct3D through DXVK and VKD3D inside Proton, which turns it into Vulkan.
- On NVIDIA, X11 games get explicit sync, so frames arrive whole without a copy.
- Exo shows whole frames at your display's refresh rate.

## Wine

- **Flathub's Wine** works like any other Flatpak.
- **Gentoo's Wine** (`sudo emerge app-emulation/wine-vanilla`) runs 32-bit Windows programs through WoW64.
- Windows programs' tray icons (old-style X11 XEmbed icons) show up on the shelf like other tray icons.

## Where to keep your games

Your home, on NickelOS's data partition, is the best place, and it's where Steam puts them by default. If you add a second Steam library on another drive, format that drive ext4 or btrfs. Proton games on an NTFS drive run into problems with permissions and with file names that differ only in case.
