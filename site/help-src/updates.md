title: Updates
order: 100
summary: How NickelOS updates. The Stable, Beta and Dev channels, A/B system partitions, signed updates from NickelOS's own server, falling back when an update won't boot, and what an update keeps.
---
# Updates

NickelOS updates the way ChromeOS does: in the background, into a spare copy of the system, and you restart when it suits you.

## How an update installs

Your disk has two system partitions, A and B. You run on one, and the other one waits.

1. **update_engine**, ChromeOS's updater, asks NickelOS's update server whether there's a newer build for your channel. It checks about 7 minutes after you start the device, then about every 45 minutes.
2. If there is, it downloads the new system into the spare partition, while you keep working.
3. Before using it, it checks the update's signature against NickelOS's key, which is part of your current system. An update that isn't signed with it is refused.
4. A notification asks you to restart. On restart, the device boots the new partition, and the old one becomes the spare.

Everything comes from NickelOS's own server, `update.nickelos.dev`, over HTTPS. Google's update servers aren't involved.

**Settings > About NickelOS** shows the version you're on and has **Check for updates**, so you don't have to wait for the next check.

## Channels

| Channel | What it gets |
|---|---|
| **Stable** | Builds that have been on Beta for a few weeks |
| **Beta** | Each new ChromiumOS release, once NickelOS has moved onto it |
| **Dev** | Nightly builds, for testing |

All three also get NickelOS's own fixes, and Google's security fixes for the current ChromiumOS release, as soon as they're ready.

To switch, go to **Settings > About NickelOS > Additional details > Channel > Change channel**. Only the device's owner, the first account, can do that.

- **Moving to a less stable channel** (Stable to Beta) keeps everything, and takes that channel's next newer build.
- **Moving to a more stable channel** (Beta to Stable) means going back to an older build. That can only be done by wiping the device (a powerwash), and the dialog says so before it happens.

## If an update doesn't boot

A new system gets a few tries. If it fails to boot every time, the boot loader falls back to the system you had before, by itself. If an update boots but something about it is wrong, you can go back yourself: hold **Shift** while the computer starts to get the boot menu, and pick the other system.

## What an update keeps

An update only replaces the system partition. Everything on the data partition stays:

| What | After an update |
|---|---|
| Accounts and everything in their homes | Untouched |
| Flatpak apps | Untouched (Flatpak updates them, not the OS) |
| Gentoo and what you installed with `emerge` | Untouched. If the update brings a system package you rebuilt, at the same version or newer, your copy is cleared, and rebuilt with your USE flags at the next startup if they differ. Update what you installed yourself with `sudo emerge -uDN @world` |
| Your own builds and services in `/usr/local` | Untouched |
| 32-bit NVIDIA libraries for Gentoo | Download again at the next startup to match the new driver |

## Version numbers

NickelOS uses ChromiumOS's version numbers, since update_engine compares them. The last number is ours: it counts NickelOS's own builds on the same ChromiumOS release, so a NickelOS fix is always a newer version than the build before it.
