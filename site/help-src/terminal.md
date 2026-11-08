title: The Terminal and the system
nav: Terminal and system
order: 40
summary: The NickelOS Terminal is a real shell on the device. sudo and polkit, the session environment, what runs when you sign in, upstart jobs, elogind, and adding your own boot scripts and services.
---
# The Terminal and the system

The Terminal on the shelf is ChromeOS's Terminal app. On NickelOS, though, it opens a shell on the device itself, as you, not a shell in a container or a VM.

## Your shell

The Terminal opens a login shell (`bash`) in your home folder, with the same environment your apps get. Administrators have `sudo`, and it asks for your password.

Chrome runs inside a tight sandbox (`no_new_privs` plus Landlock), and anything Chrome starts itself inherits it, so a shell Chrome opened directly couldn't use `sudo` or `mount`. So the Terminal doesn't start your shell itself. It hands its terminal over to **nickel-appd**, which runs in your session outside Chrome's sandbox, and nickel-appd starts the shell on it (`nickel-terminal-shell` is the go-between). That's why `sudo`, `mount` and `pkexec` work there.

A few things work differently from other distributions:

- **`/tmp` doesn't allow programs to run from it** (it's mounted `noexec`, as on all of ChromeOS). Build and run things in your home or in `/usr/local/tmp`, which do.
- **The system's own files are read-only**, but `/usr` and `/etc` aren't only the system's: Gentoo's files appear there too, and `sudo emerge` installs into them, as on any Gentoo (see [Gentoo and Portage](/help/gentoo/)). Portage won't let another package overwrite a system file.
- **nano and vim are installed.** nano is Gentoo's default `EDITOR`. Apps that run in a terminal, like vim or htop, open in the Terminal when you start them from the launcher or open a file with them.
- **The init system is upstart**, ChromeOS's, not systemd. `initctl list` shows every job, and `sudo start`, `sudo stop` and `sudo restart` run them. Services you install from Gentoo run under OpenRC, as on Gentoo: `sudo rc-update add sshd default`.
- **Password prompts from programs** (`pkexec`, a disk writer, removing a system app) come from polkit, through ChromeOS's own password prompt, the one with your account picture. elogind tells polkit which session is yours and active, the same way logind does on systemd distributions.

## The session environment

When you sign in, NickelOS writes your session's environment to `/run/nickel/session/env`. Apps started from the launcher get it, the Terminal gets it, and other login shells read it through `/etc/profile.d/nickel-session.sh`.

| Variable | Value | Why |
|---|---|---|
| `HOME` | `/home/<you>` | Your home, which is also Home in Files (see [Your files](/help/files/)) |
| `PATH` | `~/.local/bin`, `/usr/bin`, `/usr/sbin`, `/bin`, `/sbin`, `/usr/local/bin`, `/usr/local/sbin` | Your own commands first. The system and Gentoo share one set of commands |
| `XDG_RUNTIME_DIR` | `/run/user/<uid>` | Sockets: Wayland, PipeWire, D-Bus |
| `WAYLAND_DISPLAY` | `wayland-0` | ChromeOS's Wayland server (Exo) |
| `DISPLAY` | `:0` | Xwayland, for X11 apps |
| `XAUTHORITY` | `/run/user/<uid>/Xauthority` | A fresh X11 cookie every sign-in |
| `DBUS_SESSION_BUS_ADDRESS` | `unix:path=/run/user/<uid>/bus` | Your session bus |
| `XDG_CURRENT_DESKTOP` | `NickelOS` | So apps and autostart rules know where they are |
| `XDG_SESSION_TYPE` | `wayland` | |
| `XDG_SEAT`, `XDG_SESSION_ID` | `seat0`, your elogind session | |
| `XDG_DATA_DIRS` | Your Flatpak exports, the system's Flatpak exports, `/usr/share/nickel`, `/usr/share`, `/usr/local/share` | Where apps, icons and MIME types are found (Gentoo's are in `/usr/share`) |
| `GTK_USE_PORTAL`, `GDK_DEBUG` | `1`, `portals` | Native GTK and Electron apps use the desktop portals too, so they get ChromeOS's file picker |
| `XMODIFIERS` | `@im=nickel` | X11 apps' input method server (see [Typing and keyboard](/help/input/)) |
| `__EGL_VENDOR_LIBRARY_FILENAMES` | Gentoo's Mesa (NVIDIA's first on NVIDIA) | Which OpenGL/EGL driver native apps load: the same Mesa Chrome uses (see [Graphics](/help/graphics/)) |

## What runs when you sign in

Signing in starts a set of upstart jobs that run as you, for as long as you're signed in:

| Job | What it runs |
|---|---|
| `nickel-session` | Sets up your runtime directory, mounts your home (Home in Files) at `/home/<you>`, writes the environment, then runs your session's D-Bus bus |
| `nickel-logind-session` | Registers your session with elogind |
| `nickel-appd` | nickel-appd: apps in the launcher, starting apps, the Terminal's shell, tray icons, autostart, Steam's games, previews |
| `nickel-x11` | sommelier and Xwayland, display `:0` |
| `nickel-pipewire` | PipeWire: sound, cameras, screen sharing |
| `nickel-wireplumber` | WirePlumber, PipeWire's session manager |
| `nickel-audio` | `pipewire-pulse`, the PulseAudio server apps talk to |
| `nickel-xim` | The XIM server that gives X11 apps ChromeOS's input methods |
| `nickel-x11-keys` | Shortcut keys for X11 apps in the background |
| `nickel-xembed-tray` | Old-style X11 tray icons |
| `nickel-polkit-agent` | Hands polkit's password prompts to Chrome, which shows its own |
| `nickel-phone` | Valent (KDE Connect) |
| `nickel-cloud` | Cloud storage through rclone |

The desktop portals, gnome-keyring and the like aren't jobs. Your session bus starts them when an app first asks for them.

These run for the whole device, signed in or not:

| Job | What it does |
|---|---|
| `nickel-accountsd` | Local accounts: makes them, changes passwords, administrators |
| `nickel-flatpak-storage` | Puts `/var/lib/flatpak` on the data partition |
| `nickel-flatpak-defaults` | Installs the first-boot apps |
| `nickel-usr-local` | Mounts `/usr/local` (the data partition's, allowed to run programs) |
| `nickel-gentoo-mount`, `nickel-gentoo` | Puts your Gentoo layer over `/usr` and `/etc` early at boot (after an OS update, it brings Portage's records up to date first); then Gentoo's accounts and the 32-bit NVIDIA libraries |
| `nickel-gentoo-setup` | Fetches Gentoo's package tree and the compiler at the first startup, once online; after an OS update, rebuilds what the update left waiting |
| `nickel-openrc` | Your services from Gentoo, under OpenRC |
| `nickel-local-services`, `nickel-local-service` | Your own boot scripts and services (below) |

If the launcher stops picking up new apps, `sudo restart nickel-appd` restarts nickel-appd without signing out. Be careful with the others: restarting `nickel-x11` closes every X11 app, and restarting `nickel-session` ends everything in the session.

## Your own boot scripts and services

The system partition is replaced by every update, so services you add live on the data partition instead:

| Path | What happens |
|---|---|
| `/usr/local/etc/nickel/boot.d/NAME` | Runs once at every boot, as root, in name order |
| `/usr/local/etc/nickel/services/NAME/run` | Kept running as root: started at boot, restarted if it exits |

They must belong to root, and so must every folder above them up to `/usr/local`, with nobody else able to write to them. Anything else is skipped, because otherwise any user could make themselves root this way.

For example, a service that keeps a program running:

```
sudo mkdir -p /usr/local/etc/nickel/services/myservice
sudo tee /usr/local/etc/nickel/services/myservice/run >/dev/null <<'EOF'
#!/bin/sh
exec /usr/local/myservice/bin/myserviced --foreground
EOF
sudo chmod 755 /usr/local/etc/nickel/services/myservice/run
sudo start nickel-local-service NAME=myservice
```

`run` should stay in the foreground (`exec` the program, don't let it fork into the background). Once it's there, it starts by itself at every boot.

## Where things live

| Path | What |
|---|---|
| `/home/<you>` | Your home (Home in Files) |
| `/home/chronos/user` | The top of your encrypted vault, where Chrome keeps its profile. Apps don't get it as their home |
| `/run/user/<uid>` | Your runtime directory |
| `/run/nickel/session/env` | The session environment |
| `/run/chrome/wayland-0` | ChromeOS's Wayland socket (linked into your runtime directory) |
| `/mnt/stateful_partition` | The data partition |
| `/usr/local` | The data partition's own software area: what you build yourself (`make install`) and your services |
| `/mnt/stateful_partition/nickel-gentoo` | Your Gentoo layer, which appears over `/usr` and `/etc` |
| `/var/lib/flatpak` | Flatpak apps for everyone (really `/mnt/stateful_partition/nickel/flatpak`) |
| `/media/removable` | USB drives and SD cards |
| `/etc/nickel` | NickelOS's own settings in the image |
| `/var/log/messages` | The system log, NickelOS's services included |
