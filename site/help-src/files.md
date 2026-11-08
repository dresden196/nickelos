title: Your files
order: 80
summary: Where your files live on NickelOS. One home folder shared by Files and Linux apps, the XDG folders, one Trash, the desktop, USB drives, other drives and Windows partitions, network shares, cloud storage through rclone, and picture previews.
---
# Your files

Where your files are, how Files and Linux apps share them, and how to reach USB drives, other disks, network shares and cloud storage.

## One home

On NickelOS, **Home** in the Files app and your Linux home folder are the same folder. Files, the file picker, the Terminal and every Linux app see the same files.

Here's how. ChromeOS keeps each account's files in an encrypted vault, and the Files app shows the vault's `MyFiles` folder. When you sign in, NickelOS mounts that folder at `/home/<username>` and makes it your `HOME`. The top of the vault, where Chrome keeps its own profile (history, cookies and so on), stays out of it. `ls ~` shows your files, not Chrome's internals.

| Folder | Path |
|---|---|
| Home | `/home/<you>` |
| Desktop | `~/Desktop` (what's on the wallpaper) |
| Documents | `~/Documents` |
| Downloads | `~/Downloads` (where the browser saves) |
| Music | `~/Music` |
| Pictures | `~/Pictures` (screenshots in `~/Pictures/Screenshots`) |
| Videos | `~/Videos` |
| Trash | `~/.local/share/Trash` |
| Apps' settings | `~/.config`, `~/.local/share`, `~/.cache` |
| Flatpak apps' data | `~/.var/app/<app id>` |

These are the standard XDG folders, so apps find them on their own. Files gives them their own icons in its sidebar. Dotfiles are there too, hidden in Files until you turn on **Show hidden files** in its menu. Search in the launcher and in Files skips hidden folders, so app caches don't fill up your results.

When files pass between Chrome and a Linux app (opening a file with an app, the file picker, drag and drop), NickelOS translates the paths both ways. An app always gets a path it can open.

## One Trash

Files and Linux apps share one Trash: `~/.local/share/Trash`, the standard freedesktop.org home trash. Delete something in a Linux app and it's in Files' Trash, and what you delete in Files is in the apps' Trash.

## The desktop

The files and folders in `~/Desktop` are the icons on your wallpaper. Right-click the wallpaper for **New folder**. Right-click an icon to **Open** it, **Show in Files**, **Rename** it or **Move to trash**. Drag icons to arrange them, and they stay put. Files you drop on the desktop from the Files app are moved into `~/Desktop`.

## Moving files between Files and apps

- **Drag and drop** works both ways. Drag from Files into an app and it gets the file. Drag a file out of an app into Files and it's copied there.
- **Copy and paste** files works both ways too.
- An app in a sandbox (a Flatpak) can only hand over files it can see itself. NickelOS checks that against the app's own view of the file system, so a sandboxed app can't sneak out a file it was never given.

## The file picker

When a Linux app opens or saves a file, you get ChromeOS's own file picker, with Home, your drives and your cloud storage, the same one the browser uses. That works for Flatpak apps and native apps both.

## USB drives and SD cards

Plug one in and it's mounted at `/media/removable/<name>`, owned by you, so you can write to it from Files, the Terminal or any app.

## Other drives in this computer {#other-drives}

If your computer has other drives or partitions (a second disk, or Windows on the same disk), they're in **Settings > System preferences > Storage and power > Storage management > Other drives**:

- **Show in Files** puts a drive in Files (and mounts it). Turn it off and it isn't mounted at all, now or at your next sign-in.
- The drive with **Windows** on it opens **read-only**, so Windows stays intact. **Allow changes** makes it writable after a warning. Windows' Fast Startup doesn't really shut Windows down, and changes made while it's half-hibernated can be lost or damage files when Windows starts again. If Windows wasn't fully shut down, the drive stays read-only and Settings tells you so.
- Other operating systems' own partitions (Microsoft reserved, Windows recovery, EFI, BIOS boot, swap) aren't shown at all.
- NTFS drives use ntfs-3g.

## Network shares

Windows and Samba shares (SMB) use ChromeOS's own client. Add one in **Settings > System preferences > Files > Network file shares**, and it shows up in Files and in the file picker.

## Cloud storage

**Settings > System preferences > Files > Cloud storage > Add account** connects Google Drive, OneDrive, Dropbox, Proton Drive, Box, pCloud, Nextcloud or any WebDAV server, and Amazon S3 or any S3-compatible storage. Each account shows up in Files and in apps' file pickers.

It's built on **rclone**:

- rclone isn't part of the image. The first account you add downloads it from rclone.org (checked against its SHA-256) into `~/.local/share/nickel-cloud/`.
- Your accounts are rclone remotes in `~/.config/rclone/rclone.conf`, so any rclone you run yourself sees them too. NickelOS's copy is in `~/.local/share/nickel-cloud/rclone-<version>/rclone`.
- rclone's log for the session is `$XDG_RUNTIME_DIR/nickel-cloud/rclone.log`.
- Each account is mounted at `$XDG_RUNTIME_DIR/nickel-cloud/drives/<name>`, with rclone's full file cache: files open like local ones, and changes upload in the background.
- Services that sign in through the browser open their sign-in page in a browser tab, and Settings waits until you're done.
- **Google Drive** needs your own client ID and secret from Google's Cloud console. Settings links to how to make them.
- OneDrive's Personal Vault is left out, because only Microsoft's own apps can open it.

Removing an account takes it out of Files and leaves your files in the cloud. If uploads are still going, it waits for them to finish first.

## Picture previews

Files and the desktop show previews of pictures and videos. For what Chrome can't decode itself, like HEIC photos from phones and H.265 videos, NickelOS makes them with the standard Linux thumbnailers (libheif and ffmpegthumbnailer). Those run inside a sandbox with no network and read-only access to the file. Previews are kept in `~/.cache/thumbnails`, the same cache Linux file managers use.

## Default apps

Which app opens which kind of file is in **Settings > Apps > Default apps** (see [Apps](/help/apps/#default-apps-links-and-open-with)).
