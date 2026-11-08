#!/bin/bash
# Quick tries of the ISO's initramfs (on the build host, as root): the
# kernel and a fresh initramfs booted directly (OVMF), the built ISO as a
# USB stick (STICK: its 16 GB image), the 40 GB disk to install on (vda;
# DISK), the debug disk (vdb); MEM the VM's memory, XARGS more kernel
# arguments.
set -u
W=/srv/nickel/src/.nickel/iso-work
P=/srv/nickel/sb-proto
[ "${NEWSTICK:-0}" = 1 ] && { rm -f $P/iso-stick.img $P/iso-disk.img; truncate -s 16G $P/iso-stick.img; dd if=$P/nickelos-test.iso of=$P/iso-stick.img bs=4M conv=notrunc,sparse status=none; truncate -s 40G $P/iso-disk.img; }
: > $P/iso-debug.img; truncate -s 64M $P/iso-debug.img
cp /usr/share/OVMF/OVMF_VARS_4M.fd /tmp/direct-vars.fd
rm -f $P/iso-qmp.sock
exec qemu-system-x86_64 -machine q35,accel=kvm -m ${MEM:-8G} -smp 4 -cpu host \
  -drive if=pflash,format=raw,unit=0,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd \
  -drive if=pflash,format=raw,unit=1,file=/tmp/direct-vars.fd \
  -kernel $W/iso/nickelos/vmlinuz -initrd $W/iso/nickelos/initrd.img \
  -append "rdinit=/init root=/dev/ram0 cros_efi nickelos.debuglog=/dev/vdb ${XARGS:-}" \
  -device virtio-vga -display none -device qemu-xhci,id=xhci \
  -drive if=none,id=disk0,file=${DISK:-$P/iso-disk.img},format=raw -device virtio-blk-pci,drive=disk0 \
  -drive if=none,id=dbg,file=$P/iso-debug.img,format=raw -device virtio-blk-pci,drive=dbg \
  -drive if=none,id=stick,file=${STICK:-$P/iso-stick.img},format=raw -device usb-storage,bus=xhci.0,drive=stick,removable=on \
  -nic user,model=virtio-net-pci,hostfwd=tcp:127.0.0.1:9231-:22 \
  -qmp unix:$P/iso-qmp.sock,server,nowait
