#!/bin/bash
# In the SDK: the ISO's GRUB, from the board's modules: everything built in
# (Secure Boot), the menu in a memdisk, the editor and shell closed
# (scripts/secureboot/early.cfg), SBAT data.  ARGS: more kernel arguments.
#   build-grub.sh OUT.efi
set -euo pipefail
out=${1:?output}
BOARD=${NICKEL_BOARD:-nickel}
mods=/build/$BOARD/lib64/grub/x86_64-efi
here=$(dirname "$(readlink -f "$0")")
sb=$here/../secureboot
t=$(mktemp -d)
trap 'rm -rf "$t"' EXIT
mkdir -p "$t/m/boot/grub"
sed "s|@ARGS@|${ARGS:-}|" "$here/grub.cfg" > "$t/m/boot/grub/grub.cfg"
tar -C "$t/m" -cf "$t/memdisk.tar" boot
grub-mkimage -O x86_64-efi -d "$mods" -o "$out" -p '(memdisk)/boot/grub' \
	-c "$sb/early.cfg" -m "$t/memdisk.tar" --sbat "$sb/sbat.csv" \
	part_gpt part_msdos fat iso9660 normal boot linux search search_fs_file \
	test echo sleep reboot halt efi_gop memdisk tar password_pbkdf2
echo "ISO GRUB: $out"
