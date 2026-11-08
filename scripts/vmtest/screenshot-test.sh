#!/bin/bash
# Runs on a build host, as root, with tester signed in in the test VM (port
# 9222; local-account-test.sh). The Screenshot portal (nickel-portal r10,
# docs/vision.md "Desktop portals"):
#   1. an app asks org.freedesktop.portal.Screenshot for a screenshot; ChromeOS's
#      screen picker shows (the same as for screen sharing); picking the screen
#      answers the app with a file: URI of a PNG in ~/Pictures/Screenshots, the
#      screen's size, showing the screen without the picker (not even fading
#      out) and without a "sharing your screen" notification (Chrome patch
#      0065)
#   2. asking again and canceling the picker answers "canceled" (1)
#   3. PickColor opens ChromeOS's eyedropper; Esc answers "canceled" (1)
#      (picking a colour: portals-test.sh)
# NICKEL_PORTAL_DEPLOY=1 first copies the board's xdg-desktop-portal-nickel and
# nickel.portal into the VM and restarts the portals (no new image needed).
set -u
V=${NICKEL_VMTEST_DIR:-/srv/nickel/vmtest}
B=/srv/nickel/src/out/build/nickel
vm() { nickel-vmssh "$@" </dev/null; }
K=$(mktemp); install -m600 /srv/nickel/src/chromite/ssh_keys/testing_rsa "$K"
cp_vm() { scp -q -i "$K" -P "${NICKEL_VMTEST_SSHPORT:-9222}" -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \
  -o LogLevel=ERROR "$1" "root@127.0.0.1:$2"; }

if [ -n "${NICKEL_PORTAL_DEPLOY:-}" ]; then
  echo "== deploy nickel-portal from the board"
  vm 'mount -o remount,rw /'
  # Under another name first: the running one can't be written over.
  cp_vm "$B/usr/libexec/xdg-desktop-portal-nickel" /usr/libexec/xdg-desktop-portal-nickel.new
  vm 'mv -f /usr/libexec/xdg-desktop-portal-nickel.new /usr/libexec/xdg-desktop-portal-nickel'
  cp_vm "$B/usr/share/xdg-desktop-portal/portals/nickel.portal" /usr/share/xdg-desktop-portal/portals/nickel.portal
  # The session's portals start again on the next call (D-Bus activation).
  # (By path: process names stop at 15 characters, "xdg-desktop-por".)
  vm 'for p in $(pgrep -u tester -f "^/usr/libexec/xdg-desktop-portal"); do kill $p; done; sleep 2'
fi
rm -f "$K"

"$(dirname "$0")"/build-test-client.sh portal-client gio-2.0 || exit 1
ask() {   # METHOD: portal-client in the session, answer in /tmp/shot.out
  vm "rm -f /tmp/shot.out; nohup nickel-session-exec /usr/local/bin/portal-client $1 - - 60 > /tmp/shot.out 2>&1 &"
}
response() { vm 'cat /tmp/shot.out | grep -v "^asked" | tr "\n" " "'; }
expect() {   # RESPONSE WANT: the response number, or FAILED with what came
  n=$(echo "$1" | grep -o "response [0-9]*" | cut -d" " -f2)
  if [ "$n" = "$2" ]; then echo "response: $2"; else echo "response: FAILED, wanted $2: $1"; fi
}

# The picture is compared with the wallpaper: no browser window in it (Chrome
# opens one again after it crashed). Chrome's shelf icon brings its window
# up (or opens one, or minimizes the one in front), and Ctrl+Shift+W closes
# it; twice, for a second window.
for _ in 1 2; do
  nickel-vmctl click 555 776; sleep 3
  nickel-vmctl key ctrl-shift-w; sleep 2
done

echo "== 1. a screenshot, the screen picked in ChromeOS's picker"
ask Screenshot
sleep 6
nickel-vmsettle "$V/flow/screenshot-picker.png" 5 >/dev/null 2>&1
nickel-vmctl click 505 357; sleep 2; nickel-vmctl click 888 545
sleep 10
r=$(response); expect "$r" 0
uri=$(echo "$r" | grep -o "file://[^ ]*" | head -1)
path=$(python3 -c "import sys, urllib.parse; print(urllib.parse.unquote(sys.argv[1][7:]))" "$uri" 2>/dev/null)
# GNOME's name for it, in the session's time zone.
if [[ $path =~ ^/home/tester/Pictures/Screenshots/Screenshot\ from\ ([0-9]{4}-[0-9]{2}-[0-9]{2})\ ([0-9]{2})-[0-9]{2}-[0-9]{2}\.png$ ]]; then
  # (Its hour now or a minute ago: the hour may just have turned.)
  hours=$(vm 'date "+%Y-%m-%d %H"; date -d "-1 min" "+%Y-%m-%d %H"')
  if grep -qx "${BASH_REMATCH[1]} ${BASH_REMATCH[2]}" <<<"$hours"; then
    echo "name: Screenshot from <date> <time>.png, local time"
  else
    echo "name: FAILED, ${path##*/} is not the VM's local time ($(head -1 <<<"$hours"))"
  fi
else
  echo "name: FAILED, $path"
fi
echo "file: $(vm "file '$path' 2>&1" | sed 's/^[^:]*: //' | cut -c1-80)"
vm "cat '$path'" > "$V/flow/screenshot-taken.png" 2>/dev/null
# Nothing but the wallpaper where the picker was (the middle) and where
# "sharing your screen" pops up (bottom right, above the shelf). The test
# VM's screen (1280x800) shows the small default wallpaper centred, not
# scaled: compared with the middle of it, or with a corner where there is
# none (a flat colour). (Not against a screendump from before: QEMU's
# leaves the shelf out.) Either default wallpaper: dark mode shows the dark
# one (Chrome 0112), and Auto switches at sunset (17.9's check ran across it).
vm 'cat /usr/share/chromeos-assets/wallpaper/default_small.jpg 2>/dev/null' > "$V/flow/screenshot-wallpaper.jpg"
vm 'cat /usr/share/chromeos-assets/wallpaper/default_dark_small.jpg 2>/dev/null' > "$V/flow/screenshot-wallpaper-dark.jpg"
python3 - "$V/flow/screenshot-taken.png" "$V/flow/screenshot-wallpaper.jpg" "$V/flow/screenshot-wallpaper-dark.jpg" <<'PY'
import os, subprocess, sys
def pnm(tool, path):   # (width, height, RGB bytes)
    data = subprocess.run([tool, path], capture_output=True, check=True).stdout
    fields, pos = [], 0
    while len(fields) < 4:   # magic, width, height, maxval; then the pixels
        while data[pos:pos + 1].isspace():
            pos += 1
        end = pos
        while not data[end:end + 1].isspace():
            end += 1
        fields.append(data[pos:end]); pos = end
    pixels = data[pos + 1:]
    if fields[0] == b"P5":
        pixels = bytes(b for v in pixels for b in (v, v, v))
    return int(fields[1]), int(fields[2]), pixels
try:
    width, height, shot = pnm("pngtopnm", sys.argv[1])
    walls = [pnm("jpegtopnm", p) for p in sys.argv[2:] if os.path.getsize(p)] or [None]
except Exception as e:
    sys.exit(f"compare: FAILED, {e}")
corner = shot[(5 * width + 5) * 3:(5 * width + 5) * 3 + 3]
wall = None
def expected(x, y):
    if not wall:
        return corner
    ww, wh, wp = wall
    i = ((y + (wh - height) // 2) * ww + x + (ww - width) // 2) * 3
    return wp[i:i + 3]
def stray(x0, y0, x1, y1):
    n = 0
    for y in range(y0, y1):
        for x in range(x0, x1):
            i = (y * width + x) * 3
            if max(abs(a - b) for a, b in zip(shot[i:i + 3], expected(x, y))) > 24:
                n += 1
    return 100 * n / ((x1 - x0) * (y1 - y0))
results = []
for wall in walls:
    results.append((stray(380, 180, 900, 580), stray(1000, 690, 1275, 745)))
middle, bottom_right = min(results)
print("compare: only the wallpaper where the picker and the notification were: ok"
      if middle < 0.5 and bottom_right < 0.5 else
      f"compare: FAILED, {middle:.1f}% of the middle, {bottom_right:.1f}% of the "
      "bottom right are not wallpaper (the picker or a notification is in it)")
PY
vm 'grep "screenshot:" /var/log/messages | tail -1 | sed -E "s/^.*\]: //; s/of (screen|window):[0-9:-]*/of the \1/" | cut -c1-140'

echo "== 2. canceled in the picker"
ask Screenshot
sleep 6
nickel-vmctl key esc
sleep 6
expect "$(response)" 1

echo "== 3. PickColor"
ask PickColor
sleep 5
nickel-vmctl key esc
sleep 3
expect "$(response)" 1
echo "crashes: $(vm 'ls /var/spool/crash/ 2>/dev/null | grep -c portal')"
