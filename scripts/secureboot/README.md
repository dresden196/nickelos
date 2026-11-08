# Secure Boot prototype (2026-10-25)

Not wired into the build yet: these scripts take a built test image and
make a copy that boots with UEFI Secure Boot on, the way a PC with
Microsoft's keys does, to find out what NickelOS's Secure Boot path needs
(docs/vision.md, "Updates", Open). A throwaway test key, made on the build
host at `/srv/nickel/keys/secureboot-test` (not the update key, never to
ship).

The chain: the firmware runs **shim** (Ubuntu's, signed by Microsoft;
`shim-signed` on the build host) as `efi/boot/bootx64.efi`; shim runs
**NickelOS's GRUB** (`grubx64.efi`, signed with our key) once our key is
enrolled in shim's MOK list; GRUB loads **the kernel** (`syslinux/vmlinuz.A`
and `.B`, signed with our key), which shim checks before GRUB starts it.

- `build-grub.sh` (in the SDK): the board's GRUB (with NickelOS's
  gptpriority) as one image: every module built in (Secure Boot forbids
  loading modules), the menu (`grub.cfg`) in a memdisk inside the signed
  image, so nothing on the disk changes what boots or with which command
  line, an early config (`early.cfg`) that closes the editor and the GRUB
  shell, and SBAT data (`sbat.csv`) for shim.
- `assemble.sh` (IMG=test image): the key (first time), the signatures, and
  the image copy at `/srv/nickel/sb-proto/sb.bin` with shim, MokManager,
  the signed GRUB and kernels and the key's certificate on the EFI system
  partition; `regrub.sh` puts a rebuilt GRUB on it.
- `run-vm.sh` (FRESH=1: nothing enrolled): OVMF with Secure Boot enforced
  and Microsoft's keys; `shot.sh NAME [keys]` types and takes screenshots
  over QMP.

What it showed, in that VM:
- Shim refuses our GRUB until our key is enrolled ("Verification failed:
  (0x1A) Security Violation"), then offers MokManager: Enroll key from
  disk, the certificate on the EFI partition, Yes, Reboot. After that:
  shim, our GRUB, our menu, the kernel, NickelOS up with SecureBoot=1
  ("Secure boot enabled"); the kernel's lockdown is "integrity", as it
  already was without Secure Boot, so the out-of-tree modules load as
  before (the build signs them).
- `e` and `c` at the menu ask for a user; the menu's superuser has no
  password, so neither opens.
- An unsigned kernel is refused ("bad shim signature").
- GRUB looks for the menu at `$prefix/grub.cfg`, and a prefix of just
  `(memdisk)` becomes `(memdisk)/boot/grub`; without a menu it drops to an
  open shell, so the early config closes it first.
- A slot whose kernel doesn't verify: its entry loads the other slot's
  kernel instead, and after GRUB's "Press any key to continue" (10 seconds
  at most) boots it, unattended. With an unsigned kernel in A, the VM went
  on to B's kernel 30 times in a row; B's system partition in this image is
  an empty 2 MiB stub until an update fills it, so B's kernel reset the VM
  each time. To confirm on an image whose B holds a system (after an
  update). GRUB's own `fallback` doesn't help here: it runs only after a
  failed `boot`, not a failed `linux`.

In the build (2026-10-26), off by default: `nickel-build-stock` with
`NICKEL_SECUREBOOT_KEY` and `NICKEL_SECUREBOOT_CERT` (PEM files on the
build host) installs `sys-boot/nickel-shim` (Ubuntu's shim-signed 1.58,
shim 15.8, pinned by its Manifest), has `nickel-image-grub` build this GRUB
(`NICKEL_SECUREBOOT=1`: the menu's kernel arguments come from the image's
own grub.cfg) and put shim, MokManager and GRUB on the EFI system partition
and in `/boot/efi/boot`, and then signs GRUB and the kernels outside the
SDK (`nickel-image-sbsign`). Tried on a copy of 16836.3.50: boots with
Secure Boot on (test key enrolled) and with it off (shim then loads GRUB
without checks). An update's postinstall (`chromeos_legacy.cc`) copies
every `.efi` of the new system's `/boot/efi/boot` to the EFI system
partition and its `/boot/vmlinuz` to `syslinux/vmlinuz.<slot>`, so updates
bring the chain along as they are.

Ubuntu's shim 15.8 is signed by Microsoft's UEFI CA 2011 (its
"dualsigned" copy adds Canonical's, not the 2023 CA). Every PC trusts the
2011 CA today; when PCs ship without it, a newer distribution shim signed
with the 2023 CA replaces it (a package bump).

The production key (2026-10-31): `nickel-secureboot-key --new-key` on
Dresden's laptop makes it in `~/.local/share/nickelos-keys/`, beside the
update key: `nickelos-secureboot.key.pem` (RSA 2048, encrypted with his
passphrase), `.crt.pem` (what sbsign signs with) and `.cer` (DER, what
people enroll in MokManager). `--copy-to <stick>/nickelos` copies it to his
flash drive and checks the copy; `--check` checks a copy. It refuses to
replace an existing key: every PC that enrolled the old certificate would
refuse the next GRUB. Tested 2026-10-31 with a throwaway key: sbsign signs
an EFI binary with it and sbverify accepts it.

Made by Dresden on 2026-10-31: certificate "CN=NickelOS Secure Boot,
O=NickelOS", SHA-256
`0F:5D:11:B3:23:C2:13:06:0A:9C:70:7E:0F:09:CA:81:72:E5:9A:F5:EB:77:9D:06:1E:73:2A:A3:C0:20:3E:A3`,
valid to 2056. On his laptop (`~/.local/share/nickelos-keys/`) and, since
2026-11-01, his flash drive (`KEYSTICK`, `nickelos/`, beside the update
key), byte for byte the same.

That certificate can't sign a release (found 2026-11-07, in the release's
dry run): it names two usages, code signing and 1.3.6.1.4.1.2312.16.1.2
(module signing only), and shim refuses to run a binary signed by a
certificate with the second one. A stand-in made the same way, enrolled
through MokManager, still got "Verification failed: (0x1A) Security
Violation" for the ISO's GRUB; a stand-in with code signing only booted the
ISO, installed, and the disk booted to setup under Secure Boot. The test
key (CA, no usages) never had it. `nickel-secureboot-key --reissue-cert`
makes a new certificate for the same key (code signing only; the old one
goes to `old/`), `--update-copy` puts it on the flash drive, and `--check`,
`nickel-release-sign` and `nickel-release attach` refuse a certificate with
that usage. Nothing had enrolled the first certificate, so replacing it
costs nothing.

Releases are signed on the laptop, the key never leaving it
(`scripts/release/`, docs/updates.md "Make a release"): the build host
sends GRUB, the kernel and the ISO's GRUB, `nickel-release-sign` returns
detached signatures (`sbsign --detached`), and the build host attaches them
(`nickel-image-sbsign --attach`). The certificate's public copy is
`nickelos-secureboot.crt.pem` / `.cer` here.

What NickelOS needs to ship it (decisions are Dresden's):
- whose shim: a distribution's Microsoft-signed one (as here) with NickelOS's
  key enrolled once by the user, or NickelOS's own shim reviewed and signed
  by Microsoft (months, and an organization behind it);
- NickelOS's Secure Boot key, kept like the update key (signing on his
  removable media), and test images signed with a test key;
- the build: this GRUB from `nickel-image-grub`, signatures on GRUB and
  every kernel before the update payload is made (an update's postinstall
  copies the kernel and EFI files from the new system to the EFI partition,
  so they must already be signed), shim and MokManager on the EFI
  partition and in `/boot/efi/boot`;
- the SBAT generation our GRUB claims (Ubuntu's shim 15.8 asks for grub 3
  or later; `grub,4` here) checked against the fixes Google's GRUB 2.12
  carries;
- the install media: under Secure Boot the stick itself only boots after
  the key is enrolled, so the first boot of the ISO goes through MokManager
  (the certificate on the stick);
- and separately: NickelOS boots its system partition unverified ("local
  image A", built with `--no-enable-rootfs-verification`), so Secure Boot
  covers GRUB and the kernel, not the system files.
