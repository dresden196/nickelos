#!/bin/bash
# A stick as Ventoy keeps ISOs: an exFAT partition with ISO files on it
# (NickelOS's and another), plus Ventoy's small EFI partition. Ventoy's own
# boot loader isn't here: the VM boots the ISO's kernel and initramfs
# directly, which tests what NickelOS does once started from such a stick.
# On the build host, as root: S the stick's image (16 GB), ISO the ISO;
# then direct-vm.sh with STICK=$S. exFAT by FUSE (exfat-fuse): Ubuntu keeps
# its exfat module in linux-modules-extra.
set -euo pipefail
P=/srv/nickel/sb-proto
S=${S:-$P/ventoy-stick.img}
ISO=${ISO:-$P/nickelos-test.iso}
rm -f $S; truncate -s 16G $S
sgdisk -o -n 1:2048:-34M -t 1:0700 -c 1:Ventoy -n 2:0:0 -t 2:ef00 -c 2:VTOYEFI $S >/dev/null
l=$(losetup -fP --show $S)
mkfs.exfat -L Ventoy ${l}p1 >/dev/null
mkfs.vfat -n VTOYEFI ${l}p2 >/dev/null
m=$(mktemp -d); mount.exfat-fuse ${l}p1 $m
mkdir -p $m/iso
cp $ISO $m/iso/nickelos.iso
echo "another distribution" > $m/iso/other-distro.iso
ls -la $m $m/iso
umount $m; losetup -d $l; rmdir $m
