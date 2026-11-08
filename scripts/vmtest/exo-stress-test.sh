#!/bin/bash
# Runs on nickel-build, signed in to the test VM: a Linux app must never take
# the desktop down. wl-stress (src/wl-stress.c) plays one odd client per
# scenario against Exo, on the socket Linux apps get; after each, Chrome must
# be the same process. One line per scenario: how the client ended (ok, the
# protocol error Exo answered with, a lost connection), then Chrome's fate,
# and the GPU process's if it restarted. A scenario that kills Chrome gets
# the FATAL line from Chrome's log, and the VM is signed in again before the
# next one.
#   exo-stress-test.sh [SCENARIO...]   (default: all of them)
# Needs wayland-scanner on the build host (Ubuntu's libwayland-bin).
vm() { nickel-vmssh "$@" </dev/null; }
SRC=/srv/nickel/src
B=/build/nickel
GEN=$SRC/.nickel/wl-stress-gen
XML=$SRC/out/build/nickel/usr/share/wayland-protocols
PROTOCOLS="stable/xdg-shell/xdg-shell.xml stable/viewporter/viewporter.xml
  stable/presentation-time/presentation-time.xml
  unstable/linux-dmabuf/linux-dmabuf-unstable-v1.xml
  unstable/text-input/text-input-unstable-v1.xml
  unstable/relative-pointer/relative-pointer-unstable-v1.xml
  unstable/pointer-constraints/pointer-constraints-unstable-v1.xml
  unstable/idle-inhibit/idle-inhibit-unstable-v1.xml
  unstable/keyboard-shortcuts-inhibit/keyboard-shortcuts-inhibit-unstable-v1.xml
  unstable/xdg-output/xdg-output-unstable-v1.xml
  unstable/xdg-decoration/xdg-decoration-unstable-v1.xml
  unstable/pointer-gestures/pointer-gestures-unstable-v1.xml
  unstable/input-timestamps/input-timestamps-unstable-v1.xml
  staging/xdg-activation/xdg-activation-v1.xml
  staging/fractional-scale/fractional-scale-v1.xml
  staging/content-type/content-type-v1.xml
  staging/single-pixel-buffer/single-pixel-buffer-v1.xml
  staging/cursor-shape/cursor-shape-v1.xml
  unstable/tablet/tablet-unstable-v2.xml"

install -d -o nickel "$GEN"
for x in $PROTOCOLS; do
  b=$(basename "$x" .xml)
  wayland-scanner client-header "$XML/$x" "$GEN/$b-client.h" &&
    wayland-scanner private-code "$XML/$x" "$GEN/$b.c" || exit 1
done
chown -R nickel "$GEN"
install -o nickel -m644 "$(dirname "$0")"/src/wl-stress.c "$SRC/.nickel/"
# One line per command: cros_sdk turns the script's newlines into spaces.
(cd "$SRC" && sudo -u nickel -H "$SRC/chromite/bin/cros_sdk" -- bash -c "
  cd /mnt/host/source/.nickel &&
  export PKG_CONFIG_SYSROOT_DIR=$B PKG_CONFIG_LIBDIR=$B/usr/lib64/pkgconfig:$B/usr/share/pkgconfig &&
  x86_64-cros-linux-gnu-clang --sysroot=$B -O2 -Wall -Iwl-stress-gen -o wl-stress wl-stress.c wl-stress-gen/*.c \$(pkg-config --cflags --libs wayland-client)") || exit 1
"$(dirname "$0")"/nickel-vmpush /usr/local/bin/wl-stress \
  < "$SRC/.nickel/wl-stress"

chrome_pid() { vm 'pgrep -o -f "^/opt/google/chrome/chrome "'; }
gpu_pid() { vm 'pgrep -o -f "^/opt/google/chrome/chrome --type=gpu-process"'; }
session() {
  vm 'dbus-send --system --print-reply --dest=org.chromium.SessionManager /org/chromium/SessionManager org.chromium.SessionManagerInterface.RetrieveSessionState' |
    sed -n 's/.*string "\(.*\)"/\1/p'
}

# After a crash: back at the sign-in screen, sign in as tester again and wait
# for the session's Wayland sockets.
sign_in() {
  for _ in $(seq 60); do
    [ "$(session)" = stopped ] && [ -n "$(chrome_pid)" ] && break
    sleep 2
  done
  sleep 8
  nickel-vmctl type nickel-test-pw
  nickel-vmctl key ret
  for _ in $(seq 60); do
    [ "$(session)" = started ] &&
      vm 'test -S /run/chrome/wayland-0 && pgrep -x sommelier' >/dev/null && break
    sleep 2
  done
  sleep 10
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

# The CHECK/NOTREACHED line Chrome logged on its way out: the session's logs
# live in the user's home, readable again once signed in.
fatal_line() {
  vm 'grep -h -E "FATAL|Check failed|NOTREACHED|Received signal" $(ls -t /home/chronos/user/log/chrome_* /var/log/chrome/chrome_* 2>/dev/null | head -4) 2>/dev/null | tail -2' |
    sed -E 's/^\[[^]]*\] //' | cut -c1-220 | tr '\n' ' '
}

if [ "$(session)" != started ]; then
  echo "signing in first"
  sign_in
fi
scenarios=("$@")
[ ${#scenarios[@]} -gt 0 ] || mapfile -t scenarios < <(vm '/usr/local/bin/wl-stress --list')
died=0
for s in "${scenarios[@]}"; do
  chrome=$(chrome_pid)
  gpu=$(gpu_pid)
  result=$(vm "nickel-session-exec timeout 60 /usr/local/bin/wl-stress $s 2>&1 | tail -1")
  # A flood ends either way: Exo keeps up, or the client's queue overflows
  # and libwayland drops it (server-side, by design). Which one is timing,
  # the session's state after the tests before (23p and 23q's release checks
  # dropped it, 4 runs on their own didn't): both are right, so one line.
  case "$s:$result" in
  *_flood:ok | *_flood:"connection lost: Broken pipe") result="ok or dropped" ;;
  esac
  sleep 2
  line="$s: ${result:-no answer}"
  if [ "$(chrome_pid)" != "$chrome" ]; then
    died=$((died + 1))
    sign_in
    line="$line | CHROME DIED: $(fatal_line)"
  else
    line="$line | Chrome lives"
    [ "$(gpu_pid)" != "$gpu" ] && line="$line, GPU process restarted"
  fi
  echo "$line"
done
echo "== ${#scenarios[@]} scenarios, Chrome died in $died"
