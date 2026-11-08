title: The NickelOS guide
nav: Start here
order: 0
summary: How NickelOS works: installing it, Linux apps on ChromeOS's desktop, Gentoo underneath, your account, sound, graphics, games, files, updates, and where to look when something breaks.
---
# The NickelOS guide

NickelOS is ChromiumOS with the Linux underneath opened up. You get ChromeOS's desktop, and Linux apps run right on it. They aren't in a virtual machine: they run on the device, as your own Linux user, next to the browser.

This guide is how it all fits together. It covers where things live, what runs when you sign in, how sound and graphics get from an app to your screen and speakers, and what to check when something goes wrong. It's written for anyone who likes to know what their computer is doing. You don't need to read it front to back.

If you just finished setup and took the tour, [Getting around](/help/basics/) picks up where the tour left off. If something is broken, go straight to [Troubleshooting](/help/troubleshooting/).

## The pages

| Page | What's in it |
|---|---|
| [Installing NickelOS](/help/install/) | What a PC needs, writing the USB stick, Secure Boot and NickelOS's key, installing or trying it from the stick first |
| [Getting around](/help/basics/) | The desktop, setup, your account and administrators, the Settings pages NickelOS adds, phones |
| [Apps](/help/apps/) | Bazaar and Flathub, the apps you start with, AppImages, uninstalling, what happens when an app asks for the camera, your location or your screen |
| [Android apps](/help/android/) | Setting up Android, installing APKs, ARM apps, how Android draws on your GPU, the waydroid command |
| [Gentoo and Portage](/help/gentoo/) | Why NickelOS is Gentoo, Gentoo at /, emerge and eix, 32-bit (i386) programs |
| [Learning Gentoo](/help/learning-gentoo/) | New to Gentoo: what Portage's words mean, a first session with emerge, USE flags, what to do when emerge says no, pacman and apt commands side by side |
| [The Terminal and the system](/help/terminal/) | Your shell, sudo, the session environment, what runs when you sign in, your own services |
| [Graphics](/help/graphics/) | How Wayland and X11 apps reach the screen, the Wayland protocols, Intel, AMD and NVIDIA |
| [Sound, camera and screen sharing](/help/sound/) | PipeWire into CRAS, microphones, webcams, screen sharing and remote control |
| [Steam and games](/help/games/) | Steam, Proton, games in the launcher, controllers, Wine |
| [Your files](/help/files/) | One home folder, the Trash, the desktop, USB drives, other drives, network shares, cloud storage |
| [Typing and keyboard](/help/input/) | Input methods for Chinese, Japanese and Korean, middle-click paste, shortcut keys for apps |
| [Updates](/help/updates/) | Channels, how an update installs, what an update keeps |
| [Under the hood](/help/under-the-hood/) | How NickelOS is built, nickel-appd, and the patches we carry |
| [Troubleshooting](/help/troubleshooting/) | Logs, crash reports, the usual problems and their fixes |
| [Getting help](/help/community/) | The forum, Discord, GitHub, and how to write a bug report we can act on |

## NickelOS in two minutes

- **Your account is a Linux user.** Setup makes a local account. No Google account is involved, and setup works offline. The account is a real Linux user with its own encrypted home at `/home/<your username>`. Chrome, your apps and the Terminal all run as that user. The first account is an administrator and has `sudo`.
- **Apps come from where they come from on any Linux distribution.** Bazaar installs Flatpaks from Flathub. AppImages run. Gentoo's whole package tree is one command away. Whatever you install shows up in the launcher and on the shelf like any ChromeOS app.
- **Android apps run too.** Set up Android in Settings, and Waydroid runs Android apps in windows of their own. Opening an APK file installs it.
- **Under it all is Gentoo.** ChromiumOS has always been built with Gentoo's Portage. On NickelOS your device is Gentoo too: current Gentoo at `/`, with `emerge` and `eix`. The system's own packages stay in the read-only image, under what you install.
- **Sound goes through ChromeOS.** Linux apps play into a PipeWire server in your session, which hands the sound to CRAS, ChromeOS's own audio server. Your volume keys and output picker work for everything.
- **Graphics run on the GPU.** Intel and AMD use the kernel's drivers and Mesa. NVIDIA cards use NVIDIA's own driver. Wayland apps talk to ChromeOS's compositor directly, and X11 apps go through Xwayland.
- **Updates work like ChromeOS's.** The system lives on a read-only partition. An update installs into the spare one, and a restart switches over. Your files, apps and Gentoo packages live on a separate data partition, so updates leave them where they are.
