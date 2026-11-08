#!/bin/bash
# After build-grub.sh: the new GRUB, signed with the test key, onto the
# prototype image's EFI system partition (the VM stopped).
set -euo pipefail
S=/srv/nickel/src/.nickel/sb; W=/srv/nickel/sb-proto; K=/srv/nickel/keys/secureboot-test
sbsign --key $K/sb-test.key --cert $K/sb-test.crt --output $W/grubx64.efi $S/grubx64.efi 2>&1 | grep -v "^warning" || true
l=$(losetup -fP --show $W/sb.bin); m=$(mktemp -d)
trap 'umount "$m" 2>/dev/null || true; losetup -d "$l"; rmdir "$m"' EXIT
mount ${l}p12 $m
cp $W/grubx64.efi $m/efi/boot/grubx64.efi
sbverify --cert $K/sb-test.crt $m/efi/boot/grubx64.efi
umount $m
