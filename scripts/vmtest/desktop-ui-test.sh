#!/bin/bash
# Runs on the build host, as root: the desktop (Chrome patch 0057, icons for
# ~/Desktop on the wallpaper's layer) in a signed-in session, from a fresh
# image (latest, or IMG=) in its own VM (NICKEL_VMTEST_DIR
# /srv/nickel/vmtest-b, ssh port 9224), with real mouse input
# (nickel-vmctl; the tablet is a pen to ChromeOS, so menus open with a long
# press) and menus by keyboard; Files read through DevTools:
#   1. setup and the account "Tester Nickel"
#   2. three items put in ~/Desktop show as icons, top to bottom, and get
#      their places saved (pref nickel.desktop.positions)
#   3. Open (right-click menu) on a folder opens Files there
#   4. Rename from the menu, in the bubble
#   5. New folder on the desktop, at the click, then named
#   6. dragging an icon moves it to another cell, saved (with the pen; 6b
#      with the mouse: the item itself follows the pointer)
#   7. Move to trash from the menu
#   8. a file dragged from Files lands on the desktop (moved, as in Files)
# Screenshots: $D/flow/desktop-*.png. The VM keeps running. CHROME_DIR= puts
# another Chrome in the VM first, to try a change without building an image:
# a directory with the stripped chrome, its *.pak and locales/en-US.pak (the
# paks go with the binary: resource IDs change between builds).
set -u
export NICKEL_VMTEST_DIR=${NICKEL_VMTEST_DIR:-/srv/nickel/vmtest-b}
export NICKEL_VMTEST_SSHPORT=${NICKEL_VMTEST_SSHPORT:-9224}
D=$NICKEL_VMTEST_DIR
here=$(dirname "$(readlink -f "$0")")
IMG=${IMG:-/srv/nickel/src/src/build/images/nickel/latest/chromiumos_qemu_image.bin}
NAME="Tester Nickel"
PASS=nickel-test-pw
vm() { nickel-vmssh "$@"; }
state() { vm "dbus-send --system --print-reply --dest=org.chromium.SessionManager /org/chromium/SessionManager org.chromium.SessionManagerInterface.RetrieveSessionState 2>/dev/null" | grep -oE 'string "[a-z]+"' | tail -1; }
wait_log() { for i in $(seq 1 "${2:-40}"); do vm "grep -qs '$1' /var/log/chrome/chrome" && return 0; sleep 2; done; echo "TIMEOUT waiting for: $1"; return 1; }
wait_state() { for i in $(seq 1 "${2:-30}"); do [ "$(state)" = "string \"$1\"" ] && return 0; sleep 3; done; echo "TIMEOUT waiting for session state $1 (now $(state))"; return 1; }
wait_ssh() { sleep 15; for i in $(seq 1 90); do timeout 15 nickel-vmssh true 2>/dev/null && return 0; sleep 4; done; echo "SSH NEVER CAME UP"; exit 1; }
shot() { sleep 2; nickel-vmctl move 1270 790; sleep 1; nickel-vmctl shot "$D/flow/desktop-$1.png"; }

mkdir -p "$D/flow"
unit=nickel-$(basename "$D")
systemctl stop "$unit" 2>/dev/null; systemctl reset-failed "$unit" 2>/dev/null
systemd-run --unit "$unit" --collect -E NICKEL_VMTEST_DIR="$D" \
  -E NICKEL_VMTEST_SSHPORT="$NICKEL_VMTEST_SSHPORT" \
  /usr/local/bin/nickel-vmtest "$IMG" >/dev/null
sleep 5
systemctl is-active -q "$unit" || { echo "the VM did not start"; journalctl -u "$unit" | tail -5; exit 1; }

tunnel=
cleanup() { [ -n "$tunnel" ] && kill "$tunnel" 2>/dev/null; }
trap cleanup EXIT
wait_ssh
if [ -n "${CHROME_DIR:-}" ]; then
  wait_log "AdvanceToScreen" 60
  vm 'mount -o remount,rw / 2>/dev/null; rm -rf /usr/local/chrome.new; mkdir -p /usr/local/chrome.new'
  tar -C "$CHROME_DIR" -cf - chrome $(cd "$CHROME_DIR" && ls *.pak) locales/en-US.pak |
    vm 'tar -C /usr/local/chrome.new -xf -'
  vm 'stop ui >/dev/null; cd /usr/local/chrome.new && for f in chrome *.pak locales/en-US.pak; do mv -f "$f" "/opt/google/chrome/$f"; done
cd /; rm -rf /usr/local/chrome.new /var/log/chrome/chrome; start ui >/dev/null'
  echo "Chrome: $CHROME_DIR"
fi

echo "== 1. setup, account $NAME"
. "$(dirname "$0")/setup-account.sh"
setup_account "$NAME" "$PASS" desktop
# NickelOS's Welcome Tour comes at a new account's first sign-in, over
# everything: no thanks (empty desktop there when it isn't up), until the
# profile says it was declined, 90 s at most.
for _ in $(seq 18); do
  nickel-vmctl click 712 594; sleep 5
  nickel-vmssh "grep -q '\"welcome_tour\":{\"v3\":{\"aborted\"' /home/chronos/user/Preferences" </dev/null && break
done
sleep 45
shot 1-session

sign_in() {
  sleep 30; nickel-vmctl click 640 400; sleep 3
  nickel-vmctl type "$PASS"; nickel-vmctl key ret
  wait_state started 40 >/dev/null; sleep 30
  # ChromeOS offers the last session's windows ("Welcome Recap"), which
  # takes the keyboard: no thanks. It comes some 35-40 s after sign-in and
  # logs nothing to wait for, so Esc every 5 s for a minute (Esc on the bare
  # desktop does nothing).
  for _ in $(seq 12); do nickel-vmctl key esc; sleep 5; done
  # A first such sign-in with nothing to restore shows the feature's
  # introduction instead, which Esc leaves: its "Got it" (empty desktop
  # when it isn't there).
  nickel-vmctl click 638 536; sleep 3
}
# Chrome with DevTools on the VM's port 9223, here on 9323.
vm 'mount -o remount,rw / 2>/dev/null
grep -q remote-debugging-port=9223 /etc/chrome_dev.conf ||
  echo --remote-debugging-port=9223 >> /etc/chrome_dev.conf
grep -q hide-crash-restore-bubble /etc/chrome_dev.conf ||
  echo --hide-crash-restore-bubble >> /etc/chrome_dev.conf'
restart_ui
sign_in
ssh -i /root/.ssh/cros_testing_rsa -p "$NICKEL_VMTEST_SSHPORT" \
  -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR \
  -N -L 9323:127.0.0.1:9223 root@127.0.0.1 &
tunnel=$!
sleep 3

# Runs JS (with the helpers below) in the page whose URL starts with $1.
js() {
  cat > /tmp/cloud-step.js <<JS
(async () => {
  const sleep = ms => new Promise(r => setTimeout(r, ms));
  const deep = (root, sel) => root.querySelector(sel) ||
      [...root.querySelectorAll('*')].reduce((hit, el) =>
          hit || (el.shadowRoot && deep(el.shadowRoot, sel)), null);
  const all = (root, sel, out = []) => {
    out.push(...root.querySelectorAll(sel));
    root.querySelectorAll('*').forEach(
        el => el.shadowRoot && all(el.shadowRoot, sel, out));
    return out;
  };
  const text = el => el ? el.innerText.replace(/\s+/g, ' ').trim() : null;
  $2
})()
JS
  # Only the value: the page's console messages come before it.
  python3 "$here/cdp-eval.py" 9323 "$1" /tmp/cloud-step.js |
    tr -d '\n' | sed -E 's/  +/ /g; s/^.*result: ?//; s/ ?console: .*$//'; echo
}
# The user's home, its Desktop, and their prefs.
U=$(vm 'ls -d /home/chronos/u-* | head -1')
DESK="$U/MyFiles/Desktop"
uid=$(vm "stat -c %u '$U/MyFiles'")
as_user() { vm "sudo -u '#$uid' sh -c \"$1\""; }
desktop() { vm "ls -1 '$DESK'" | tr '\n' ' '; }
positions() {
  # Prefs reach the file some seconds after they change.
  sleep 12
  vm "python3 -c \"import json; p = json.load(open('$U/Preferences')); print(json.dumps(p.get('nickel', {}).get('desktop', {}).get('positions', {}), sort_keys=True))\""
}
menu() {  # long press at X Y (the VM's tablet is a pen to ChromeOS: its
          # right button does nothing), then the menu's Nth item by keyboard
  nickel-vmctl hold "$1" "$2" 1.3; sleep 3
  for i in $(seq 1 "$3"); do nickel-vmctl key down; sleep 0.5; done
  nickel-vmctl key ret; sleep 4
}
# The grid: 96 x 104 cells from (12, 12); centre of column C, row R.
cx() { echo $((12 + $1 * 96 + 48)); }
cy() { echo $((12 + $2 * 104 + 52)); }
# Where an item is (its saved cell): "X Y" of its icon's centre.
at() {
  local cell
  cell=$(vm "python3 -c \"import json; p = json.load(open('$U/Preferences')); c = p['nickel']['desktop']['positions']['$1']; print(c[0], c[1])\"")
  set -- $cell
  echo "$(cx "$1" 0) $(cy 0 "$2")"
}

echo "== 2. items in ~/Desktop"
# No windows over the desktop.
for i in 1 2 3; do nickel-vmctl key ctrl-shift-w; sleep 1; done
# Made in a hidden folder and moved in at once: made one by one, a rescan
# between them placed them in the order they came (17.6's release check:
# Projects before notes.txt on one run, after it on the next).
as_user "mkdir -p '$DESK/.stage/Projects' && echo notes > '$DESK/.stage/notes.txt' && { cp /usr/share/chromeos-assets/images_100_percent/*.png '$DESK/.stage/photo.png' 2>/dev/null || head -c 100 /dev/urandom > '$DESK/.stage/photo.png'; } && mv '$DESK/.stage/Projects' '$DESK/.stage/notes.txt' '$DESK/.stage/photo.png' '$DESK/' && rmdir '$DESK/.stage'"
sleep 5
echo "desktop: $(desktop)"
shot 2-icons
echo "positions: $(positions)"
nickel-vmctl key ctrl-w; sleep 1

echo "== 3. Open on a folder"
menu $(at Projects) 1
# Files shows Home first, then the folder once it has found it.
for i in $(seq 1 10); do
  t=$(js chrome://file-manager 'await new Promise(r => setTimeout(r, 1000)); return document.title;' 2>/dev/null)
  case $t in *Projects*) break ;; esac
  sleep 3
done
echo "Files: $t"
shot 3-open
nickel-vmctl key ctrl-w; sleep 2

echo "== 4. Rename"
menu $(at notes.txt) 3
shot 4-rename-bubble
nickel-vmctl type todo; nickel-vmctl key ret; sleep 4
echo "desktop: $(desktop)"

echo "== 5. New folder"
menu 600 400 1
shot 5-new-folder
nickel-vmctl type Stuff; nickel-vmctl key ret; sleep 4
echo "desktop: $(desktop)"
echo "positions: $(positions)  (Stuff wanted at [6,3])"

echo "== 6. Drag an icon to another cell"
nickel-vmctl drag $(at photo.png) 400 300; sleep 4
shot 6-moved
echo "positions: $(positions)  (photo.png wanted at [4,2])"

echo "== 6b. Drag with the mouse"
# The mouse is placed by relative motion from the top left corner, accurate
# only nearby: drag whatever is in the first cell ([0,0]) three cells right.
# The screenshot is taken during the drag (the item under the pointer).
first=$(vm "python3 -c \"import json; p = json.load(open('$U/Preferences')); print(next(n for n, c in p['nickel']['desktop']['positions'].items() if c == [0, 0]))\"")
nickel-vmctl mdrag "$(cx 0 0)" "$(cy 0 0)" "$(cx 3 0)" "$(cy 0 0)" \
  "$D/flow/desktop-6b-mouse-drag.png" >/dev/null
sleep 3
echo "positions: $(positions)  ($first wanted off [0,0])"

echo "== 7. Move to trash"
sleep 12
menu $(at todo.txt) 4
echo "desktop: $(desktop)"
# One Trash for Files and Linux apps, in the Linux home.
home=$(vm "getent passwd $uid | cut -d: -f6")
echo "in the trash: $(vm "find '$home/.local/share/Trash/files' -maxdepth 1 -name 'todo*' 2>/dev/null | wc -l")"

echo "== 8. From Files to the desktop"
as_user "echo dragged > '$U/MyFiles/Downloads/fromfiles.txt'"
nickel-vmctl key alt-shift-m; sleep 12
# Where the file's row is on the screen: its place in the page, plus the
# window's place.
pos=$(js chrome://file-manager "
  for (let i = 0; i < 20; i++) {
    const item = all(document, 'xf-tree-item').find(i => i.label === 'Downloads');
    if (item) { item.shadowRoot.querySelector('.tree-row').click(); break; }
    await sleep(500);
  }
  let row = null;
  for (let i = 0; i < 30 && !row; i++) {
    row = all(document, '.entry-name').find(e => e.innerText.trim() === 'fromfiles.txt');
    if (!row) await sleep(500);
  }
  if (!row) return 'none';
  const r = row.getBoundingClientRect();
  const top = window.outerHeight - window.innerHeight;
  return Math.round(window.screenX + r.left + r.width / 2) + ' ' +
         Math.round(window.screenY + top + r.top + r.height / 2);" | tr -d '"')
echo "row at: $pos"
if [ "$pos" != none ] && [ -n "$pos" ]; then
  # To the desktop left of the window (column 0, row 5).
  nickel-vmctl drag $pos "$(cx 0 5)" "$(cy 0 5)" 1.5; sleep 6
fi
shot 8-dropped
echo "desktop: $(desktop)"
echo "downloads: $(vm "ls -1 '$U/MyFiles/Downloads'" | tr '\n' ' ')"
echo "positions: $(positions)"

echo "crashes: $(vm 'ls /var/spool/crash/ 2>/dev/null | grep -ciE "chrome|nickel"')"
echo "screenshots in $D/flow/desktop-*.png"
