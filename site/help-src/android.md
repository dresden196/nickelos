title: Android apps
order: 25
summary: Android apps on NickelOS through Waydroid. Setting it up, installing APKs, ARM apps and the translators, how Android draws on Intel, AMD and NVIDIA, what runs underneath, the waydroid command, and what to do when an app won't install.
---
# Android apps

NickelOS runs Android apps with **Waydroid**: a full Android system in a container on the device, sharing the Linux kernel with everything else. It isn't an emulator or a virtual machine. Each Android app opens in a window of its own, with its own shelf icon, next to your Linux apps.

## Setting it up

Android isn't on the system image. It's about 1 GB to download, so it waits until you want it. Go to **Settings > Apps > Android apps** and choose **Set up**. A progress bar shows the download, and **Cancel** stops it and cleans up. When it's done, Android takes about 2.5 GB on the device.

What gets downloaded is Waydroid's own Android image: LineageOS 20 (Android 13) without Google's apps, from the Waydroid project's servers. It's stored once, in `/var/lib/waydroid`, for everyone on the device. Only an administrator can set it up or remove it. Everyone else sees the same page with its controls turned off.

If you'd rather use the Terminal, this does the same thing:

```
sudo waydroid init
sudo start waydroid-container
```

## Your Android

Each person on the device gets their own Android: their own apps, accounts and data, in `~/.local/share/waydroid`. Android starts the first time you open an Android app or an APK, which takes about 15 seconds, and then keeps running while you're signed in. On Intel and AMD, Waydroid freezes it while it's idle, so it uses no CPU.

The apps you install appear in the launcher, and they stay there. Waydroid writes a desktop file for each one into `~/.local/share/applications` (named `waydroid.<package>.desktop`), and keeps them in step as apps come and go. Android's own apps (Calculator, Clock, Files, Gallery, Settings and the rest) are kept out of the launcher, so they don't sit beside NickelOS's own Files and Settings. They're on Android's home screen, which the **Waydroid** entry in the launcher opens in a window.

**Android settings**, on the same Settings page, opens Android's own Settings app. That's where you manage Android's apps, their permissions and their storage. Android uses ChromeOS's time zone, and follows it when it changes.

## Installing apps

Open an APK file from Files (or any app that opens files) and NickelOS installs it. A notification says when the app is installed, with **Open**. If an app can't be installed, the notification says why. If Android wasn't running, it starts first.

Installing a newer version of an app works the same way: open its APK, and the notification says the app was updated.

For an app store, [F-Droid](https://f-droid.org) works well: download its APK from their site, open it, and install apps from there.

**Uninstalling.** Right-click the app in the launcher or on the shelf and choose **Uninstall**, as with any app. It goes through Android's own package manager, in your Android (which starts first if it isn't running). Uninstalling from **Android settings**, under Apps, works too. Either way, the app's launcher entry goes away by itself.

## ARM apps

Phones use ARM processors, and many Android apps, games especially, are built only for ARM. Your computer's processor runs x86 code. To run ARM-only apps, Android needs a translator, which it loads as its "native bridge".

NickelOS doesn't make or include a translator. **ARM app support**, on the Android apps page in Settings, downloads one for you, after you agree. It's proprietary software, under its owner's terms, and you're the one installing it. Which translator you get depends on your processor:

| Your processor | The translator | Download |
|---|---|---|
| Intel (`vendor_id` is `GenuineIntel` in `/proc/cpuinfo`) | Intel's Houdini, `libhoudini` | about 75 MB |
| AMD, or anything else | Google's NDK translation, `libndk_translation` | about 18 MB |

Both come from GitHub, at the exact commits the Waydroid community's [waydroid_script](https://github.com/casualsnek/waydroid_script) installs, and each download is checked against waydroid_script's MD5 checksum and our own SHA-256 before anything is installed. The download is kept in `/mnt/stateful_partition/nickel/waydroid-arm`.

The translator's files go into Waydroid's overlay (`/var/lib/waydroid/overlay/system`), which sits over Android's system image without changing it. Android's properties are set so it uses the translator and lists ARM among the CPU types it runs: `x86_64, x86, arm64-v8a, armeabi-v7a, armeabi`. Android restarts to use it, so open Android apps close.

Turning the switch off takes the translator's files and properties back out, and Android restarts again. ARM-only apps you installed stay installed, but won't run until you turn it back on.

If you open an ARM-only APK while ARM app support is off, the notification says so and has a **Turn on** button that takes you to the switch.

## Graphics

Android draws on your GPU, like your Linux apps do:

- **Intel and AMD:** Android uses its own Mesa, from Waydroid's image, on your GPU's render node (`iris` on Intel, `radeonsi` on AMD).
- **NVIDIA:** NickelOS sets Android up for [waydroid-nvidia](https://github.com/Shiro836/waydroid-nvidia) by itself when Android's container starts on an NVIDIA GPU. Inside Android, Mesa's Venus driver sends Vulkan calls over a socket (`/run/waydroid-venus/venus.sock`) to a render server in your session (the `nickel-waydroid-venus` job), which runs them on NVIDIA's own driver. OpenGL ES runs through ANGLE, on top of that Vulkan. It needs NVIDIA's open kernel modules, so a GeForce GTX 16 or RTX 20 series card or newer (see [Graphics](/help/graphics/#nvidia)).

Each app window reaches the screen as a GPU buffer handed to ChromeOS's Wayland server, the same way a Linux app's does.

## What runs underneath

| Piece | What it does |
|---|---|
| `waydroid-container` (an upstart job, as root) | Waydroid's container manager. Starts at boot once Android is set up. Before it starts, `nickel-waydroid-prepare` mounts the cgroup v2 hierarchy, so Android's network filters stay inside Android, and sets Android's options |
| Waydroid's session (in your session, as you) | Starts and stops your Android, writes the launcher entries, and brings Android's windows, input and notifications to the desktop |
| `nickel-waydroidd` (an upstart job, as root) | The system service behind the Settings page and the APK handler: setting Android up, ARM app support, removing Android, and installing APKs through Android's own package manager |
| `nickel-waydroid-apk` | What opens APK files, and sends the notifications |
| `waydroid0` | The network bridge Android sits behind. Android gets an address in `192.168.240.0/24` and uses ChromeOS's DNS. ChromeOS's network manager knows to leave the bridge alone |

## The waydroid command

| Command | What it does |
|---|---|
| `waydroid status` | Whether your Android is running |
| `waydroid app list` | Your Android's apps and their package names (while Android is running) |
| `waydroid app launch <package>` | Opens an app, and starts Android if it isn't running |
| `waydroid session stop` | Stops your Android |
| `sudo waydroid shell` | A root shell inside Android |
| `sudo waydroid logcat` | Android's log |

## Removing Android

**Settings > Apps > Android apps > Remove Android** (administrators only) deletes Android's image, ARM app support, and your Android apps and data. Other people on the device keep their Android data in their own home folders. They can't open their Android apps until Android is set up again, and their launcher entries for Android apps are removed the next time they sign in.

## When something goes wrong

**An APK won't install.** The notification says why. The usual reasons:

| The notification says | What it means |
|---|---|
| Needs ARM app support | The app has only ARM code. Turn on ARM app support, then open the APK again |
| It needs a newer version of Android | The app wants a newer Android than Android 13 |
| A newer version of this app is already installed | Android doesn't install an older version over a newer one. Uninstall the app first |
| A copy of this app signed by someone else is installed | The installed app came from a different developer or store. Uninstall it first |
| This is only part of an app | A "split" app, which comes as several APK files (often in an `.apks` or `.xapk` file) |
| The file isn't a working Android app | The download is damaged or incomplete, or it isn't an APK |
| Android said: … | Android's own error code, for anything else |

**An app wants Google Play services.** Android here is LineageOS without Google's apps. Apps that rely on Play services to sign in, get notifications or check licenses may not work.

**Android doesn't start, or its windows don't appear.** Check that the container manager is running (`status waydroid-container`), then look at Waydroid's log, `/var/lib/waydroid/waydroid.log`, and the system log: `sudo grep -E 'waydroid|nickel-waydroidd' /var/log/messages | tail -50`. `waydroid session stop`, then opening an Android app again, starts your Android fresh.

**On NVIDIA, a game shows grey or placeholder textures.** It uses ETC2 compressed textures, which waydroid-nvidia doesn't convert for NVIDIA's driver.
