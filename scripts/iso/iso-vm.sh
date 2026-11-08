#!/bin/bash
# The ISO in a VM, as a USB stick written with dd (STICK: a 16 GB file that
# starts with the ISO), beside an empty 40 GB disk to install on (DISK),
# under OVMF with Secure Boot and Microsoft's keys (VARS: its variables;
# the prototype's, with the test key enrolled, by default). ssh on 9231 once
# NickelOS runs, QMP at $W/iso-qmp.sock (../secureboot/shot.sh with
# QMP=...; nickel-vmctl with NICKEL_VMTEST_DIR=$W/ctl, which links it as
# qmp.sock, for clicks through the USB tablet), the kernel's console in
# $W/iso-serial.log.
#   iso-vm.sh new ISO   a new stick and disk from the ISO, then the VM
#   iso-vm.sh           the VM again with the stick and disk as they are
#   NOSTICK=1           without the stick (boot what was installed)
#   GPU="-vga std"      a display only the firmware drives (simpledrm)
set -euo pipefail
W=/srv/nickel/sb-proto
STICK=${STICK:-$W/iso-stick.img}
DISK=${DISK:-$W/iso-disk.img}
VARS=${VARS:-$W/vars.fd}
if [ "${1:-}" = new ]; then
	iso=${2:?iso}
	rm -f "$STICK" "$DISK"
	truncate -s 16G "$STICK"
	dd if="$iso" of="$STICK" bs=4M conv=notrunc,sparse status=none
	truncate -s 40G "$DISK"
fi
# A small disk for test ISOs' nickelos.debuglog=/dev/vdb (init's log).
[ -f "$W/iso-debug.img" ] || truncate -s 64M "$W/iso-debug.img"
rm -f "$W/iso-qmp.sock"
mkdir -p "$W/ctl"; ln -sfn "$W/iso-qmp.sock" "$W/ctl/qmp.sock"
args=(-machine q35,smm=on,accel=kvm -global driver=cfi.pflash01,property=secure,value=on
	-drive if=pflash,format=raw,unit=0,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.secboot.fd
	-drive if=pflash,format=raw,unit=1,file="$VARS"
	-m 8G -smp 4 -cpu host ${GPU:--device virtio-vga} -display none
	-device qemu-xhci,id=xhci -device usb-tablet,bus=xhci.0
	-drive if=none,id=disk0,file="$DISK",format=raw -device virtio-blk-pci,drive=disk0
	-drive if=none,id=dbg,file="$W/iso-debug.img",format=raw -device virtio-blk-pci,drive=dbg
	-nic user,model=virtio-net-pci,hostfwd=tcp:127.0.0.1:9231-:22
	-qmp unix:"$W/iso-qmp.sock",server,nowait -serial file:"$W/iso-serial.log")
if [ "${NOSTICK:-0}" != 1 ]; then
	args+=(-drive if=none,id=stick,file="$STICK",format=raw -device usb-storage,bus=xhci.0,drive=stick,removable=on,bootindex=0)
fi
exec qemu-system-x86_64 "${args[@]}"
