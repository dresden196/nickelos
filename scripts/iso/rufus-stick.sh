#!/bin/bash
# A stick as Rufus's ISO mode writes it: GPT, one FAT32 partition, the
# ISO's files on it (Rufus leaves out boot.catalog and [BOOT]). On the
# build host, as root: S the stick's image (16 GB), ISO the ISO. Then
# direct-vm.sh or iso-vm.sh with STICK=$S; on this host, keep both in
# /dev/shm (README: testing).
set -euo pipefail
P=/srv/nickel/sb-proto
S=${S:-$P/rufus-stick.img}
ISO=${ISO:-$P/nickelos-test.iso}
rm -f $S; truncate -s 16G $S
sgdisk -o -n 1:2048:0 -t 1:0700 -c 1:NICKELOS $S >/dev/null
l=$(losetup -fP --show $S)
mkfs.vfat -F 32 -n NICKELOS ${l}p1 >/dev/null
m=$(mktemp -d); i=$(mktemp -d)
mount ${l}p1 $m
mount -o ro,loop $ISO $i
cp -r $i/EFI $i/nickelos $i/README.txt $m/
[ -f $i/nickelos-secureboot.der ] && cp $i/nickelos-secureboot.der $m/
ls -la $m $m/nickelos
umount $i $m; losetup -d $l; rmdir $m $i
