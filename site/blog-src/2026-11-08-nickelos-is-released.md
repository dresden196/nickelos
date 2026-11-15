title: NickelOS is released
slug: nickelos-is-released
date: release
release: yes
summary: The first public release of NickelOS, ChromeOS's desktop with the Linux underneath it opened up. What's in it, how to get it, and what comes next.
---
# NickelOS is released

NickelOS is out. NickelOS {{NAME}} is the first release anyone can download and install.

NickelOS is ChromiumOS with the Linux underneath it opened up. You get the ChromeOS desktop you know from Chromebooks, and Linux apps run right on it, as your own Linux user next to the browser. There's no Crostini and no virtual machine. Underneath, the whole system is a current Gentoo.

## What's in it

- **Linux apps that just work.** Bazaar and Flathub, AppImages, and all of Gentoo with `emerge`. What you install shows up in the launcher and on the shelf, plays sound through ChromeOS, and opens files like any other app.
- **Steam and games.** Steam from Flathub or from Gentoo, Proton, controllers, and your games in the launcher with their own icons. NVIDIA cards run on NVIDIA's own driver, Intel and AMD on Mesa.
- **Android apps** through Waydroid, set up from Settings.
- **No Google account.** Setup makes a local account that's a real Linux user, with an encrypted home and `sudo`. Setup works offline.
- **De-Googled from the start.** Setup asks how you'd like to start: the Full experience (DuckDuckGo, uBlock Origin, Proton's apps and a set of free apps) or Minimal.
- **A desktop.** Files and folders on the wallpaper, and launchers that start their apps. Steam puts the games you install there.
- **Updates from NickelOS's own server**, signed with NickelOS's key and installed in the background into a spare copy of the system, with a fallback if one ever doesn't boot.
- **Secure Boot**, with NickelOS's own key, which you enroll once.
- **Every language ChromeOS has**, NickelOS's own screens included.

[What changes](/#what) on the front page has the longer list, and [the guide](/help/) explains how it all fits together.

## Getting it

NickelOS comes as one ISO for any 64-bit Intel or AMD PC with UEFI. Write it to a USB stick with Rufus, Etcher, Ventoy or dd, start the PC from it, and install NickelOS, or try it from the stick first. It's on the [download page](/download/), and [Installing NickelOS](/help/install/) goes through it step by step, Secure Boot's one-time key enrollment included.

## Known issues

- There's no recovery for a forgotten password yet.
- NickelOS doesn't use the PC's TPM yet.
- Laptops with two GPUs (Intel or AMD with NVIDIA) haven't been tested yet.
- NVIDIA cards older than the GeForce GTX 16 series run on nouveau, which is slow for games.

## What's next

- **Windows apps from Settings.** One switch that installs Wine (its newest development release) with Mono, Gecko, DXVK and VKD3D-Proton, so `.exe` files open and Windows games run, the way Android is set up today.
- **Laptops with two GPUs**, Intel or AMD together with NVIDIA.
- **Each new ChromiumOS release**, as Google ships them, through the update server.

NickelOS's source is on GitHub, at [github.com/dresden196/nickelos](https://github.com/dresden196/nickelos).

## Where to talk

The [NickelOS forum](https://forum.nickelos.dev/) is open for questions, hardware reports and ideas, and a Discord server is on its way. [Getting help](/help/community/) has what a useful bug report includes.

Thanks for trying it.

Dresden
