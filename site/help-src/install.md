title: Installing NickelOS
nav: Installing
order: 5
summary: What a PC needs for NickelOS, writing the ISO to a USB stick, Secure Boot and NickelOS's key, starting NickelOS from the stick, and setup's two choices: install it now, or try it first.
---
# Installing NickelOS

NickelOS comes as one ISO file. You write it to a USB stick and start the PC from the stick. NickelOS starts right from it, and its setup either installs NickelOS on the PC's disk or lets you try it first.

## What you need

- **A 64-bit PC with UEFI firmware.** Intel or AMD processor, 4 GB of memory or more (8 GB is better). The old BIOS mode (often called CSM or Legacy) isn't supported.
- **A disk you can give to NickelOS.** Installing erases the whole disk. At least 32 GB, and 64 GB or more for Flatpak apps, games or a lot from Gentoo. Back up anything you want to keep from it first.
- **A USB stick** of 4 GB or more. Writing the ISO erases everything on it (with Ventoy, the ISO is just a file on the stick).
- **Graphics:** Intel and AMD GPUs work with Mesa. NVIDIA GPUs from the GeForce GTX 16 and RTX 20 series onward use NVIDIA's own driver; older NVIDIA cards get nouveau (see [Graphics](/help/graphics/)).

## Writing the stick

Download the ISO from [nickelos.dev/download](/download/), and check it against the SHA-256 there if you like. Then write it with any of these:

| Tool | How |
|---|---|
| **Rufus** (Windows) | Pick the stick and the ISO, then **Start**. ISO mode and DD mode both work. |
| **balenaEtcher** (Windows, macOS, Linux) | **Flash from file**, the stick, **Flash**. |
| **Ventoy** | Copy the ISO onto the Ventoy stick and pick it from Ventoy's menu. |
| **dd** (Linux, macOS) | `sudo dd if=nickelos-<version>.iso of=/dev/sdX bs=4M status=progress conv=fsync`, where `/dev/sdX` is the stick (check with `lsblk`; dd overwrites whatever you point it at). |

## Secure Boot

NickelOS boots with Secure Boot on or off. With it on, NickelOS's boot loader and kernel are signed with NickelOS's own key, and the PC has to be told once to trust that key. Most PCs only trust Microsoft's key and the keys Microsoft signs for. So NickelOS starts through **shim**, the small boot loader Linux distributions use, signed by Microsoft. Shim then asks you to enroll NickelOS's key.

The first time you start from the stick with Secure Boot on:

1. A blue screen says **Verification failed: (0x1A) Security Violation**. Press Enter for **OK**.
2. **Shim UEFI key management** says **Press any key to perform MOK management** and counts down from 10 seconds. Press an arrow key before it runs out. If it does run out, the PC tries again and you're back at step 1.
3. In the menu (Continue boot, Enroll key from disk, Enroll hash from disk), choose **Enroll key from disk**.
4. Pick the USB stick (usually listed by its label, `NICKELEFI` or `NICKELOS`), then the file `nickelos-secureboot.der`.
5. Choose **Continue**, then **Yes** to "Enroll the key(s)?", then **Reboot**.
6. Start from the stick again. This time NickelOS starts.

You only do this once per PC. The key stays enrolled in the firmware, the installed system boots with it, and updates are signed with it.

The key is "NickelOS Secure Boot". Before you enroll it, you can check it's ours: at step 5, **View key 0** shows the key, and its **Fingerprint** at the bottom should match the one on the [download page](/download/).

If the PC won't start the stick at all with Secure Boot on, its firmware may not trust the Microsoft key that signs shim (some PCs ship with only Windows's own key on). Look for an option such as **Allow Microsoft 3rd Party UEFI CA** in the firmware's Secure Boot settings, or turn Secure Boot off.

## Starting from the stick

Plug in the stick and turn on the PC while pressing its boot menu key. That's often **F12**, **F11**, **F10**, **F8** or **Esc**, depending on who made it, and the PC usually shows it on its first screen. Pick the stick, often listed as **UEFI: (the stick's name)**.

A short menu shows for five seconds, then **NickelOS** starts by itself. It runs from the stick: nothing on the PC's disk is touched until you choose to install. Its setup starts with a choice:

- **Install NickelOS**: setup asks which disk, then installs. Everything on that disk is erased. Installing takes a few minutes. Take out the stick when it's done, and the PC restarts into NickelOS's setup from its disk. The disk gets two system partitions (one runs, the other takes the next update; see [Updates](/help/updates/)) and a data partition with the rest of the space.
- **Try it first**: NickelOS's setup goes on as on an installed PC, running from the stick. What you do is kept in memory, so it's gone when the PC turns off. When you want to keep NickelOS, start from the stick again and choose **Install NickelOS**.

The stick itself is never offered as a place to install to.

### The text-mode installer

The menu's second entry, **NickelOS: text-mode installer**, is for a PC whose screen stays black when NickelOS starts. It installs from a plain text menu, which shows on any display the PC's firmware can draw on:

```
  1  Install NickelOS on this computer
  2  Try NickelOS first (makes this USB stick a NickelOS stick)
  3  Turn off
```

**Install** asks for the disk and a typed `yes`. **Try** turns the USB stick itself into a NickelOS stick that keeps what you do (16 GB or more, and only a stick that holds nothing else: not Ventoy, not a disc).

## Setup

Setup asks for a language, a network (or **Set up offline**), how you'd like to start, **Full experience** or **Minimal**, and your account. [Getting around](/help/basics/#setup) says what each step does. No Google account is involved.

After setup, NickelOS fetches Gentoo's package tree in the background. Updates come from NickelOS's server by themselves.

## If something goes wrong

- **The PC skips the stick and starts its old system.** The stick isn't in the boot order, or the PC is set to Legacy/CSM only. Use the boot menu key, and switch the firmware to UEFI.
- **A black screen after choosing the stick.** Wait a minute or two: NickelOS starts once the image is found and read. If it never does, try another USB port (one on the PC itself, not a hub), or write the stick again. If the screen still stays black, start the **text-mode installer** from the menu.
- **The installer finds no disk.** With the firmware's storage mode set to RAID or Intel RST, the disk may not show up. Switch it to AHCI (if Windows is still on that disk, look up how to do that safely first).
- **The installed system won't start with Secure Boot on.** If you reset the firmware's keys since enrolling NickelOS's, enroll it again: start from the stick, as above.

[Getting help](/help/community/) has where to ask.
