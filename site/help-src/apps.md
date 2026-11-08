title: Apps
order: 20
summary: Getting apps on NickelOS: Bazaar and Flathub, the apps installed at first boot, AppImages, Gentoo packages, uninstalling, default apps, and the dialogs apps open when they ask for the camera, your location or your screen.
---
# Apps

On NickelOS you install a Linux app the same way you would on any other Linux distribution. You don't need to repackage it or turn on a "Linux mode" first. However it got there, it shows up in the launcher, can be pinned to the shelf, and opens in a normal ChromeOS window.

## Where apps come from

| Source | How | Lands in |
|---|---|---|
| Flathub | Bazaar, or `flatpak install` in the Terminal | `/var/lib/flatpak` (for everyone) or `~/.local/share/flatpak` (just you) |
| Gentoo | `sudo emerge` in the Terminal (see [Gentoo and Portage](/help/gentoo/)) | `/usr`, like on any Gentoo (really on the data partition) |
| AppImage | Download it and run it, or let Gear Lever manage it | Wherever you keep it |
| Android | Set up Android in Settings, then open an APK (see [Android apps](/help/android/)) | Your own Android, in `~/.local/share/waydroid` |
| Built in | Comes with NickelOS | The system image |

Snap isn't supported. snapd needs systemd, and NickelOS runs ChromeOS's init, upstart, with OpenRC for the services you add. Most apps on the Snap Store are on Flathub too.

The built-in apps are ChromeOS's own, all open source: the Chrome browser (Chromium), Files, Settings, the Terminal, the Camera app, and screen capture (Ctrl + Show windows, or Quick Settings > Screen capture).

## Bazaar and Flatpak

Bazaar is pinned to the shelf and is where most people get their apps. It's a store for Flathub, the main place Linux apps are published as Flatpaks. Flatpak apps carry their own libraries (a "runtime"), so they work the same on every distribution, NickelOS included.

Bazaar installs apps for everyone on the device. Administrators don't get asked for a password in their own session, and standard users are asked for an administrator's. Flathub is already set up, so the `flatpak` command works in the Terminal straight away:

```
flatpak search gimp
flatpak install flathub org.gimp.GIMP
flatpak update
flatpak list --app
flatpak install --user flathub org.gimp.GIMP   # just for you, no password
```

On ChromeOS, `/var` is small and doesn't allow programs to run from it. So on NickelOS `/var/lib/flatpak` is really `/mnt/stateful_partition/nickel/flatpak`, on the data partition with the rest of your stuff. Big apps and games have room there, and system updates don't touch it.

### The apps you start with

On first boot, as soon as there's a network, NickelOS installs these from Flathub. A notification tells you while it's happening, and when it's done.

| App | For |
|---|---|
| Bazaar | Getting more apps |
| Loupe | Pictures |
| Showtime | Videos |
| Papers | PDFs and other documents |
| Amberol | Music |

Files opens pictures, videos, music and PDFs with them until you choose other apps (below). If you remove one of them, it stays removed. If there's no network within half an hour, NickelOS tries again at the next start. What happened is logged in `/var/log/nickel-flatpak-defaults.log`.

## AppImages

AppImages work as they do anywhere: make the file executable and run it.

```
chmod +x ~/Downloads/Some-App.AppImage
~/Downloads/Some-App.AppImage
```

Your home folder lets programs run from it (ChromeOS normally blocks that, and NickelOS changes it). FUSE is there for AppImages that mount themselves. AppImages use the libraries a Linux desktop has (GTK, X11, sound), and on NickelOS those are Gentoo's, in the system image. **Gear Lever** (on Flathub) puts AppImages in the launcher for you and keeps them in one folder.

## How apps get into the launcher

Every app in the launcher comes from a `.desktop` file, the standard Linux app entry. A small service called **nickel-appd** runs in your session. It reads the desktop files in every data directory your session knows about and hands the list to Chrome:

- `~/.local/share/applications` (your own, Steam's games, AppImages added by Gear Lever)
- `~/.local/share/flatpak/exports/share` and `/var/lib/flatpak/exports/share` (Flatpak apps)
- `/usr/share` (the system's apps and Gentoo's) and `/usr/local/share`

It watches those folders, so a new app shows up as soon as its desktop file appears, and disappears when it goes. App icons come from the icon theme like on any desktop. When you start an app from the launcher, Chrome asks nickel-appd to start it, and it starts with your full session environment. The window then gets grouped under the app's shelf item, matched by its Wayland app id or X11 window class, the desktop file's `StartupWMClass`, or its name.

[Under the hood](/help/under-the-hood/#nickel-appd) has the whole list of what nickel-appd does.

## Uninstalling

Right-click an app in the launcher or on the shelf and pick **Uninstall**. NickelOS removes it through whatever installed it:

| Installed with | Removed by |
|---|---|
| Flatpak, for everyone | `flatpak uninstall`, after polkit checks you're allowed |
| Flatpak, just you | `flatpak uninstall --user` |
| Gentoo (emerge) | `emerge --deselect` and `emerge --depclean` for that package, after an administrator's password |
| AppImage | Deleting the AppImage and its desktop file |
| Steam (a game) | Steam's own uninstall dialog |
| Android (an APK) | Android's package manager, in your Android |

Settings > Apps > Manage your apps shows, for each Linux app, where it came from, its version, its size and how much data it keeps (for a Flatpak, what's in `~/.var/app/<app id>`).

## Default apps, links and "Open with"

**Settings > Apps > Default apps** picks the app that opens images, videos, music, PDFs, text files, documents, spreadsheets and presentations, the app that gets email (`mailto:`) links, and what the Print Screen key opens. It covers ChromeOS's own apps, web apps and Linux apps, and it's used by Files and by Linux apps alike.

When a Linux app opens something with `xdg-open` (or GLib, or the OpenURI portal), NickelOS hands it to ChromeOS: links open in the browser, files open in their default app, and folders open in Files. That's `nickel-open`, the default handler in `/usr/share/nickel/applications`. Your own choices in `~/.config/mimeapps.list` still win over NickelOS's.

## Notifications, tray icons and starting at sign-in

- **Notifications** from Linux apps (libnotify, GNotification, the portal) are ChromeOS notifications, with the app's name, icon and buttons. Clicking one brings the app forward.
- **Tray icons.** ChromeOS has no system tray for apps, so on NickelOS the shelf is the tray. An app that closes to the tray (Discord, Slack, Steam, Telegram) keeps its shelf item while it runs, even with no window open. Click it to bring the window back. Right-click it for the app's own tray menu, and **Close** quits it for real. Old X11 tray icons (Wine programs, Java apps) work the same way.
- **Start at sign-in.** An app's "start when I sign in" setting works. NickelOS starts what's in `~/.config/autostart` and `/etc/xdg/autostart` once per sign-in, after the desktop is up.

## When an app asks for something

Linux apps ask the desktop for files, the camera, the screen and so on through **desktop portals** (xdg-desktop-portal), the same way they do on GNOME and KDE. NickelOS has its own portal backend, `xdg-desktop-portal-nickel`, that answers with ChromeOS's own dialogs. Flatpak apps use portals always. Other apps use them too, because the session sets `GTK_USE_PORTAL=1` and `GDK_DEBUG=portals`.

| The app wants to | What you see |
|---|---|
| Open or save a file | ChromeOS's own file picker |
| Use the camera | "Allow (app) to use the camera?" The answer is remembered for that app |
| Know your location | Flatpak apps ask once, with ChromeOS's location dialog. The Privacy controls switch covers every Linux app |
| Share your screen | ChromeOS's "share your screen" picker, then a notification with **Stop** while it's shared |
| Control the device (remote desktop) | "Allow remote control?", then a notification with **Stop** |
| Take a screenshot | The same picker. The picture is saved in `~/Pictures/Screenshots` |
| Use keyboard shortcuts while in the background | "Allow keyboard shortcuts?" with each shortcut's keys, which you can change |
| Print | ChromeOS's print preview, with your printers and Save as PDF |
| Keep the screen on (video, games, downloads) | Nothing: it just stays on until the app lets go |
| Remember a password or sign-in | Nothing: it goes into your keyring (below) |
| Pick a colour from the screen | ChromeOS's eyedropper |
| Set the wallpaper | Nothing: it's set, like choosing one in Wallpaper and style |
| Add its own launcher (a web app, a program it manages) | A dialog asking first |
| Your name and picture | A dialog asking first |
| Send an email with attachments | Your default mail app with a new message |

Linux apps also follow ChromeOS's dark or light mode, accent colour and fonts, live, through the same backend.

**Saved passwords.** Apps keep passwords and sign-ins in the Secret Service, which on NickelOS is gnome-keyring. Your login keyring unlocks itself when you sign in. Its password is a random key made for your account, kept at the top of your encrypted vault, outside the home folder apps can see. Flatpak apps get their own key through the Secret portal.

**Location** comes from GeoClue. When an app asks and location is allowed, GeoClue sends the Wi-Fi networks your device can see, and your internet address, to beaconDB, an open location service, and gets back a position. If the Privacy controls switch is off, GeoClue gives apps nothing.

**Changing your mind.** The Flatpak permission store holds the answers you gave. `flatpak permissions` lists them, and `flatpak permission-reset <app id>` forgets them for an app, so it asks again next time.
