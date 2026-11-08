#!/bin/bash
# Runs on nickel-next, as root: a release image updating itself from the
# real update server (docs/updates.md), the way a NickelOS PC out in the
# world would, with nobody asking:
#   1. publish IMG_NEXT (a base image, version N+1) on stable and push it
#      to update.nickelos.dev (push-update-root)
#   2. boot IMG_N (a base image, version N, stable) in its own VM. Its VM
#      copy is made here: image_to_vm.sh (the full-size ROOT-B an update
#      needs; a raw base image has the USB layout with a 2 MB one), then
#      cros_debug taken back out of its grub.cfg (image_to_vm adds it, and
#      with it update_engine would run as on a test image). Base images
#      have no ssh, so it is driven by keyboard and mouse (QMP) and watched
#      through screenshots and the server's log
#   3. set it up with an account: update_engine checks nothing until setup
#      is done ("OOBE not completed, blocking update checks")
#   4. wait for update_engine's own periodic check (7 minutes after boot,
#      give or take 10): over HTTPS, trusting only its own CA list (ISRG
#      roots from nickel-update), the payload verified against the image's
#      key. The server's log shows the check, the download and "applied"
#   5. power the VM off and on (a QMP system_reset left this VM's firmware
#      dead, with nothing on screen or serial; a real power cycle boots):
#      GRUB takes the new system (B), the login screen shows N+1, and its
#      own next check reports N+1
# The payload stays on the server's stable channel afterwards only with
# KEEP=1; otherwise it is pulled and deleted again at the end (the server
# offers nothing until there is a real release, and this payload is signed
# with the test key).
#
#   IMG_N=.../chromiumos_base_image.bin IMG_NEXT=.../chromiumos_base_image.bin \
#     release-update-test.sh
#
# The VM's writes are kept in $D/disk.qcow2 (the update must survive the
# power cycle). DIAG=1 adds a test-only job to the VM copy that copies the
# logs every minute to the unencrypted stateful partition; read them
# afterwards with qemu-nbd on a throwaway overlay of disk.qcow2, e2fsck,
# and mount -o ro,noload (the journal isn't replayed otherwise, and the host
# kernel can't mount it read-write: its quota feature).
# The base images must be built for tests (NICKEL_TEST_KEY=1
# nickel-build-stock): they then carry the test payload key, which this
# test signs with; release images carry the production key, whose private
# half is only with Dresden.
set -u
export NICKEL_VMTEST_DIR=/srv/nickel/vmtest-r NICKEL_VMTEST_SSHPORT=9226
D=$NICKEL_VMTEST_DIR
here=$(dirname "$(readlink -f "$0")")
update=$here/../update
IMG_N=${IMG_N:?IMG_N: the base image to start from}
IMG_NEXT=${IMG_NEXT:?IMG_NEXT: the base image to update to}
ROOT=${ROOT:-/srv/nickel/update}
SERVER=root@<internal address>
NAME="Tester Nickel"
PASS=nickel-test-pw
shot() { nickel-vmctl shot "$D/flow/release-$1.png" >/dev/null; echo "screenshot: $D/flow/release-$1.png"; }
version_of() {
  local l m v
  l=$(losetup -f --show -P -r "$1"); m=$(mktemp -d)
  mount -o ro "${l}p3" "$m"; v=$(sed -n 's/^CHROMEOS_RELEASE_VERSION=//p' "$m/etc/lsb-release")
  umount "$m"; rmdir "$m"; losetup -d "$l"; echo "$v"
}
# The server's log since this test started, this VM's lines (through the
# colo's NAT the VM is <internal address>).
server_log() { ssh "$SERVER" "journalctl -u nickel-update-server --no-pager -o cat --since '$since'" | grep <internal address>; }
wait_server() {
  for i in $(seq 1 "$2"); do server_log | grep -q -E "$1" && return 0; sleep 20; done
  echo "TIMEOUT waiting for the server to see: $1"; return 1
}
mkdir -p "$D/flow"
from=$(version_of "$IMG_N"); to=$(version_of "$IMG_NEXT")
echo "== $from -> $to"

echo "== 1. publish $to on stable, push to update.nickelos.dev"
if [ ! -f "$ROOT/channels/stable/nickelos-$to-stable-full.json" ]; then
  "$update/nickel-update-publish" "$IMG_NEXT" stable "$ROOT" || exit 1
fi
"$update/deploy/push-update-root" "$ROOT" "$SERVER" || exit 1
"$update/nickel-update-channels" list "$ROOT"
since=$(ssh "$SERVER" "date '+%Y-%m-%d %H:%M:%S'")

echo "== 2. the VM copy of $from, then boot it"
vmimg=$(dirname "$IMG_N")/chromiumos_qemu_image.bin
if [ ! -f "$vmimg" ] || [ "$vmimg" -ot "$IMG_N" ]; then
  # image_to_vm reads chromiumos_image.bin; cgpt won't open a symlink.
  dir=$(dirname "$IMG_N")
  sudo -u nickel ln -f "$IMG_N" "$dir/chromiumos_image.bin"
  sudo -u nickel -H env PATH=/home/nickel/depot_tools:/srv/nickel/src/chromite/bin:/usr/bin:/bin \
    bash -c "cd /srv/nickel/src && cros_sdk -- ./image_to_vm.sh --board=nickel \
      --from=/mnt/host/source/${dir#/srv/nickel/src/} --adjust_part=STATE:=8GiB" >/dev/null 2>&1
  rm -f "$dir/chromiumos_image.bin"
  [ -f "$vmimg" ] || { echo "image_to_vm failed"; exit 1; }
fi
l=$(losetup -f --show -P "$vmimg"); m=$(mktemp -d)
mount "${l}p12" "$m" && sed -i 's/ cros_debug//g' "$m/efi/boot/grub.cfg" &&
  echo "cros_debug left in its grub.cfg: $(grep -c cros_debug "$m/efi/boot/grub.cfg")"
umount "$m"
if [ -n "${DIAG:-}" ]; then
  mount "${l}p3" "$m" && cp "$here/release-update-diag.conf" "$m/etc/init/nickel-diag.conf" && umount "$m"
fi
rmdir "$m"; losetup -d "$l"
IMG_N=$vmimg
unit=nickel-$(basename "$D")
systemctl stop "$unit" 2>/dev/null; systemctl reset-failed "$unit" 2>/dev/null
rm -f "$D/disk.qcow2"
boot() {
  systemctl reset-failed "$unit" 2>/dev/null
  systemd-run --unit "$unit" --collect -E NICKEL_VMTEST_DIR="$D" \
    -E NICKEL_VMTEST_SSHPORT="$NICKEL_VMTEST_SSHPORT" \
    -E NICKEL_VMTEST_OVERLAY="$D/disk.qcow2" \
    /usr/local/bin/nickel-vmtest "$IMG_N" >/dev/null
}
boot
# Setup isn't ready for clicks until well after it shows (a click at 70 s
# went nowhere).
sleep 110
shot 2-welcome

echo "== 3. setup and the account \"$NAME\""
# Left alone, the welcome screen offers the screen reader in a dialog, and
# a space typed while it shows turns ChromeVox on (the account name has
# one). Escape closes it; nothing else on that screen reacts to Escape.
nickel-vmctl key esc; sleep 2
nickel-vmctl click 214 649; sleep 40
shot 3a-setup
# "How should NickelOS work for you?": the default, then Next.
nickel-vmctl click 1077 647; sleep 15
nickel-vmctl click 850 227; sleep 1
nickel-vmctl type "$NAME"; sleep 1
nickel-vmctl key tab; sleep 0.5; nickel-vmctl key tab; sleep 0.5
nickel-vmctl type "$PASS"; sleep 1
nickel-vmctl key tab; sleep 0.5
nickel-vmctl type "$PASS"; sleep 8
shot 3b-account
nickel-vmctl click 1077 647; sleep 60
shot 3c-desktop
# Does the session draw? (The launcher opens over whatever is there.)
nickel-vmctl key meta_l; sleep 5
shot 3d-launcher
nickel-vmctl key esc

echo "== 4. update_engine's own check (up to 25 minutes)"
wait_server "version $from stable-channel: .*update to $to" 75 && echo "offered $to"
wait_server "version $from stable-channel: event 13/1" 30 && echo "download started"
wait_server "version $from stable-channel: event 14/1" 60 && echo "download finished"
wait_server "version $from stable-channel: event 3/1" 30 && echo "applied"
shot 4-applied
if [ -n "${STOP_AFTER_APPLY:-}" ]; then
  # For boot-fallback-test.sh: the disk as it is right after an update,
  # before any restart ($D/disk.qcow2).
  systemctl stop "$unit"
  echo "== stopped after the update was applied: $D/disk.qcow2"
  "$update/nickel-update-channels" pull "$to" stable "$ROOT" >/dev/null
  "$update/deploy/push-update-root" "$ROOT" "$SERVER"
  ssh "$SERVER" "rm -f /srv/nickel-update/payloads/nickelos-$to-stable-full.bin"
  exit 0
fi

echo "== 5. power off and on"
systemctl stop "$unit"; sleep 5
boot
sleep 120
shot 5a-after-restart
echo "boot entry: $(strings "$D/serial.log" | grep -o "Booting .local image [AB]." | tail -1)"
wait_server "version $to stable-channel" 90 && echo "back on $to: its own check reports it"
shot 5b-back

echo "== the server saw"
server_log | sed -E 's/^[0-9-]+ [0-9:,]+ //' | cut -c1-150 | uniq -c
echo "== payload downloads (nginx)"
ssh "$SERVER" "grep 'GET /payloads/nickelos-$to-stable-full.bin' /var/log/nginx/update.nickelos.dev.access.log | tail -5" | cut -c1-160

if [ -z "${KEEP:-}" ]; then
  "$update/nickel-update-channels" pull "$to" stable "$ROOT" >/dev/null
  "$update/deploy/push-update-root" "$ROOT" "$SERVER"
  ssh "$SERVER" "rm -f /srv/nickel-update/payloads/nickelos-$to-stable-full.bin"
  echo "== $to taken off stable again; the server offers: $(curl -s -X POST --data '<request protocol="3.0"><app appid="{84A28531-7D33-44D5-AAA1-6CFA2BCF8652}" version="0.0.0.0" track="stable-channel"><updatecheck/></app></request>' https://update.nickelos.dev/update | grep -o 'updatecheck status="[a-z]*"')"
fi
systemctl stop "$unit"
