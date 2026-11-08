#!/bin/bash
# On a build host, as root: NickelOS's ISO (scripts/iso/README.md) from a
# built image (the USB image, chromiumos_*_image.bin):
#  - EFI/BOOT: shim (sys-boot/nickel-shim) as BOOTX64.EFI, MokManager, and
#    the ISO's GRUB (scripts/iso/build-grub.sh, signed with the Secure Boot
#    key if one is given), as files (Rufus copies them onto a FAT stick) and
#    in an EFI system partition image (discs, and sticks written as they
#    are: dd, Etcher, Stoke);
#  - nickelos/vmlinuz: the image's kernel (signed if the image is),
#    nickelos/initrd.img: the installer (scripts/iso/make-initramfs.sh and
#    the image's lsb-release), nickelos/image.sqfs: the image itself in a
#    squashfs (zstd; its empty space costs nothing), and the key's
#    certificate for MokManager.
#   build-iso.sh IMAGE OUT.iso [KEY CERT]
# For a release, the production key stays on Dresden's laptop
# (scripts/release): NICKEL_ISO_GRUB is the ISO's GRUB built and signed
# there beforehand, and with CERT but no KEY (an empty third argument) the
# GRUB and the image's kernel must already be signed with CERT's key.
set -euo pipefail
img=$(readlink -f "${1:?image}") out=${2:?output iso} key=${3:-} cert=${4:-}
src=/srv/nickel/src
here=$(dirname "$(readlink -f "$0")")
ovl=src/overlays/overlay-nickel
board=${NICKEL_BOARD:-nickel}
W=$src/.nickel/iso-work
rm -rf "$W"; mkdir -p "$W/iso/EFI/BOOT" "$W/iso/nickelos" "$W/m"; chown -R nickel:nickel "$W"
stage() { echo "== $1 $(date +%T)"; }

stage "initramfs and GRUB (SDK)"
sdk() { sudo -u nickel env -C "$src" /home/nickel/depot_tools/cros_sdk -- "$@"; }
sdk "/mnt/host/source/$ovl/scripts/iso/make-initramfs.sh" /mnt/host/source/.nickel/iso-work/initrd-base.cpio.xz
if [ -n "${NICKEL_ISO_GRUB:-}" ]; then
	cp "$NICKEL_ISO_GRUB" "$W/grubx64.efi"
	echo "ISO GRUB: $NICKEL_ISO_GRUB"
else
	sdk env ARGS="${NICKEL_ISO_ARGS:-}" "/mnt/host/source/$ovl/scripts/iso/build-grub.sh" /mnt/host/source/.nickel/iso-work/grubx64.efi
fi

stage "from the image: kernel, version"
l=$(losetup -fP --show -r "$img")
trap 'umount "$W/m" 2>/dev/null || true; losetup -d "$l" 2>/dev/null || true' EXIT
mount -o ro "${l}p12" "$W/m"
cp "$W/m/syslinux/vmlinuz.A" "$W/iso/nickelos/vmlinuz"
umount "$W/m"
mount -o ro "${l}p3" "$W/m"
mkdir -p "$W/extra/etc/nickelos"
cp "$W/m/etc/lsb-release" "$W/extra/etc/nickelos/lsb-release"
version=$(sed -n 's/^CHROMEOS_RELEASE_VERSION=//p' "$W/m/etc/lsb-release")
umount "$W/m"
losetup -d "$l"
trap - EXIT
# The image's version beside the installer: a second cpio archive after the
# first (the kernel unpacks both).
(cd "$W/extra" && find . | cpio -o -H newc --quiet) > "$W/extra.cpio"
cat "$W/initrd-base.cpio.xz" "$W/extra.cpio" > "$W/iso/nickelos/initrd.img"

stage "EFI files"
shim=$src/out/build/$board/usr/share/nickel-shim
cp "$shim/shimx64.efi" "$W/iso/EFI/BOOT/BOOTX64.EFI"
cp "$shim/mmx64.efi" "$W/iso/EFI/BOOT/mmx64.efi"
cp "$W/grubx64.efi" "$W/iso/EFI/BOOT/grubx64.efi"
if [ -n "$key" ]; then
	sbsign --key "$key" --cert "$cert" --output "$W/iso/EFI/BOOT/grubx64.efi" "$W/grubx64.efi" 2>&1 | grep -v "^warning\|^Signing Unsigned" || true
	sbverify --cert "$cert" "$W/iso/EFI/BOOT/grubx64.efi" >/dev/null
	sbverify --cert "$cert" "$W/iso/nickelos/vmlinuz" >/dev/null ||
		sbsign --key "$key" --cert "$cert" --output "$W/iso/nickelos/vmlinuz" "$W/iso/nickelos/vmlinuz" 2>&1 | grep -v "^warning\|^Signing Unsigned" || true
	openssl x509 -in "$cert" -outform DER -out "$W/iso/nickelos-secureboot.der"
elif [ -n "$cert" ]; then
	sbverify --cert "$cert" "$W/iso/EFI/BOOT/grubx64.efi" >/dev/null ||
		{ echo "build-iso.sh: the ISO's GRUB isn't signed with $cert's key" >&2; exit 1; }
	sbverify --cert "$cert" "$W/iso/nickelos/vmlinuz" >/dev/null ||
		{ echo "build-iso.sh: the image's kernel isn't signed with $cert's key" >&2; exit 1; }
	openssl x509 -in "$cert" -outform DER -out "$W/iso/nickelos-secureboot.der"
fi
# The EFI system partition image: the same EFI/BOOT (and the certificate).
efi_kb=$(( ($(du -sk "$W/iso/EFI" | cut -f1) + 2048) / 1024 * 1024 + 1024 ))
mkfs.vfat -n NICKELEFI -C "$W/efiboot.img" "$efi_kb" >/dev/null
mmd -i "$W/efiboot.img" ::/EFI ::/EFI/BOOT
mcopy -i "$W/efiboot.img" "$W/iso/EFI/BOOT/"* ::/EFI/BOOT/
[ -f "$W/iso/nickelos-secureboot.der" ] && mcopy -i "$W/efiboot.img" "$W/iso/nickelos-secureboot.der" ::/

stage "the image in a squashfs"
# Named as the installer looks for it: a hard link (no copy) where it can.
mkdir -p "$W/sq"
ln "$img" "$W/sq/chromiumos_image.bin" 2>/dev/null || cp --sparse=always "$img" "$W/sq/chromiumos_image.bin"
mksquashfs "$W/sq" "$W/iso/nickelos/image.sqfs" -comp zstd -Xcompression-level 19 -b 1M \
	-noappend -all-root -no-xattrs -quiet
rm -rf "$W/sq"
cat > "$W/iso/README.txt" <<TXT
NickelOS $version

Write this ISO to a USB stick (Rufus, Etcher, Ventoy, Stoke or dd) and start
the computer from it. Then: install NickelOS on the computer, or make the
stick a NickelOS stick to try it first. https://nickelos.dev
TXT

stage "the ISO"
xorriso -as mkisofs -o "$out" -V NICKELOS -r -J -joliet-long -iso-level 3 \
	-partition_offset 16 \
	-append_partition 2 0xef "$W/efiboot.img" -appended_part_as_gpt \
	-iso_mbr_part_type a2a0d0ebe5b9334487c068b6b72699c7 \
	-c boot.catalog -e --interval:appended_partition_2:all:: -no-emul-boot \
	"$W/iso" 2>&1 | grep -v "^xorriso : UPDATE\|^Drive current\|^Media current\|^Media status\|^Media summary\|^Added to ISO\|^Written to medium\|^Writing to\|^libisofs" || true
ls -la "$out"
du -h "$W/iso/nickelos/"*
