#!/bin/bash
# The Secure Boot prototype VM: OVMF with Secure Boot enforced and
# Microsoft's keys (as on a PC), the assembled image, ssh on 9230.
# FRESH=1 starts from new firmware variables (nothing enrolled); DISK= another
# image (default: assemble.sh's).
W=/srv/nickel/sb-proto
[ "${FRESH:-0}" = 1 ] || [ ! -f $W/vars.fd ] && cp /usr/share/OVMF/OVMF_VARS_4M.ms.fd $W/vars.fd
rm -f $W/qmp.sock
exec qemu-system-x86_64 -machine q35,smm=on,accel=kvm \
  -global driver=cfi.pflash01,property=secure,value=on \
  -drive if=pflash,format=raw,unit=0,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.secboot.fd \
  -drive if=pflash,format=raw,unit=1,file=$W/vars.fd \
  -m 6G -smp 4 -cpu host -device virtio-vga -display none \
  -drive file=${DISK:-$W/sb.bin},format=raw,if=virtio \
  -nic user,model=virtio-net-pci,hostfwd=tcp:127.0.0.1:9230-:22 \
  -qmp unix:$W/qmp.sock,server,nowait -serial file:$W/serial.log
