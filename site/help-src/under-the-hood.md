title: Under the hood
order: 110
summary: How NickelOS is put together. The nickel board and its overlay, NickelOS's own packages, nickel-appd in detail, and the patches NickelOS carries to Chrome and ChromiumOS, with what each one is for.
---
# Under the hood

This page is for anyone who wants to see what NickelOS actually changes. It's also the map for troubleshooting: if something's off, the piece responsible is probably in one of these tables.

## The board and the overlay

NickelOS is the ChromiumOS board `nickel`, which inherits `reven`. Everything NickelOS adds or changes is in one overlay, and that keeps moving to a new ChromiumOS release manageable:

- **New packages** go in the overlay as ebuilds, ours and ported ones.
- **Changes to Google's packages** are patch files under `patches/<category>/<package>/`. A profile hook applies them when the package builds, so Google's code itself stays untouched in the tree.
- **Settings for the board** live in its profile: USE flags, the firmware list, kernel options, which of Google's packages are masked.

Beyond the stock board, NickelOS adds:

- **Firmware.** The full set for PCs, not just Chromebooks: Wi-Fi, Bluetooth, and Intel, AMD and NVIDIA graphics.
- **Kernel.** `virtio_blk` built in, so NickelOS boots as a VM too.
- **No hardware reporting.** reven sends hardware reports to Google, and NickelOS drops that package. It also has its own update ID and identity (`NAME=NickelOS` in `/etc/os-release`).

## NickelOS's own packages

| Package | What it does |
|---|---|
| `nickel-session` | Your session: runtime directory, one home, the environment, the session bus, X11, PipeWire and the PulseAudio server, the FIFO exception Steam needs |
| `nickel-appd` | The bridge between Chrome and Linux apps (below) |
| `nickel-portal` | `xdg-desktop-portal-nickel`, the portal backend that answers apps with ChromeOS's own dialogs |
| `nickel-accounts` | `nickel-accountsd`: local accounts, passwords, administrators, the `wheel` sudo rule |
| `nickel-logind` | Registers sessions with elogind, so polkit and login1 know who's signed in |
| `nickel-userland` | `/usr/local` always mounted and allowed to run programs, the system's library cache, your own services |
| `nickel-gentoo` | Your Gentoo layer over `/usr` and `/etc` (overlay mounts from the data partition), the package tree and compiler at the first startup, Gentoo's system accounts, OpenRC for your services, putting the system's version of a package back, uninstalling from the launcher, 32-bit NVIDIA libraries |
| `nickel-flatpak` | Flatpak on the data partition, Flathub, the first-boot apps |
| `nickel-cloud` | Cloud storage through rclone |
| `nickel-phone` | Valent (KDE Connect) in every session |
| `nickel-update` | What update_engine needs: NickelOS's payload key and Let's Encrypt's root certificates |
| `nickel-x11-keys` | Shortcut keys for X11 apps in the background |
| `nickel-xembed-tray` | Old-style X11 tray icons on the shelf |
| `nickel-xim` | The XIM server for X11 apps' input methods |
| `nickel-mozc` | Japanese input: Mozc in WebAssembly |
| `nickel-desktop-base` | The libraries Linux apps expect: GTK 3, GTK 4, libadwaita, AT-SPI, X11 extensions |
| `nickel-waydroid` | Android apps: Waydroid's container job, `nickel-waydroidd`, the APK handler, ARM app support, waydroid-nvidia's setup (see [Android apps](/help/android/)) |
| `nvidia-open` | NVIDIA's open kernel modules and userspace, built and signed for NickelOS's kernel |
| `chromeos-bsp-nickel` | Board support: reven's hardware support minus Google services, plus NickelOS's identity and wallpaper |
| `chromeos-config-bsp` | The model configuration reven keeps private, with Flex's per-PC power quirks |
| `google-sans-flex`, `google-sans-code` | The interface fonts (open source), standing in for the proprietary Google Sans |

Added on top of ChromiumOS (mostly ported from Gentoo, or newer than ChromiumOS's own copy): Flatpak, bubblewrap, ostree, xdg-desktop-portal, xdg-dbus-proxy, PipeWire, WirePlumber, Xwayland, GTK 3 and 4, libadwaita, elogind, polkit, accountsservice, gnome-keyring, GeoClue, Valent, libheif and libde265, ffmpegthumbnailer, `nvidia-vaapi-driver`, Valve's `steam-devices` udev rules, Waydroid with LXC and libgbinder (patched for ChromeOS's network, Android's time zone, and waydroid-nvidia), waydroid-nvidia's render server and Android files, and the libraries those need.

## nickel-appd {#nickel-appd}

nickel-appd is the most NickelOS-specific piece, and the one to look at first when an app misbehaves in the launcher or on the shelf. It runs in your session as you (`nickel-appd` job), inside Chrome's mount namespace, with the session environment, and owns `org.nickelos.Apps` on the system bus. Only you, the user Chrome runs as, can call it.

Chrome keeps Linux apps in its GuestOS registry, which on a Chromebook is filled from a VM by `vm_cicerone`. nickel-appd fills it for the device itself, as VM `nickel`, container `host`:

| What | How |
|---|---|
| Apps in the launcher | Reads every `.desktop` file in `XDG_DATA_DIRS`, copies each icon where Chrome looks first, and sends the list to Chrome. It sends it again whenever apps come or go |
| Starting apps | `LaunchApplication(desktop_file_id, args)`: Chrome asks, nickel-appd starts it with your session environment |
| The Terminal | `SpawnShell(terminal, status, command, env)`: starts your login shell on the terminal the Terminal app hands over, outside Chrome's sandbox |
| Dark/light mode and accent colour | `SetAppearance` from Chrome, passed on to the portal backend, which tells apps |
| The file picker | `FileSelected`: Chrome's answer to the portal backend's file dialog |
| Notifications | `NotificationEvent`: clicks, buttons and closing, back to the app |
| Tray icons | It's the tray apps register with (`org.kde.StatusNotifierWatcher`). `TrayEvent` carries clicks and menu choices back |
| Uninstalling | `UninstallApplication`: through Flatpak, Portage (with `pkexec`) or by deleting an AppImage |
| App info | `GetAppDetails`: version, size, data size for Settings |
| Autostart | Starts `~/.config/autostart` and `/etc/xdg/autostart` entries once per sign-in |
| Location | The GeoClue agent: apps get a location only while ChromeOS's location switch allows it. It also asks ChromeOS's network manager (shill) for Wi-Fi scans, since GeoClue may not start them itself |
| Steam's games | Writes launcher entries for installed Steam games |
| Previews | `Thumbnail(path, size)`: previews of what Chrome can't decode, made by sandboxed thumbnailers |

It also installs `nickel-open`, the handler that sends links and files from Linux apps to ChromeOS. Its messages go to the system log: `grep nickel-appd /var/log/messages`.

## The patches we carry {#patches}

### Chrome

Chrome (Ash, ChromeOS's desktop, is part of Chrome) carries most of NickelOS's work. Each patch explains itself in its header. Here's what each one is for:

| # | What it does |
|---|---|
| 0001 | Draws apps' shared-memory buffers with padded rows correctly (windows were sheared) |
| 0002 | Lists and launches Linux apps on the device |
| 0003 | The Terminal opens a shell on the device |
| 0004 | NickelOS's shelf pins: Chrome, Files, Terminal, Bazaar |
| 0005 | Local accounts |
| 0006 | Setup works without a network |
| 0007 | The installer lets you pick the disk |
| 0008 | A local account's Chrome runs as that Linux user |
| 0009 | De-Googled defaults: no Google app storefront, no "recover user" for local accounts |
| 0010 | The product name, device name and installer say NickelOS |
| 0011 | An app's overlay-priority hint can't crash Ash (it crashed with every Electron app) |
| 0012 | Linux apps on the device are their own kind of app, not Crostini's |
| 0013 | Linux apps follow ChromeOS's dark/light mode and accent colour |
| 0014 | ChromeOS's file picker for Linux apps |
| 0015 | Uninstalling Linux apps from the launcher and the shelf |
| 0016 | Linux apps in Settings' app pages |
| 0017 | Files dropped or pasted into Linux apps arrive as paths |
| 0018 | Linux app windows come back after a restart, like the browser's |
| 0019 | Linux app windows take the focus as they do on a Linux desktop |
| 0020 | Linux apps open files with ChromeOS's default apps |
| 0021 | Settings > Apps > Default apps |
| 0022 | Screen sharing for Linux apps |
| 0023 | Remote control for Linux apps |
| 0024 | Local accounts load their own network settings (secured Wi-Fi) |
| 0025 | Linux app windows open at their own size (GTK 4 windows opened tiny) |
| 0026 | Settings > Accounts manages local accounts |
| 0027 | Linux apps' notifications are ChromeOS notifications |
| 0028 | Phones through KDE Connect in Settings |
| 0029 | Linux apps see only standard Wayland protocols |
| 0030 | The sign-in screen sleeps instead of shutting down |
| 0031 | Exo survives bad requests from Linux apps |
| 0032 | The GPU sandbox lets NVIDIA's driver work |
| 0033 | Exo checks shared-memory files that can't be sealed before reading them |
| 0034 | ChromeOS's cursors for Linux apps (cursor-shape-v1) |
| 0035 | NVIDIA GPUs decode video through VA-API on NVDEC |
| 0036 | text-input-v3 (input methods) and primary selection (middle-click paste) in Exo |
| 0037 | One home: My files is your Linux home |
| 0038 | Files from Linux apps reach Chrome, checked against the app's sandbox |
| 0039 | Files and Linux apps share one Trash |
| 0040 | Files calls My files "Home" |
| 0041 | Search suggestions off by default |
| 0042 | The Japanese input method may run WebAssembly |
| 0043 | The input method's info list no longer crashes the browser |
| 0044 | Wi-Fi joined in a session is shared by default |
| 0045 | Linux apps' tray icons live on the shelf |
| 0046 | Global shortcuts for Linux apps |
| 0047 | Shortcut keys for X11 apps in the background |
| 0048 | Linux apps may lock the pointer (mouse look in games) |
| 0049 | Settings for the computer's other drives |
| 0050 | Cloud storage in Settings and Files through rclone |
| 0051 | Settings' Files page without Google Drive, OneDrive and Microsoft 365 |
| 0052 | No DriveFS (Google Drive's client) for local accounts |
| 0053 | Icons for your home's folders in Files |
| 0054 | No content recommendations |
| 0055 | GNOME's apps open pictures, videos, music and PDFs (Google's Gallery can't work outside ChromeOS) |
| 0056 | The OS is called NickelOS everywhere |
| 0057 | Desktop icons for `~/Desktop` |
| 0058 | Setup's first-start screen |
| 0059 | Account pictures from Chromium itself, chosen in setup, offline |
| 0060 | Linux apps ask for the camera |
| 0061 | Exo's `wl_surface` has `damage_buffer` (GTK 4 and WebKitGTK apps) |
| 0062 | No "Google API keys are missing" bar |
| 0063 | Linux apps can keep the screen on |
| 0064 | Linux apps print through ChromeOS's print preview |
| 0065 | Screenshots for Linux apps |
| 0066 | Picture previews on the desktop and in Files (HEIC, H.265) |
| 0067 | Linux apps can pick colours from the screen and set the wallpaper |
| 0068 | The clipboard for remote desktop sessions |
| 0069 | Linux apps ask for the location |
| 0070 | Chrome keeps running when the Linux session's D-Bus goes away at sign-out |
| 0071 | NickelOS's own Welcome Tour |
| 0072 | Settings > Apps > Android apps. Linux apps may open pages of Settings |
| 0073 | The Welcome Tour shows Android apps |
| 0074 | App Management calls Android apps Android apps |
| 0075 | Exo keeps a cursor that didn't change |
| 0076 | Clicks reach the windows behind Android apps (Exo took input regions reaching the edge of its coordinate range as empty) |
| 0077 | Exo sends no placeholder size to a window it shows (Firefox opened at its smallest size) |
| 0078 | The GPU sandbox lets Gentoo's Mesa load |
| 0079 | On AMD, the GPU process loads the radeonsi driver minigbm allocates buffers with, kept apart from Gentoo's Mesa |
| 0080 | The GPU sandbox lets Mesa ask for `F_DUPFD_QUERY` |
| 0081 | The GPU sandbox lets drivers ask for the scheduling priority range |
| 0082 | The GPU process runs without its sandbox when the sandbox kept it from starting at all |
| 0083 | Settings > About NickelOS shows the computer's name |
| 0084 | The Bluetooth name is the computer's name |
| 0085 | No AI Mode, and Safe Browsing off by default |
| 0086 | Setup offers two ways to start |
| 0087 | No Google account check (ListAccounts) |
| 0088 | No Privacy Sandbox or AI components |
| 0089 | Setup's Full experience, for every account on the device |
| 0090 | Manifest V2 extensions, so uBlock Origin keeps working, and the extensions Full experience adds |
| 0091 | The digital internal mic wins a tie with the analog one |
| 0092 | On a Minimal device, the browser asks which search engine to use |
| 0093 | Print Screen can open a screenshot app |
| 0094 | Exo names its outputs (`xdg_output.name`) |
| 0095 | The search engine choice screen works outside the EEA |
| 0096 | A screenshot app you've allowed takes screenshots without the picker asking again |
| 0097 | The digital internal mic is picked at the first sign-in too |
| 0098 | Creating a local account waits for a slow disk |
| 0099 | No Crostini: nothing offers to set up Linux in a VM |
| 0100 | Default apps for documents, spreadsheets and presentations |
| 0101 | Welcome Recap brings back Linux apps' windows, with no Google parts |
| 0102 | Setup names the computer along with the first account |
| 0103 | The administrator password prompt is ChromeOS's own |
| 0104 | Default apps: Email |
| 0105 | The screen saver is a slideshow of your Pictures folder |
| 0106 | The screen saver's weather (Open-Meteo, off until you turn it on) |
| 0107 | No growth campaigns (Google's promotions) |
| 0108 | The launcher button is always ChromeOS's circle |
| 0109 | Linux apps that run in a terminal open in the Terminal |

### ChromiumOS's other packages

| Package | Patch |
|---|---|
| `chromeos-login` (session_manager) | A local account's session runs Chrome as its own user. No hardware overlays on NVIDIA (page flips timed out) |
| `cryptohome` | Your home allows programs and symlinks (ChromeOS mounts it `noexec`, `nosymfollow`, which breaks AppImages, scripts and Flatpak). A local account owns its own vault |
| `selinux-policy` | cryptohome's namespace mounter may read `/etc/passwd` (local accounts' users). Files seen through the overlay mounts on `/usr` and `/etc` keep their own labels. `lsblk` may read udev's database |
| `cros-disks` | Mounts belong to the signed-in user. Other systems' partitions are hidden, and Windows' volume opens read-only. Read-only disk mounts are really read-only (ChromeOS's FUSE mounts still wrote to the disk) |
| `crash-reporter` | A local account's crash reports go into its own encrypted home, where it can write them |
| `chromeos-init` | The kernel may start elogind's cgroup release agent, and `crash_reporter` by its merged-`/usr` path, so crashes are collected |
| `os_install_service` | The installer installs to the disk you picked. Its sandbox allows the calls Gentoo's `lsblk`, `cp` and `lvm` make |
| `system_api` | A VM type of its own, `NICKEL_HOST`, for apps on the device |
| `sommelier` | Explicit sync for X11 apps on NVIDIA. virtio-gpu calls only on virtio-gpu (they froze NVIDIA). ChromeOS's cursors for X11 apps |
| `update_engine` | Downloadable system parts (handwriting, voices, fonts) from NickelOS's server instead of Google's |
| `minigbm` | An `nvidia-drm` backend on NVIDIA's own buffer allocator. On AMD, it loads ChromiumOS's older AMD driver, which it allocates through, from `/usr/lib64/cros/dri`, kept apart from Gentoo's Mesa |
| `adhd` (CRAS) | No generic HD-Audio configuration for AMD's "HD-Audio Generic" codec (it left the speaker muted) |
| `cras_rust` (CRAS) | No requests for the noise cancellation model, which only Chromebooks can download |
| `grub` | Falls back to the previous system when a new one doesn't boot |
| `crosh-extension` | The Terminal's home page offers the device, not a Linux VM to set up |
| `libhwsec-foundation`, `missive` | Key derivation (HKDF) works with Gentoo's OpenSSL, which refuses an empty salt passed as a null pointer |
| `minijail` | Services can bind-mount paths that go through merged `/usr`'s links |
| `spaced` | Its sandbox lets Gentoo's `lvm` list devices and check semaphores |
| `screen-capture-utils` | `screenshot` uses the EGL device of the card it captures (it failed with Gentoo's Mesa on Intel and AMD) |
| `vboot_reference` | `crossystem` doesn't print errors about the Chromebook firmware device a PC doesn't have |
| `chromeos-hangul` | Korean input in JavaScript (Chrome no longer runs Native Client) |
| `chromeos-kernel` | NickelOS turns off two of the ChromiumOS security module's mount restrictions so Flatpak's sandbox (bubblewrap) can mount inside user namespaces and through symlinked paths. This patch keeps the kernel building that way. A second lets root mount overlayfs, which Waydroid uses over Android's system image. A third adds the full ntsync driver from Linux 6.18 (`/dev/ntsync`), which Wine and Proton use for Windows' events, mutexes and semaphores |
| `shill` | ChromeOS's network manager leaves Waydroid's network bridge alone, as it does ARC's |
