#!/bin/bash
# Runs on the build host, as root: the device's other drives in a signed-in
# session, from a fresh image (latest, or IMG=) with other-os-disks.sh's two
# disks as internal SATA disks, in its own VM (NICKEL_VMTEST_DIR
# /srv/nickel/vmtest-b, ssh port 9224):
#   1. setup and the account "Tester Nickel" (as local-account-test.sh)
#   2. at sign-in Chrome mounts Windows read-only and Data read-write, and
#      nothing of Windows' own partitions
#   3. Settings > Device > Storage management lists both under "Other
#      drives" (screenshot)
#   4. the switches, clicked through Chrome's DevTools (cdp-eval.py; the VM
#      renders in software and misses quick clicks): "Allow changes" asks
#      first and mounts Windows again writable, Cancel changes nothing, off
#      mounts it read-only again; "Show in Files" off unmounts Data, on
#      mounts it again; after signing in again, Windows (not allowed) is
#      read-only and Data (not shown) not mounted
# Screenshots: $D/flow/drives-*.png. The VM keeps running.
set -u
export NICKEL_VMTEST_DIR=${NICKEL_VMTEST_DIR:-/srv/nickel/vmtest-b}
export NICKEL_VMTEST_SSHPORT=${NICKEL_VMTEST_SSHPORT:-9224}
D=$NICKEL_VMTEST_DIR
V=$D
here=$(dirname "$(readlink -f "$0")")
IMG=${IMG:-/srv/nickel/src/src/build/images/nickel/latest/chromiumos_qemu_image.bin}
NAME="Tester Nickel"
PASS=nickel-test-pw
vm() { nickel-vmssh "$@"; }
state() { vm "dbus-send --system --print-reply --dest=org.chromium.SessionManager /org/chromium/SessionManager org.chromium.SessionManagerInterface.RetrieveSessionState 2>/dev/null" | grep -oE 'string "[a-z]+"' | tail -1; }
wait_log() { for i in $(seq 1 "${2:-40}"); do vm "grep -qs '$1' /var/log/chrome/chrome" && return 0; sleep 2; done; echo "TIMEOUT waiting for: $1"; return 1; }
wait_state() { for i in $(seq 1 "${2:-30}"); do [ "$(state)" = "string \"$1\"" ] && return 0; sleep 3; done; echo "TIMEOUT waiting for session state $1 (now $(state))"; return 1; }
wait_ssh() { sleep 15; for i in $(seq 1 90); do timeout 15 nickel-vmssh true 2>/dev/null && return 0; sleep 4; done; echo "SSH NEVER CAME UP"; exit 1; }
shot() { nickel-vmctl click 1270 300; sleep 4; nickel-vmctl shot "$D/flow/drives-$1.png"; }

mkdir -p "$D/flow"
unit=nickel-$(basename "$D")
systemctl stop "$unit" 2>/dev/null; systemctl reset-failed "$unit" 2>/dev/null
"$here/other-os-disks.sh" >/dev/null
systemd-run --unit "$unit" --collect -E NICKEL_VMTEST_DIR="$D" \
  -E NICKEL_VMTEST_SSHPORT="$NICKEL_VMTEST_SSHPORT" \
  -E NICKEL_VMTEST_TARGETS="1 1" -E NICKEL_VMTEST_TARGETS_KEEP=1 \
  /usr/local/bin/nickel-vmtest "$IMG" >/dev/null
sleep 5
systemctl is-active -q "$unit" || { echo "the VM did not start"; journalctl -u "$unit" | tail -5; exit 1; }
wait_ssh

echo "== 1. setup, account $NAME"
. "$(dirname "$0")/setup-account.sh"
setup_account "$NAME" "$PASS" drives
# NickelOS's Welcome Tour comes at a new account's first sign-in, over
# everything: no thanks (empty desktop there when it isn't up), until the
# profile says it was declined, 90 s at most.
for _ in $(seq 18); do
  nickel-vmctl click 712 594; sleep 5
  nickel-vmssh "grep -q '\"welcome_tour\":{\"v3\":{\"aborted\"' /home/chronos/user/Preferences" </dev/null && break
done
sleep 45
shot 1-session

echo "== 2. what Chrome mounted"
vm 'for dev in /dev/sda1 /dev/sda2 /dev/sda3 /dev/sda4 /dev/sdb1; do
  m=$(awk -v d="$dev" "\$1 == d {print \$2, substr(\$4, 1, 2)}" /proc/mounts | head -1)
  echo "$dev: ${m:-not mounted}"
done'

echo "== 3. Settings > Device > Storage management"
nickel-vmctl key ctrl-n; sleep 8
nickel-vmctl type "chrome://os-settings/storage"; sleep 1
nickel-vmctl key ret; sleep 15
shot 2-storage
echo "screenshots in $D/flow/drives-*.png"

echo "== 4. the switches"
mounted() {
  local m
  m=$(vm "awk -v d=$1 '\$1 == d {print substr(\$4, 1, 2)}' /proc/mounts | head -1")
  echo "${m:-not mounted}"
}
# After each UI restart (setup-account.sh: retried, then the Welcome Recap
# and its introduction put away).
sign_in() { sign_in_again "$PASS"; }
open_storage() {
  # Through DevTools: no keyboard focus needed (cdp-eval.py --open).
  python3 "$here/cdp-eval.py" 9323 --open chrome://os-settings/storage
  sleep 5
}
# The page's switches in order: Windows' "Show in Files" and "Allow
# changes", then Data's "Show in Files".
ui() {
  cat > /tmp/drives-step.js <<JS
(async () => {
  const deep = (root, sel) => root.querySelector(sel) ||
      [...root.querySelectorAll('*')].reduce((hit, el) =>
          hit || (el.shadowRoot && deep(el.shadowRoot, sel)), null);
  const sleep = ms => new Promise(r => setTimeout(r, ms));
  let s = null;
  for (let i = 0; i < 30 && !s; i++) {
    s = deep(document, 'settings-storage');
    if (!s) await sleep(500);
  }
  await sleep(1000);
  const toggles = () => [...s.shadowRoot.querySelectorAll('cr-toggle')];
  const dialog = s.shadowRoot.querySelector('#nickelDriveDialog');
  const pref = n => [...s.getPref(n).value];
  $1
})()
JS
  python3 "$here/cdp-eval.py" 9323 chrome://os-settings/storage /tmp/drives-step.js |
    tr -d '\n' | sed -E 's/ +//g; s/^result://'; echo
}
vm 'mount -o remount,rw / 2>/dev/null
grep -q remote-debugging-port=9223 /etc/chrome_dev.conf ||
  echo --remote-debugging-port=9223 >> /etc/chrome_dev.conf
grep -q hide-crash-restore-bubble /etc/chrome_dev.conf ||
  echo --hide-crash-restore-bubble >> /etc/chrome_dev.conf'
restart_ui
sign_in
# DevTools of the VM's Chrome on port 9323 here.
ssh -i /root/.ssh/cros_testing_rsa -p "$NICKEL_VMTEST_SSHPORT" \
  -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR \
  -N -L 9323:127.0.0.1:9223 root@127.0.0.1 &
tunnel=$!
sleep 3
open_storage
echo "Allow changes, confirmed: $(ui "toggles()[1].click(); await sleep(1000);
  const asked = dialog.open; dialog.querySelector('.action-button').click();
  await sleep(1000);
  return {asked, writable: pref('nickel.windows_drives_writable').length};")"
sleep 10; echo "  Windows: $(mounted /dev/sda3)"
echo "Allow changes off: $(ui "toggles()[1].click(); await sleep(1000);
  return {asked: dialog.open,
          writable: pref('nickel.windows_drives_writable').length};")"
sleep 10; echo "  Windows: $(mounted /dev/sda3)"
echo "Allow changes, cancelled: $(ui "toggles()[1].click(); await sleep(1000);
  const asked = dialog.open; dialog.querySelector('.cancel-button').click();
  await sleep(1000);
  return {asked, checked: toggles()[1].checked,
          writable: pref('nickel.windows_drives_writable').length};")"
sleep 5; echo "  Windows: $(mounted /dev/sda3)"
echo "Data not shown: $(ui "toggles()[2].click(); await sleep(1000);
  return {notShown: pref('nickel.drives_not_shown').length};")"
sleep 10; echo "  Data: $(mounted /dev/sdb1)"
echo "Data shown: $(ui "toggles()[2].click(); await sleep(1000);
  return {notShown: pref('nickel.drives_not_shown').length};")"
sleep 10; echo "  Data: $(mounted /dev/sdb1)"
echo "Data not shown, then signed in again: $(ui "toggles()[2].click();
  await sleep(1000); return {notShown: pref('nickel.drives_not_shown').length};")"
kill "$tunnel" 2>/dev/null
restart_ui
sign_in
echo "  Windows: $(mounted /dev/sda3), Data: $(mounted /dev/sdb1)"
echo "crashes: $(vm 'ls /var/spool/crash/ 2>/dev/null | grep -ciE "chrome|cros_disks"')"
