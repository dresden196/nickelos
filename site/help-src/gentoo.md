title: Gentoo and Portage
nav: Gentoo and Portage
order: 30
summary: NickelOS is Gentoo underneath. How the system is built with Portage, Gentoo at / with emerge and eix, your packages and the system's, USE flags, 32-bit (i386) programs, Steam, and NVIDIA.
---
# Gentoo and Portage

ChromiumOS has always been Gentoo underneath. Google builds it with Portage, Gentoo's package manager, from Gentoo ebuilds, and most of what's in it started out in Gentoo's tree. ChromeOS hides that. NickelOS doesn't: it's built the same way, and on your device it is Gentoo, with `emerge` installing into `/usr` like on any Gentoo.

New to Gentoo? [Learning Gentoo](/help/learning-gentoo/) walks through `emerge` from the start. This page is how Gentoo is set up on NickelOS.

## How NickelOS is built

NickelOS is a ChromiumOS **board**. A board is an overlay of ebuilds and profiles on top of ChromiumOS's own tree. Ours is called `nickel`. It inherits `reven`, the public board ChromeOS Flex is built from, which is what makes it run on ordinary PCs instead of Chromebooks. Everything NickelOS adds or changes lives in that one overlay:

- our own packages (`chromeos-base/nickel-*`) and the Linux desktop stack ChromiumOS doesn't carry (Flatpak, PipeWire, Xwayland, GTK 4, elogind, polkit, gnome-keyring, GeoClue and more), mostly ported straight from Gentoo's tree;
- patches to Google's packages, kept as plain patch files and applied when each package builds (see [Under the hood](/help/under-the-hood/#patches));
- the board's profile: USE flags, firmware, kernel options.

Portage builds all of it into one disk image. That image is what you install and what updates replace.

## One system

The system image is one current Gentoo: Gentoo's libraries, ChromeOS's programs (Chrome, sign-in, Wi-Fi, Settings, Files, Android) and NickelOS's packages, all recorded in Portage's database. There's one glibc, one `/usr/lib64` and one Python, at the usual places. Programs built elsewhere that expect a normal Linux system (AppImages, Steam, `-bin` packages) find one.

What you `emerge` goes into your own layer on the data partition, over `/usr`, `/etc` and `/var/db/pkg`. Nothing is in a container: a Gentoo app shows up in the launcher, plays sound, gets your input method and opens files like any other.

## Where Gentoo lives

| Part | What's on it | Writable? |
|---|---|---|
| System partition A, and its twin B | The OS. One of them runs, the other takes the next update | Read-only |
| Data partition (`/mnt/stateful_partition`) | Everyone's encrypted homes, Flatpak apps, what you install with `emerge`, the device's own settings | Yes |

What you install is on the data partition, and it appears in `/usr` and `/etc` on top of the system's (overlay mounts made at startup). So the system partition stays read-only and checked, and an OS update replaces only the system: what you installed with `emerge` is still there afterwards.

## Setting it up

There's nothing to do. Gentoo itself is part of the system image. Once you've finished setting up the computer and it's online, NickelOS fetches Gentoo's package tree (the repository `emerge` reads) and the compiler, in the background and at low priority so it doesn't slow you down. `nickel-gentoo status` tells you whether that's done and how big your own layer has grown. If you want to start it yourself (or watch it), run `sudo nickel-gentoo setup`. If the setup at startup is already running, it waits for that one.

## Finding and installing packages

As on any Gentoo:

```
eix gimp                          # search the tree
equery uses media-gfx/gimp        # its USE flags (gentoolkit)
sudo emerge --ask media-gfx/gimp  # install
sudo emerge --sync                # update the tree (eix's index updates with it)
sudo emerge --ask -uDN @world     # update everything you've installed
sudo emerge --ask --depclean      # remove what nothing needs anymore
```

Searching and pretending (`eix`, `emerge -s`, `emerge -pv`, `equery`) need no password. `man emerge`, `man make.conf` and tab completion of emerge's options and package names work in the Terminal.

Apps you install show up in the launcher right away, and you can remove them from there (it asks for an administrator's password).

### Binary packages

`emerge` downloads ready-built packages when it can, so most installs don't compile anything. It looks at Gentoo's own binary packages first (signed by Gentoo, and checked), then NickelOS's, which carry what Gentoo's don't (the 32-bit builds Steam needs, for one). Anything neither has, or anything you've set different USE flags for, compiles on your machine, using all your cores.

### Your packages and the system's

Portage knows every file of the system's packages, so its collision checks keep another package from overwriting one.

- **Rebuilding a system package.** You can rebuild any package with your own USE flags, the system's included (Steam needs that for its 32-bit libraries). Your build goes in your layer, over the system's copy, which stays underneath. `sudo nickel-gentoo restore <category/package>` puts the system's version back.
- **Updates.** `sudo emerge -uDN @world` updates what you installed and leaves the system's packages at the system's versions. Those come with NickelOS updates.
- **A newer system package.** If a package you install needs a newer version of a system package, `emerge` takes it in the same run. To take a newer one yourself, add it to `/etc/portage/package.unmask`. Either way it's yours, in your layer, until an OS update catches up.

## Repositories and profile

| Repository | What it is |
|---|---|
| `gentoo` | Gentoo's main tree |
| `guru` | GURU, Gentoo's user repository. Its packages are accepted with their testing keyword (`~amd64`) |
| `steam-overlay` | Steam for Gentoo (`games-util/steam-launcher`), the overlay Gentoo's wiki points to. Its testing keyword is accepted too |
| `nickel` | NickelOS's own, holding the profile |

All four are on from the start. GURU and steam-overlay arrive with Gentoo's tree at the first startup, and `sudo emerge --sync` keeps them current. Other repositories are a command away with `eselect repository`, which replaced layman (layman is gone from Gentoo):

```
eselect repository list
sudo eselect repository enable <name>
sudo emerge --sync <name>
```

`sudo eselect repository disable guru` turns one off again.

The profile is `nickel/amd64/desktop`: Gentoo's 23.0 desktop profile (OpenRC, merged `/usr`), plus what NickelOS needs. Keep it. Its one job is to fit packages to NickelOS: the system runs the services itself (udev, D-Bus's system bus, polkit's rules, login), so packages don't install their own copies of those. `eselect profile list` shows all of Gentoo's profiles too, but switching to one of those drops that.

## Changing USE flags, keywords and licenses

The configuration is in `/etc/portage/`, as on Gentoo. `make.conf` is a folder here: `0000_gentoo.conf` and `0100_nickelos.conf` are kept up to date by the OS (changes to those are overwritten), and your own settings go in files of your own next to them, like `make.conf/50-mine.conf`. `package.use/`, `package.accept_keywords/`, `package.license/` and `repos.conf/` are all yours.

Some of it emerge does by itself. When a package needs another one built with a USE flag, or a newer version of one of the system's packages, emerge adds that to `/etc/portage` and carries on. Testing keywords and licenses are left to you: emerge stops and names the ones a package needs. Add those lines yourself ([Learning Gentoo](/help/learning-gentoo/#changing-how-a-package-is-built) shows them), or let emerge write them all down and stop, so you can look them over:

```
sudo emerge --ask --autounmask-write --autounmask-keep-keywords=n --autounmask-license=y --autounmask-continue=n some/package
sudo dispatch-conf
sudo emerge --ask some/package
```

What emerge adds goes in the `zz-autounmask` file of each of those folders, which is yours like any file you add there.

Settings for one command work too: `USE="-X" emerge -pv media-video/vlc`, `sudo MAKEOPTS=-j4 emerge ...`.

## 32-bit programs (i386, multilib)

Gentoo here is multilib, like any Gentoo amd64 install: 32-bit glibc and GCC are there, and everything else 32-bit comes the Gentoo way, through the `abi_x86_32` USE flag on the packages that need it. Programs built elsewhere find the usual loaders, `/lib/ld-linux.so.2` and `/lib64/ld-linux-x86-64.so.2`.

### Steam from Gentoo

Steam from Flathub is the easy way (see [Steam and games](/help/games/)). If you'd rather have Gentoo's:

```
sudo emerge --ask --autounmask-write --autounmask-keep-keywords=n --autounmask-license=y --autounmask-continue=n games-util/steam-launcher
sudo dispatch-conf
sudo emerge --ask games-util/steam-launcher
```

The first command works out what Steam needs and writes it down without installing anything: `abi_x86_32` for the libraries it uses (Mesa, X11 and what they pull in), testing keywords for two of its helpers, and Valve's license (`ValveSteamLicense`), which is yours to accept. `dispatch-conf` shows each change and asks. NickelOS's server has the 32-bit libraries built, so they mostly download.

### Wine

Gentoo's Wine (`app-emulation/wine-vanilla`) runs 32-bit Windows programs through WoW64, so it doesn't need 32-bit libraries on the Linux side. Flathub's Wine works too.

## NVIDIA

Mesa finds Intel and AMD GPUs by itself. NVIDIA's driver is different: its libraries have to match the kernel module exactly, and they come with the system, in `/usr/lib64`, where Gentoo's programs find them.

For 32-bit programs, `nickel-gentoo gpu` downloads NVIDIA's 32-bit libraries for your exact driver version into `/usr/lib/nickel-nvidia` at startup. When an OS update brings a new driver, the new ones download.

Don't emerge `x11-drivers/nvidia-drivers`. The driver is the system's job.

## Accounts

Gentoo's packages make system users and groups (`portage` and the like) as on Gentoo. They're kept in `/var/lib/nickel-gentoo/accounts`, and NickelOS adds them to the device's accounts at every startup, beside the system's and yours.

## Paths

| Path | What |
|---|---|
| `/usr`, `/etc` | The system's files, with yours on top |
| `/mnt/stateful_partition/nickel-gentoo` | Your layer: what you've installed or rebuilt |
| `/etc/portage` | Portage's configuration |
| `/var/db/repos` | The repositories |
| `/var/db/pkg` | What's installed |
| `/etc/nickel/binhost` | The address of NickelOS's package server |
| `/var/log/nickel-gentoo-setup.log` | What the setup at startup did |
