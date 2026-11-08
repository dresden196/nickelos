title: Troubleshooting
order: 120
summary: When something goes wrong on NickelOS. Where the logs and crash reports are, and fixes for the usual problems with apps, the launcher, Gentoo, sound, graphics, drives and updates.
---
# Troubleshooting

Start with the logs. Then look for your problem below, roughly grouped by what's acting up.

## Where to look

| File | What's in it |
|---|---|
| `/var/log/messages` | The system log. nickel-appd, the portal backend, the session's jobs, disks, networking, nearly everything |
| `/var/log/chrome/chrome` | Chrome's log: Ash, Exo and Linux app windows. While you're signed in it's yours. `chrome.PREVIOUS` is the run before (the sign-in screen's, or the session before a crash) |
| `/var/log/ui/ui.LATEST` | The session manager: starting and stopping Chrome |
| `/var/log/update_engine.log` | Updates |
| `/var/log/nickel-flatpak-defaults.log` | Installing the first-boot apps |
| `$XDG_RUNTIME_DIR/nickel-cloud/rclone.log` | Cloud storage |

A few of them need `sudo` to read. For example:

```
sudo grep nickel-appd /var/log/messages | tail -50
grep -E 'FATAL|Check failed' /var/log/chrome/chrome
```

**Crash reports.** When a program you ran crashes, its report goes to your encrypted home, under `/run/daemon-store/crash/`. Crashes of the system's own services go to `/var/spool/crash/`. A report is a `.meta` file with a minidump (`.dmp`) and logs beside it. Attach them to a bug report.

## An app isn't in the launcher

1. Check that its desktop file is somewhere nickel-appd looks: `~/.local/share/applications`, the Flatpak export folders, `/usr/share/applications` (the system's apps and Gentoo's) or `/usr/local/share/applications`.
2. Check the desktop file itself. Apps marked `NoDisplay=true` or `Hidden=true` are left out on purpose. If its `Exec` (or `TryExec`) program doesn't exist, it's skipped too: GLib won't list an app it can't start.
3. Look for nickel-appd's complaints: `sudo grep nickel-appd /var/log/messages`.
4. `sudo restart nickel-appd` sends Chrome the list again.

## An app's windows don't land on its shelf icon

ChromeOS matches a window to its app by the window's app id: the Wayland `app_id`, or for X11 apps, `WM_CLASS`. If they don't match the desktop file's name or its `StartupWMClass`, the window gets a shelf item of its own with a plain icon.

To find an X11 window's class, `xprop` from Gentoo works (`sudo emerge x11-apps/xprop`, then `xprop WM_CLASS` and click the window). Then copy the app's desktop file into `~/.local/share/applications` and set `StartupWMClass=` to that class.

## emerge problems

| What you see | What's going on |
|---|---|
| `emerge` finds no packages, or can't build anything | Gentoo's package tree or the compiler isn't there yet. They come at the first startup once you're online: `nickel-gentoo status` says whether they're there, `/var/log/nickel-gentoo-setup.log` what happened, and `sudo nickel-gentoo setup` fetches them (or waits for the one running) |
| The setup stopped partway (a restart, no network) | It tries again at the next startup |
| A 32-bit program says it can't find a library | Turn on `abi_x86_32` for the libraries it needs (`sudo emerge --ask --autounmask --autounmask-write <package>`) |
| emerge says there are no ebuilds for a package | It isn't in any repository that's on. `eix <name>` searches the ones that are on, and `eselect repository list` shows the others Gentoo knows of; `sudo eselect repository enable <name>` and `sudo emerge --sync <name>` add one. layman is gone from Gentoo: `eselect repository` replaced it |
| A package wants a keyword, USE flag or license change | `sudo emerge --ask --autounmask --autounmask-write <package>`, then `sudo dispatch-conf` |
| emerge stops with a file collision | The package wants to install a file one of the system's packages owns. Portage won't overwrite it |
| A system package you rebuilt misbehaves | `sudo nickel-gentoo restore <category/package>` puts the system's version back |

## A program won't run from /tmp

`/tmp` is `noexec` on ChromeOS, and NickelOS keeps it that way. Use your home or `/usr/local/tmp`.

## No sound from a Linux app

1. Check the output device and volume in Quick Settings. That's CRAS, and it decides for everything.
2. Play something in the app and run `wpctl status -n`. The app's stream should be there, and `cras_out` should be the default sink (marked `*`).
3. `pactl info` should show a PulseAudio server "on PipeWire", with `cras_out` as the default sink. If an app talks to PulseAudio and gets nothing, this is where it shows.
4. On an AMD laptop, see [Sound](/help/sound/#amd-laptops).

## Graphics

- `lsmod | grep -E 'i915|xe|amdgpu|radeon|nvidia|nouveau'` shows which kernel driver drives your GPU.
- On NVIDIA, `nvidia-smi` shows the driver and what's running on the GPU. GPUs older than Turing (GeForce GTX 16 / RTX 20) use nouveau instead of NVIDIA's driver.
- A Flatpak app on NVIDIA needs the `org.freedesktop.Platform.GL.nvidia-<version>` extension that matches your driver. Flatpak installs it by itself. `flatpak update` fetches it again after a driver change.
- In the browser, `chrome://gpu` shows what Chrome is using.

## Files and drives

- **The Windows drive is read-only.** That's on purpose (see [Other drives](/help/files/#other-drives)). If **Allow changes** says Windows didn't shut down fully, start Windows and shut it down with Shift held, or turn off Fast Startup in Windows.
- **A Flatpak app can't see a folder.** Flatpak apps only see what their sandbox allows. Opening a file through the file picker always works. To let an app see a folder all the time: `flatpak override --user --filesystem=~/Folder <app id>`.
- **Cloud storage won't connect.** Check `$XDG_RUNTIME_DIR/nickel-cloud/rclone.log`. For Google Drive, the client ID and secret must be your own, from Google's Cloud console.

## The first-boot apps never showed up

They install once the device is online, within half an hour of starting. Otherwise they try again at the next start. `/var/log/nickel-flatpak-defaults.log` says what happened. You can also install any of them from Bazaar or with `flatpak install flathub <app id>`.

## Location

GeoClue asks beaconDB, an open location service. Where beaconDB doesn't know the Wi-Fi networks around you yet, it falls back to your internet address, which is only accurate to a city or so. Check that location is on in **Settings > Privacy controls**.

## An update went wrong

If a new version doesn't boot, NickelOS falls back to the previous one by itself after a few tries. If it boots but something is broken, hold **Shift** while the computer starts, choose the other system in the boot menu, and tell us (see [Getting help](/help/community/)).
