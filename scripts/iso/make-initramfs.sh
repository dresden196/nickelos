#!/bin/bash
# In the SDK: the ISO's initramfs (scripts/iso/README.md) from the board's
# root: busybox, util-linux's losetup (partitions of a loop device),
# blockdev and switch_root, lvm2's dmsetup (the live session's
# device-mapper devices), frecon-lite (ChromeOS's console as its own
# initramfs use it: it finds keyboards in /dev/input, where frecon asks
# udev, which doesn't run here) and their libraries (lddtree), the kernel's
# modules for the filesystems boot media use if the kernel doesn't build
# them in (the kernel's own version, signed as the build signs them;
# NickelOS's kernel builds them in, so none), init and the installer.
#   make-initramfs.sh OUT.cpio.xz
set -euo pipefail
out=${1:?output}
BOARD=${NICKEL_BOARD:-nickel}
R=/build/$BOARD
here=$(dirname "$(readlink -f "$0")")
t=$(mktemp -d)
trap 'rm -rf "$t"' EXIT
mkdir -p "$t"/{bin,sbin,etc/nickelos,lib/modules,proc,sys,dev,run,tmp,media,nickelos}
[ -x "$R/sbin/frecon-lite" ] || emerge-"$BOARD" --quiet --noreplace sys-apps/frecon-lite
lddtree -R "$R" --copy-to-tree "$t" /bin/busybox /sbin/losetup /sbin/blockdev \
	/sbin/switch_root /sbin/dmsetup /usr/bin/unshare /sbin/frecon-lite >/dev/null
mv "$t/sbin/frecon-lite" "$t/sbin/frecon"
# losetup as /sbin/losetup is the real one; busybox's applets go to /bin.
kver=$(ls "$R/lib/modules" | head -1)
md=$R/lib/modules/$kver
: > "$t/etc/nickelos/modules"
# Each module after the ones it needs (modules.dep), once; none for what
# the kernel builds in (modules.builtin).
add_mod() {
	local name=$1 path dep
	grep -q "/${name}\.ko\$" "$md/modules.builtin" 2>/dev/null && return 0
	path=$(awk -F: -v m="/$name.ko" 'index($1, m) == length($1) - length(m) + 1 { print $1; exit }' "$md/modules.dep")
	[ -n "$path" ] || { echo "no module $name in $kver" >&2; return 1; }
	grep -qx "${path##*/}" "$t/etc/nickelos/modules" && return 0
	for dep in $(awk -F: -v p="$path" '$1 == p { print $2 }' "$md/modules.dep"); do
		dep=${dep##*/}; add_mod "${dep%.ko}"
	done
	cp "$md/$path" "$t/lib/modules/"
	echo "${path##*/}" >> "$t/etc/nickelos/modules"
}
for m in nls_cp437 nls_iso8859-1 nls_utf8 isofs fat vfat exfat ntfs3; do add_mod "$m"; done
# /init's interpreter: there before init makes busybox's other links.
ln -sf busybox "$t/bin/sh"
install -m 755 "$here/init" "$t/init"
install -m 755 "$here/installer" "$t/sbin/agetty"
(cd "$t" && find . | cpio -o -H newc --quiet) | xz -9 --check=crc32 > "$out"
echo "initramfs: $(du -h "$out" | cut -f1), modules: $(tr '\n' ' ' < "$t/etc/nickelos/modules")"
