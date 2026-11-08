title: Getting around
order: 10
summary: The NickelOS desktop, setup, local accounts and administrators, the screen saver, the Settings pages NickelOS adds, Wi-Fi and phones.
---
# Getting around

The desktop is ChromeOS's, so if you've used a Chromebook you already know most of it. This page covers what's different on NickelOS and where its own settings are.

## The desktop

- **The shelf** along the bottom holds your pinned apps and whatever is running. NickelOS pins Chrome, Files, Terminal and Bazaar to start with. Right-click an app to pin or unpin it, or to uninstall it.
- **The launcher** is the circle at the left end of the shelf, or the Search key on your keyboard (the Windows key on most PC keyboards). Every app is in there, Linux apps included. Its search box looks through apps, files and settings.
- **Quick Settings** is at the right end of the shelf: Wi-Fi, Bluetooth, volume, the output device for sound, Night Light, and the way into Settings.
- **Desktop icons.** Anything in your Desktop folder shows on the wallpaper. Icons stay where you put them. Right-click the wallpaper for **New folder**, or right-click an icon to **Open**, **Show in Files**, **Rename** or **Move to trash**. Files you drag from the Files app onto the desktop are moved into the Desktop folder. Pictures and videos show a preview of themselves.

Linux app windows are ChromeOS windows: they snap to the sides, show up in overview, go to other desks and follow tablet mode like the browser does.

## Setup

- **No network needed.** If you don't have Wi-Fi handy, pick **Set up offline** on the network screen.
- **Language.** The welcome screen offers every language ChromeOS has, and NickelOS's own screens and settings are translated into all of them.
- **How would you like to start?** Setup asks this once, for the whole device. **Full experience** makes DuckDuckGo the search engine in the browser and the launcher, adds Proton's apps (Mail, Calendar, Drive, Docs, Pass, Lumo) as web apps, puts uBlock Origin in the browser, and installs free apps such as LibreOffice, GIMP, Kdenlive and OBS Studio. **Minimal** keeps the launcher's search on the device and sets up nothing else, and the browser asks you which search engine to use. Bazaar and the apps that open pictures, videos, music and PDFs come with both. Any of it can be changed later.
- **Your account.** You choose a name, a username and a password. The username becomes your Linux user name and your home folder, `/home/<username>`. The account gets a picture at random. **Change** shows the rest of them, and later you can use a photo of your own in Wallpaper and style.
- **Computer name.** Setup suggests one from your name, and you can change it. It's the device's hostname, and the name other computers, Bluetooth devices and paired phones see. It's in **Settings > About NickelOS** later.
- **Installing.** When you install from a USB stick, the installer asks which disk to use.

## Your account is a Linux user

There are no Google accounts on NickelOS. Each person on the device has a local account, and each local account is a real Linux user:

- It has its own user ID (local accounts get IDs from 5000 to 5999) and its own groups.
- Its home is encrypted. ChromeOS's cryptohome keeps each home in its own encrypted vault, unlocked by that person's password when they sign in. While you're signed out, nobody else on the device can read it, administrators included.
- Chrome, every app you start and the Terminal all run as you. A file you save from a Linux app belongs to you, the same as on any Linux desktop.

> Your password is the key to your encrypted home. Nobody can open the vault without it, including us, so keep it somewhere safe.

### Administrators

The first account made on the device is its owner, and an administrator. Administrators:

- have `sudo` in the Terminal (they're in group `wheel`; sudo asks for your password);
- install Flatpak apps for everyone without a password, through Bazaar or `flatpak`;
- install Gentoo packages with `emerge`;
- answer the password prompts other programs ask for through polkit (`pkexec`, disk tools, removing system apps).

Standard users can do all of those too, if an administrator types their password into the prompt. The prompt is ChromeOS's own password dialog, with the account's picture. When you aren't an administrator, it asks for an administrator's password and says whose.

### Settings > Accounts

NickelOS replaces ChromeOS's Google accounts page with its own:

- **Change password** changes both the password that unlocks your home and your Linux password, together.
- **Add person** signs you out. On the sign-in screen, the new person picks a name, a username and a password.
- Administrators can make someone else an administrator or a standard user, and remove accounts. Removing an account deletes its files from the device. Every one of these asks for your password first, and the device always keeps at least one administrator.

**Guest** sessions work as on ChromeOS. Everything from a guest session is wiped when the guest signs out.

## The Settings pages NickelOS adds

| Where | What it does |
|---|---|
| Accounts | Local accounts, passwords, administrators |
| Apps > Default apps | Which app opens images, videos, music, PDFs, text files, documents, spreadsheets and presentations, which app gets email links, and what Print Screen opens |
| Apps > Linux apps | **Shortcut keys for older Linux apps**, for push-to-talk in X11 apps (see [Typing and keyboard](/help/input/)) |
| Apps > Manage your apps > (an app) | For a Linux app: where it came from (Flatpak, Portage, AppImage), its version, its size and the size of its data |
| Connected devices > Phones and tablets | Phones paired through KDE Connect |
| System preferences > Storage and power > Storage management > Other drives | The other drives inside this computer, Windows included (see [Your files](/help/files/#other-drives)) |
| System preferences > Files > Cloud storage | Google Drive, OneDrive, Dropbox, Proton Drive and others, through rclone |
| Privacy controls | The location switch covers Linux apps too, and apps can only be allowed the camera while camera access is on |
| Wallpaper and style > Screen saver | A slideshow of your Pictures folder, with the weather if you want it (see [Screen saver](#screen-saver)) |
| About NickelOS | The version, **Check for updates**, the update channel, and the computer's name |

## Screen saver {#screen-saver}

Turn it on in **Settings > Wallpaper and style > Screen saver**. It shows the pictures in your Pictures folder, and the folders inside it, as a slideshow with the time over it. It needs at least two pictures.

**Show the weather** is off until you turn it on. It then adds the temperature and an icon for the conditions. The weather comes from Open-Meteo, for a rough location (about a city) that beaconDB works out from your internet address. Nothing else is sent. It's fetched only while the screen saver is showing, at most every 15 minutes, and the location switch in **Privacy controls** still applies. Fahrenheit or Celsius is right under the switch.

## Wi-Fi

A Wi-Fi network you join is shared with the sign-in screen and the other accounts on the device, the way most Linux distributions do it. That way the device is online before anyone signs in. Turn off **Share** when you join a network to keep it to your account.

## Phones and tablets

NickelOS uses KDE Connect instead of Google's Phone Hub. Support for it is still being finished. Install **KDE Connect** on your phone (Google Play, F-Droid or the App Store) and open it on the same Wi-Fi network. Your NickelOS device shows up there, and the phone shows up in **Settings > Connected devices > Phones and tablets**. Pair from either side.

For a paired phone, Settings shows whether it's connected and its battery, and has **Ring** and **Send files** (with ChromeOS's file picker). **Open Valent** opens the full app for everything else KDE Connect does. Valent is the KDE Connect app on the NickelOS side, and it runs in every session.

## Sleep

Closing the lid on the sign-in screen puts the device to sleep, the same as closing it while signed in.
