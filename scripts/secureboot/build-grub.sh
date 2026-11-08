#!/bin/bash
# In the SDK: the board's GRUB as one signed-ready image: every module it
# needs built in (Secure Boot forbids loading modules), the menu in a
# memdisk, SBAT data for shim.
set -euo pipefail
S=/mnt/host/source/.nickel/sb
mods=/build/nickel/lib64/grub/x86_64-efi
t=$(mktemp -d)
mkdir -p "$t/m/boot/grub"
ARGS=${ARGS:-init=/sbin/init rootwait ro noresume loglevel=7 noinitrd console= i915.modeset=1 cros_efi cros_debug}
sed "s|@ARGS@|${ARGS}|" "$S/grub.cfg" > "$t/m/boot/grub/grub.cfg"
tar -C "$t/m" -cf "$t/memdisk.tar" boot
grub-mkimage -O x86_64-efi -d "$mods" -o "$S/grubx64.efi" -p '(memdisk)/boot/grub' \
	-c "$S/early.cfg" -m "$t/memdisk.tar" --sbat "$S/sbat.csv" \
	part_gpt gptpriority test fat ext2 normal boot linux search probe keystatus \
	regexp echo sleep reboot halt efi_gop memdisk tar password_pbkdf2
grep -q "tries used up" "$S/grubx64.efi" || { echo "no NickelOS gptpriority" >&2; exit 1; }
ls -la "$S/grubx64.efi"
rm -rf "$t"
