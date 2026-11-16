title: NickelOS is released
slug: nickelos-is-released
date: 2026-11-15
summary: Our first stable release. What NickelOS changes about ChromeOS, why it's Gentoo underneath, what we took out, and why we're keeping ChromeOS's desktop going now that Google is leaving it behind.
release: yes
---
# NickelOS is released

NickelOS is out now! NickelOS 1.0 is our first stable release.

NickelOS is a modified fork of ChromiumOS that comes with enhancements, fixes, and native Linux application support. This is our vision of what ChromeOS should have been!

I've always liked ChromeOS's desktop. It was (and still is) fast and stays out of your way. What bugged me was everything around it. On a Chromebook, Linux apps run in a Debian container inside a virtual machine, on a OS that's already Linux. And they want a Google account before even wanting to use it! And outside of Chromebooks, there's ChromeOS Flex, which plays by the same rules.

So I took ChromiumOS apart and built it back up around Gentoo Linux that was in there all along. That turned into hundreds upon hundreds hours of work, and most of it went into details you'll hopefully never notice, because they just behave the way you'd expect from a Linux Distro

## This is our answer to Googlebook OS

What put nail in the coffin for me on kicking into high gear on developing NickelOS was the announcement of GoogleBook OS. Instead of improving on the formula they already had, Google decided to abandon ChromeOS. ChromeOS updates stop in 2034, and what Google is putting in its place is Googlebook OS, an Android-based operating system that's plagued with tons bugs. Most Android apps don't work on it, and it crashes constantly.

The end of ChromeOS is also somewhat bad timing. Because RAM is absolutely expensive right now.  Even though laptops should be moving to 32 GB of RAM, we're moving back to 16 GB, or even 8GB! One of the huge things about ChromiumOS is how little memory it uses. ChromeOS is made to run on laptops from as low as 2GB of RAM, it absolutely sips memory. And with NickelOS since Linux apps run right on the system, there's no virtual machine holding on to its own share of your RAM.

We plan to follow every ChromiumOS release Google ships until 2034, and after that to keep the ChromiumOS desktop going ourselves, on Gentoo.

## ChromeOS and NickelOS, side by side

| | ChromeOS | NickelOS |
|---|---|---|
| Linux apps | In a Debian container, inside a virtual machine | On the system itself, as your own Linux user |
| Linux apps and the GPU | Through the virtual machine's virtual GPU | Straight on the GPU |
| Your account | A Google account | A local account, no Google account at all |
| Installing software | Google Play, and `apt` inside the VM | Flathub through Bazaar, AppImages, and Gentoo's package tree |
| Android apps | Google Play on Chromebooks (ChromeOS Flex has none) | Waydroid, from APKs |

## Linux apps, without the virtual machine

On NickelOS there's no Linux container to start and no folder to share with it. The Linux under ChromeOS is the system you're using. Linux apps draw straight on Exo, the Wayland server built into ChromeOS's desktop, and X11 apps go through Xwayland, both with the GPU. They're in the launcher and on the shelf, and their windows snap to the sides, show up in overview and move between desks like the browser's.

Most of the work was making them feel like they belong here. We wrote our own portal backend, `xdg-desktop-portal-nickel`, the piece that answers when an app asks the desktop for something, the same job GNOME and KDE each have one for. Because of it, the file picker, the screen sharing prompt with its Stop button, notifications, tray icons and `pkexec` password prompts in Linux apps are all ChromeOS's own. Chinese, Korean and Japanese input works in Linux apps, old X11 ones included. Most apps get ChromeOS's title bar and window buttons, and GTK 4 apps keep their own header bar, so you never end up with two rows of buttons.

Files opens at your home folder, and it's the same home folder the Terminal and every app see.

## Gentoo underneath

Why Gentoo, and not Debian or Arch? Because it was already there. ChromiumOS has always been built with Portage, Gentoo's package manager. Google's own build system uses it to put together every ChromeOS image. Then the finished image ships without any of it, with no package manager for you. NickelOS keeps it. The system you're running is a current Gentoo, with one glibc, Gentoo's Python and Gentoo's Mesa, and ChromeOS's programs on top, all in Portage's database.

That means `sudo emerge` just works. Anything in Gentoo's tree or GURU, with your own USE flags. `emerge` grabs a ready-built package when there is one, from Gentoo's own signed binary packages or from ours, which carry what Gentoo's don't, like the 32-bit libraries Steam needs. Anything else builds on your machine. The package tree and the compiler set themselves up after setup, once you're online, at low priority so they don't slow you down. The services you add run under OpenRC, so an SSH server or Tailscale works the way the Gentoo wiki says it does.

Fitting that inside a ChromeOS system took some doing. ChromeOS's system partition is read-only, checked at every boot, and replaced whole by every update. So what you install goes into your own layer on the data partition, laid over `/usr` and `/etc`, and it stays there through every update. You keep it current with `emerge -uDN @world`, like on any Gentoo. If you rebuilt one of the system's own packages, say with different USE flags, and an update brings a newer version of it, the system's copy takes over and yours is rebuilt with your flags at the next startup. `nickel-gentoo restore` puts the system's version of a package back whenever you want. [Gentoo and Portage](/help/gentoo/) in the guide has the whole story.

## No Google account, no Google services

Setup works offline and makes a local account. It's a real Linux user with an encrypted home folder, and the first account on the computer is an administrator with `sudo`. There's no Google sign-in anywhere in setup, and no screens for setting up from an Android phone or enrolling the computer in Google's device management.

Then you pick how you'd like to start. The Full experience sets you up with DuckDuckGo, uBlock Origin, Proton's apps and a set of free apps. Minimal gives you none of it.

The Google features ChromeOS puts on top are gone too. No Google AI features, no content recommendations, no promotions, and search suggestions stay off unless you turn them on. We also went after what Chrome contacts all by itself. Left sitting at the sign-in screen, it now asks Google for one thing on its own, the component updater, which we kept because it brings security data like certificate revocation lists.

Your phone pairs through KDE Connect instead of Google's Phone Hub, so you can ring it, send it files and see its battery. That part is still being finished. Android apps run through Waydroid, which you turn on in Settings. If you do want Google Drive, you can add it to Files in Settings with your own key from Google, next to OneDrive, Dropbox, Proton Drive and Nextcloud. It's only there if you add it.

## Games

Steam runs from Flathub or from Gentoo, with Proton, and your games show up in the launcher with their own icons and on the desktop. NVIDIA cards run on NVIDIA's own driver and AMD and Intel on Mesa, and Chrome's video decoding uses the GPU on all three. ChromeOS normally won't let a Linux app lock the mouse pointer, which breaks mouse look in pretty much every game, so NickelOS lets it. Controllers work.

## What we kept from ChromeOS

We didn't want to lose what ChromeOS gets right. Verified boot, the read-only system on two partitions and encrypted homes are all straight from ChromiumOS. Updates come from NickelOS's own server, signed with our key, and install in the background into the spare copy of the system. If a new version ever won't boot, the computer goes back to the one before by itself. It works with Secure Boot on, too. You enroll NickelOS's key once, the first time you start from the USB stick. The shelf, the launcher, Settings and Files work the way they do on a Chromebook, in every language ChromeOS has, NickelOS's own screens included.

We added some things ChromeOS never had, too. There are icons on the desktop, with previews of your photos and videos, and a screen saver of your own pictures that can show the weather. Files also sees the computer's other drives, Windows partitions included.

## Getting it

NickelOS comes as one ISO for any 64-bit PC with an Intel or AMD processor, UEFI, and Intel, AMD or NVIDIA graphics. Write it to a USB stick with Rufus, Etcher, Ventoy or dd, start the PC from it, and install NickelOS, or try it from the stick first. It's on the [download page](/download/), and [Installing NickelOS](/help/install/) goes through it step by step.

## Known issues

- There's no recovery for a forgotten password yet.
- On most PCs NickelOS doesn't use the TPM yet. Your home folder is still encrypted with your password.
- Laptops with two GPUs (Intel or AMD with NVIDIA) may not work yet. Getting them right is next on our list.
- NVIDIA cards older than the GeForce GTX 16 series run on nouveau, which is slow for games.

## What's next

Next up is Windows apps from Settings: one switch that installs Wine (its newest development release) with Mono, Gecko, DXVK and VKD3D-Proton, so `.exe` files open and Windows games run, the way Android is set up today.

NickelOS is a non-commercial community project, and it's open source from the first release. The source is on GitHub, at [github.com/dresden196/nickelos](https://github.com/dresden196/nickelos).

## Where to talk

The [NickelOS forum](https://forum.nickelos.dev/) is open for questions, hardware reports and ideas, and a Discord server is on its way. [Getting help](/help/community/) has what a useful bug report includes.

Thanks for trying it!
