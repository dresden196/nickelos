#!/bin/bash
# On nickel-build, as root: a copy of a test image whose EFI system
# partition boots shim -> NickelOS's GRUB -> the kernel, all signed for
# Secure Boot with a throwaway test key (not the update key, not a key to
# ship), and the key's certificate on the partition for MokManager.
set -euo pipefail
S=/srv/nickel/src/.nickel/sb
W=/srv/nickel/sb-proto
K=/srv/nickel/keys/secureboot-test
IMG=${IMG:?test image}
mkdir -p "$W" "$K"
if [ ! -f "$K/sb-test.key" ]; then
  openssl req -new -x509 -newkey rsa:2048 -nodes -days 3650 -sha256 \
    -subj "/CN=NickelOS Secure Boot TEST key (not for release)/" \
    -keyout "$K/sb-test.key" -out "$K/sb-test.crt" 2>/dev/null
  openssl x509 -in "$K/sb-test.crt" -outform DER -out "$K/sb-test.der"
  chmod 600 "$K/sb-test.key"
fi
sbsign --key "$K/sb-test.key" --cert "$K/sb-test.crt" --output "$W/grubx64.efi" "$S/grubx64.efi" 2>&1 | grep -v "^warning" || true
cp --sparse=always "$IMG" "$W/sb.bin"
l=$(losetup -fP --show "$W/sb.bin"); m=$(mktemp -d)
trap 'umount "$m" 2>/dev/null || true; losetup -d "$l"; rmdir "$m"' EXIT
mount "${l}p12" "$m"
cp /usr/lib/shim/shimx64.efi.signed.latest "$m/efi/boot/bootx64.efi"
cp /usr/lib/shim/mmx64.efi "$m/efi/boot/mmx64.efi"
cp "$W/grubx64.efi" "$m/efi/boot/grubx64.efi"
cp "$K/sb-test.der" "$m/nickelos-secureboot-test.der"
for k in A B; do
  sbsign --key "$K/sb-test.key" --cert "$K/sb-test.crt" --output "$m/syslinux/vmlinuz.$k.signed" "$m/syslinux/vmlinuz.$k" 2>&1 | grep -v "^warning" || true
  mv "$m/syslinux/vmlinuz.$k.signed" "$m/syslinux/vmlinuz.$k"
done
sbverify --cert "$K/sb-test.crt" "$m/efi/boot/grubx64.efi"
sbverify --cert "$K/sb-test.crt" "$m/syslinux/vmlinuz.A"
ls -la "$m/efi/boot" "$m/syslinux" | grep -E "efi|vmlinuz"
sync
