# NickelOS's ISO (2026-10-26)

The release media (docs/vision.md: "Release media is an ISO", so Rufus,
Etcher, Ventoy, Stoke and dd take it with no special steps). One ISO,
three ways to boot it: as a disc (El Torito, EFI), as a stick written as
it is (dd, Etcher, Stoke: GPT with an EFI system partition), and as files
(Rufus's ISO mode copies them onto a FAT stick; Ventoy boots the ISO file).
UEFI only.

What's on it (`build-iso.sh IMAGE OUT.iso [KEY CERT]`, on the build host):
- `EFI/BOOT`: shim (`sys-boot/nickel-shim`, Ubuntu's, Microsoft-signed)
  as `BOOTX64.EFI`, MokManager, and the ISO's GRUB (`build-grub.sh`: its
  menu inside it, editor and shell closed, as in `scripts/secureboot`),
  signed with the Secure Boot key when one is given;
- `nickelos/vmlinuz`: the image's own kernel; `nickelos/initrd.img`: the
  installer (`make-initramfs.sh`: busybox, util-linux's losetup,
  frecon-lite, the filesystem modules, `init`, `installer`);
  `nickelos/image.sqfs`: the image itself (the USB image,
  chromiumos_image.bin) in a squashfs (its empty space costs nothing);
  the key's certificate, for MokManager.

At boot, `init` finds the medium (a filesystem with nickelos/image.sqfs, or
an ISO file holding one, as Ventoy keeps them), mounts the image, starts
frecon-lite and runs the installer once, on its VT1:
- **Install NickelOS on this computer**: a disk, "yes", and
  `chromeos-install --payload_image` (NickelOS's own installer, run from the
  image's system partition) writes the installed layout: two systems for
  updates, the data partition the rest of the disk.
- **Try NickelOS first**: the stick becomes the NickelOS USB image (the
  live session with setup's installer, as before the ISO): the image into
  memory, its partition table made again for the stick (sfdisk), the
  partitions written, ROOT-A by its used blocks (e2image), the data
  partition made new over the rest of the stick and filled with the
  image's files. Refused on a disc, on a stick holding other files, and
  when NickelOS started from an ISO file (Ventoy): Install works there.

What getting here took (ChromeOS's kernel and frecon, in an initramfs):
- `root=/dev/ram0` on the kernel's command line: without a root= the kernel
  unpacks the initramfs onto tmpfs, and ChromeOS's security module refuses
  to run anything from tmpfs (its memfd rule), init included ("Failed to
  execute /init (error -13)", read from the kernel's log in the VM's
  memory: QMP memsave of __log_buf, the kernel has no console);
- no console at all (no VT, no serial line): init must not redirect to
  /dev/console when it can't be opened;
- frecon-lite, not frecon: frecon picks keyboards by a udev property, and
  no udev runs here; `--no-login` and the installer started on VT1 by init
  (frecon would start one in every terminal), `switchvt` and `input:on`
  escapes so VT1 shows and takes keys;
- the modules (ISO 9660, FAT, exFAT, NTFS) load from the initramfs, and
  LoadPin then pins it; nothing later needs a module, since both choices
  end in a restart.

Tried in a VM (OVMF, Secure Boot on, Microsoft's keys, the test key
enrolled; 16836.3.50 and 3.51 test images, ISO 3.56 GB because a test image
carries 2.6 GB of developer tools):
- a dd'd stick: Install, then the disk booted NickelOS under Secure Boot
  with both 6 GiB systems and the data partition on the rest; Try, then
  the stick booted NickelOS's setup under Secure Boot, running from the
  stick (is_running_from_installer: yes), its data partition the rest of
  the 16 GB stick;
- Rufus's ISO mode (`rufus-stick.sh`: the files on FAT32): Try wrote the
  stick in about a minute, then it booted NickelOS under Secure Boot from
  the stick;
- Ventoy (`ventoy-stick.sh`: the ISO as a file on exFAT, beside another):
  init found and mounted the ISO file, Try refused (the stick holds other
  files), Install wrote the disk, and the disk booted NickelOS under Secure
  Boot;
- the release size: from a base image (16836.3.51, no developer tools)
  the ISO is 1.85 GB (the squashfs 1.7 GB); written as it is to a stick,
  Install took about 70 seconds and the disk started NickelOS's setup
  under Secure Boot;
- a display only the firmware drives (`iso-vm.sh` with `GPU="-vga std"`):
  the menu shows through simpledrm, which the kernel has built in (the
  firmware's framebuffer, so any GPU the firmware draws on); NickelOS's own boots turn it off
  (`initcall_blacklist` in `scripts/build_kernel_image.sh`), the ISO's
  GRUB leaves it on.

Still to do:
- real PCs: the menu on the firmware's framebuffer, Install and Try;
- the initramfs isn't covered by Secure Boot (GRUB checks the kernel
  only).

Test builds: `NICKEL_ISO_ARGS="nickelos.debuglog=/dev/vdb"` puts init's log
and the kernel's on a spare disk (`iso-vm.sh` and `direct-vm.sh` have one),
each boot in its own 4 MB of it, every second.

Testing on the build host: keep the VM's stick and disk images in
/dev/shm. With them on the host's disk, the VM froze for up to a minute
during Try while the host disk was busy (the guest's log: "clocksource:
Long readout interval ... cs_nsec: 63343433331", then a USB reset), and
the kernel's lockup panics restarted it mid-write. From /dev/shm, Try
finished in about a minute.
