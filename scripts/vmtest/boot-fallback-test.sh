#!/bin/bash
# Runs on nickel-next, as root: an update that doesn't boot falls back to
# the system before it (patches/sys-boot/grub, docs/updates.md "What a PC
# does"), with release images in a VM:
#   1. release-update-test.sh with STOP_AFTER_APPLY=1: IMG_N updates itself
#      to IMG_NEXT from update.nickelos.dev and is stopped right after the
#      update is applied (B: higher priority, tries, not successful)
#   2. good: a copy of that disk boots B, and B is marked successful
#      (chromeos-setgoodkernel, from update_engine)
#   3. bad: another copy with ROOT-B wiped, so B's kernel finds no root.
#      Each boot is ended by a power off, as someone would after a hang;
#      GRUB spends one of B's tries per boot and, once they are used up
#      (at most 3), boots A again
# Prints each boot's GRUB entry and KERN-A/KERN-B's GPT priority, tries and
# successful bits.
#   IMG_N=.../chromiumos_base_image.bin IMG_NEXT=.../chromiumos_base_image.bin boot-fallback-test.sh
# The base images must be built for tests (NICKEL_TEST_KEY=1
# nickel-build-stock): they then carry the test payload key, which this
# test signs with; release images carry the production key, whose private
# half is only with Dresden.
set -u
export NICKEL_VMTEST_DIR=/srv/nickel/vmtest-r NICKEL_VMTEST_SSHPORT=9226
D=$NICKEL_VMTEST_DIR
here=$(dirname "$(readlink -f "$0")")
unit=nickel-$(basename "$D")
vmimg=$(dirname "${IMG_N:?}")/chromiumos_qemu_image.bin

attrs() {   # the GPT attributes of KERN-A (2) and KERN-B (4) in a qcow2
  qemu-nbd -r -c /dev/nbd1 "$1" && sleep 1
  python3 - /dev/nbd1 <<'PY'
import struct, sys
f = open(sys.argv[1], "rb")
f.seek(512); h = f.read(92)
lba, n, esz = struct.unpack_from("<QII", h, 72)
f.seek(lba * 512); e = f.read(n * esz)
out = []
for i, name in ((1, "A"), (3, "B")):
    a = struct.unpack_from("<Q", e, i * esz + 48)[0]
    out.append(f"KERN-{name} prio {(a >> 48) & 15} tries {(a >> 52) & 15} good {(a >> 56) & 1}")
print(", ".join(out))
PY
  qemu-nbd -d /dev/nbd1 >/dev/null
}
boot() {    # boot DISK, wait, report the GRUB entry, power off
  systemctl reset-failed "$unit" 2>/dev/null
  systemd-run --unit "$unit" --collect -E NICKEL_VMTEST_DIR="$D" \
    -E NICKEL_VMTEST_SSHPORT="$NICKEL_VMTEST_SSHPORT" -E NICKEL_VMTEST_OVERLAY="$1" \
    /usr/local/bin/nickel-vmtest "$vmimg" >/dev/null
  sleep "$2"
  echo "   GRUB: $(strings "$D/serial.log" | grep -o "Booting .local image [AB]." | tail -1)"
  nickel-vmctl shot "$D/flow/fallback-$3.png" >/dev/null
  systemctl stop "$unit"; sleep 3
  echo "   after: $(attrs "$1")"
}
modprobe nbd max_part=16 2>/dev/null

echo "== 1. update, stopped right after it is applied"
STOP_AFTER_APPLY=1 bash "$here/release-update-test.sh" | grep -E "^== |offered|applied|TIMEOUT" || exit 1
[ -f "$D/disk.qcow2" ] || { echo "no disk.qcow2"; exit 1; }
echo "   $(attrs "$D/disk.qcow2")"

echo "== 2. good: B boots and is marked successful"
rm -f "$D/good.qcow2"; qemu-img create -q -f qcow2 -b "$D/disk.qcow2" -F qcow2 "$D/good.qcow2"
boot "$D/good.qcow2" 240 good

echo "== 3. bad: ROOT-B wiped; power off after each boot"
rm -f "$D/bad.qcow2"; qemu-img create -q -f qcow2 -b "$D/disk.qcow2" -F qcow2 "$D/bad.qcow2"
qemu-nbd -c /dev/nbd1 "$D/bad.qcow2" && sleep 1
dd if=/dev/zero of=/dev/nbd1p5 bs=1M count=64 conv=fsync status=none
qemu-nbd -d /dev/nbd1 >/dev/null
for i in 1 2 3 4 5; do
  echo "-- boot $i"
  boot "$D/bad.qcow2" 100 "bad-$i"
done
echo "screenshots: $D/flow/fallback-*.png"
