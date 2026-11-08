#!/bin/bash
# Runs on a build host, as root, with tester signed in in the test VM (port
# 9222; local-account-test.sh). The desktop portals added 2026-10-12
# (docs/vision.md "Desktop portals"), each asked the way an app asks
# (portal-call, as the host app org.nickelos.PortalTest):
#   1. Email: ComposeEmail with a file reaches the default mail app (a test
#      one) as a mailto: link with the address, the subject and the file
#   2. Account: GetUserInformation, shared in ChromeOS's dialog: the login,
#      the full name and the account picture
#   3. DynamicLauncher: PrepareInstall, added in ChromeOS's dialog, then
#      Install: the launcher is in ~/.local/share/applications, and
#      nickel-appd lists it for Chrome's launcher
#   4. Wallpaper: SetWallpaperURI with the blue-teal test JPEG, allowed in
#      ChromeOS's dialog: the screen shows it; then the default again
#   5. PickColor: ChromeOS's eyedropper over a wallpaper of one colour,
#      clicked with the mouse: that colour; Esc: canceled
#   7. Clipboard of a remote desktop session: the other computer's text
#      onto ChromeOS's clipboard (SetSelection, SelectionWrite), and read
#      back from it (SelectionOwnerChanged, SelectionRead)
#   8. Location: Start through the portal (an app outside a sandbox is not
#      asked) gives GeoClue's location (the VM has no Wi-Fi: beaconDB from
#      the internet address), with nickel-appd as GeoClue's agent; GeoClue's
#      where-am-i, without the portal, too; the question a Flatpak app gets
#      (the backend's AccessDialog with the Location portal's icon) is
#      ChromeOS's location dialog, allowed with Enter
#   6. Background: RequestBackground with autostart writes the app's
#      ~/.config/autostart entry; after signing in again nickel-appd has
#      started it (it writes a file); a user entry Hidden=true is not started
# ChromeOS's dialogs are answered with Enter (allow) or Esc. The screen is
# taken through the Screenshot portal. This signs tester in again (step 6).
set -u
V=${NICKEL_VMTEST_DIR:-/srv/nickel/vmtest}
here=$(dirname "$(readlink -f "$0")")
vm() { nickel-vmssh "$@" </dev/null; }
APP=org.nickelos.PortalTest
T=/home/tester/portals-test
session() { vm 'dbus-send --system --print-reply --dest=org.chromium.SessionManager /org/chromium/SessionManager org.chromium.SessionManagerInterface.RetrieveSessionState' | sed -n 's/.*string "\(.*\)"/\1/p'; }
# Restarts the UI and signs tester in again.
relogin() {
  vm 'restart ui >/dev/null'
  for _ in $(seq 60); do [ "$(session)" = stopped ] && vm 'pgrep -o -f "^/opt/google/chrome/chrome "' >/dev/null && break; sleep 2; done
  sleep 12
  nickel-vmctl key esc; sleep 1
  nickel-vmctl type nickel-test-pw; nickel-vmctl key ret
  for _ in $(seq 60); do [ "$(session)" = started ] && vm 'test -S /run/chrome/wayland-0 && pgrep -x sommelier' >/dev/null && break; sleep 2; done
  sleep 15
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

"$here"/build-test-client.sh portal-call gio-2.0 gio-unix-2.0 || exit 1
"$here"/build-test-client.sh portal-client gio-2.0 || exit 1
"$here"/build-test-client.sh portal-rd-clipboard gio-2.0 gio-unix-2.0 || exit 1
vm "rm -rf $T; mkdir -p $T"
# xdg-desktop-portal's Registry takes an app id only with a desktop entry.
vm "mkdir -p /home/tester/.local/share/applications; printf '[Desktop Entry]\nType=Application\nName=Portal Test\nExec=true\nNoDisplay=true\n' > /home/tester/.local/share/applications/$APP.desktop; chown -R tester:tester /home/tester/.local"
tar -C "$here/media" -cf - photo.jpg icon.png solid.png | nickel-vmssh "tar -C $T -xf -"
vm "chown -R tester:tester $T"

# portal-call in the session, in the background; its output in $T/out.
call() {
  vm "rm -f $T/out; cd $T; nohup nickel-session-exec /usr/local/bin/portal-call $APP $* > $T/out 2>&1 &"
}
out() { vm "cat $T/out 2>/dev/null" | grep -v '^asked$' | tr '\n' ' '; }
# The screen as Chrome draws it, through the Screenshot portal.
capture() {   # OUT.png
  vm "rm -f /tmp/shot.out; nohup nickel-session-exec /usr/local/bin/portal-client Screenshot - - 60 > /tmp/shot.out 2>&1 &"
  sleep 6; nickel-vmctl click 505 357; sleep 2; nickel-vmctl click 888 545; sleep 8
  local uri path
  uri=$(vm 'cat /tmp/shot.out' | grep -o "file://[^ ]*" | head -1)
  path=$(python3 -c "import sys, urllib.parse; print(urllib.parse.unquote(sys.argv[1][7:]))" "$uri")
  vm "cat '$path'; rm -f '$path'" > "$1"
}
check() {   # WHAT CONDITION-TEXT GREP-PATTERN
  if out | grep -qE "$3"; then echo "$1: $2"; else echo "$1: FAILED, $(out | cut -c1-200)"; fi
}

# Steps 4 and 5 look at the screen: no browser window over it (Chrome opens
# one again after it crashed). Chrome's shelf icon brings its window up (or
# opens one, or minimizes the one in front), and Ctrl+Shift+W closes it;
# twice, for a second window.
for _ in 1 2; do
  nickel-vmctl click 555 776; sleep 3
  nickel-vmctl key ctrl-shift-w; sleep 2
done

echo "== 1. Email"
vm "mkdir -p /home/tester/.local/share/applications && cat > /home/tester/.local/share/applications/nickel-test-mail.desktop <<'EOF'
[Desktop Entry]
Type=Application
Name=Test mail
Exec=sh -c 'printf %%s \"\$1\" > $T/mail' sh %u
MimeType=x-scheme-handler/mailto;
NoDisplay=true
EOF
chown -R tester:tester /home/tester/.local/share/applications
cp -n /home/tester/.config/mimeapps.list $T/mimeapps.list.saved 2>/dev/null; true"
vm "nickel-session-exec xdg-mime default nickel-test-mail.desktop x-scheme-handler/mailto"
call Email someone@example.org "'Holiday photo'" "'Here it is.'" $T/photo.jpg
sleep 8
mail=$(vm "cat $T/mail 2>/dev/null")
echo "response: $(out | grep -o 'response [0-9]*')"
case "$mail" in
  mailto:someone@example.org\?subject=Holiday%20photo\&body=Here%20it%20is.\&attach=*photo.jpg) echo "mail app got: address, subject, body, attachment" ;;
  *) echo "mail app: FAILED, got '${mail:0:160}'" ;;
esac
vm "rm -f /home/tester/.local/share/applications/nickel-test-mail.desktop; if [ -f $T/mimeapps.list.saved ]; then cp $T/mimeapps.list.saved /home/tester/.config/mimeapps.list; else sed -i '/nickel-test-mail/d' /home/tester/.config/mimeapps.list; fi; chown tester:tester /home/tester/.config/mimeapps.list"

echo "== 2. Account"
call Account "'To greet you'"
sleep 5
nickel-vmctl key ret
sleep 4
check "Account" "login, name and picture" "response 0 .*'id': <'tester'>.*'name': <'Tester Nickel'>.*'image': <'file:///home/chronos/tester@nickel.localhost.png'>"
call Account "'Again, refused'"
sleep 5
nickel-vmctl key esc
sleep 4
check "Account refused" "canceled" "response 1"

echo "== 3. DynamicLauncher"
call Launcher "'Portal Test App'" $T/icon.png $APP.Launched.desktop "'sh -c true'"
sleep 5
nickel-vmctl key ret
sleep 6
check "Launcher" "added and installed" "response 0 .*installed"
if vm "test -L /home/tester/.local/share/applications/$APP.Launched.desktop && grep -q 'Name=Portal Test App' /home/tester/.local/share/applications/$APP.Launched.desktop"; then
  echo "  in ~/.local/share/applications"
else
  echo "  launcher file: FAILED"
fi
sleep 5
# nickel-appd puts each app it lists for Chrome's launcher in its icon
# directory, under Chrome's id for it (nickel-appd.cc's AppId).
app_id=$(python3 -c "
import hashlib, sys
h = hashlib.sha256(('crostini:nickel/host/' + sys.argv[1]).encode()).hexdigest()[:32]
print(''.join(chr(ord('a') + int(c, 16)) for c in h))" "$APP.Launched")
if vm "test -d /home/chronos/user/crostini.icons/$app_id"; then
  echo "  nickel-appd lists it for Chrome's launcher, with its icon"
else
  echo "  nickel-appd: FAILED, no icon directory $app_id"
fi
vm "nickel-session-exec gdbus call --session --dest org.freedesktop.portal.Desktop --object-path /org/freedesktop/portal/desktop --method org.freedesktop.portal.DynamicLauncher.Uninstall $APP.Launched.desktop '{}' >/dev/null 2>&1; rm -f /home/tester/.local/share/applications/$APP.Launched.desktop"

echo "== 4. Wallpaper"
vm 'cat /usr/share/chromeos-assets/wallpaper/default_small.jpg' > "$V/flow/portals-default.jpg"
call Wallpaper file://$T/photo.jpg 0
sleep 5
nickel-vmctl key ret
sleep 8
check "Wallpaper" "set" "response 0"
capture "$V/flow/portals-wallpaper.png"
# The middle of the screen: the test picture (blue-teal), not the default
# wallpaper (the small one, centred, not scaled).
python3 "$here/screen-compare.py" "$V/flow/portals-wallpaper.png" "$V/flow/portals-default.jpg" picture
vm "nickel-session-exec gdbus call --system --dest org.chromium.VmApplicationsService --object-path /org/chromium/VmApplicationsService --method org.chromium.VmApplicationsService.NickelSetWallpaper ''" | grep -q true &&
  echo "  back to the default: asked" || echo "  back to the default: FAILED"
sleep 5
capture "$V/flow/portals-default.png"
python3 "$here/screen-compare.py" "$V/flow/portals-default.png" "$V/flow/portals-default.jpg" default

echo "== 5. PickColor"
# On a wallpaper of one colour (#3a6ea5), picked with the mouse: the test
# VM's pen (the tablet nickel-vmctl clicks with) doesn't move the mouse
# pointer the eyedropper follows, so a tap picks where it opened.
setwall() { vm "nickel-session-exec gdbus call --system --dest org.chromium.VmApplicationsService --object-path /org/chromium/VmApplicationsService --method org.chromium.VmApplicationsService.NickelSetWallpaper '$1'" | grep -q true; }
setwall $T/solid.png || echo "PickColor: FAILED, no solid wallpaper"
sleep 4
vm "rm -f /tmp/shot.out; nohup nickel-session-exec /usr/local/bin/portal-client PickColor - - 60 > /tmp/shot.out 2>&1 &"
sleep 5
nickel-vmctl mdrag 640 300 642 300 >/dev/null
sleep 4
picked=$(vm 'cat /tmp/shot.out' | grep -o "color [0-9. ]*")
case "$picked" in
  "color 0.227 0.431 0.647") echo "PickColor: the wallpaper's colour, #3a6ea5" ;;
  *) echo "PickColor: FAILED, got '$picked', want #3a6ea5 (0.227 0.431 0.647)" ;;
esac
vm "rm -f /tmp/shot.out; nohup nickel-session-exec /usr/local/bin/portal-client PickColor - - 60 > /tmp/shot.out 2>&1 &"
sleep 5
nickel-vmctl key esc
sleep 3
echo "PickColor, Esc: $(vm 'cat /tmp/shot.out' | grep -o 'response [0-9]*' | sed 's/response 1/canceled/')"
setwall "" || echo "  back to the default: FAILED"
sleep 4

echo "== 7. Clipboard of a remote desktop session"
# As a remote desktop app: the other computer's text onto ChromeOS's
# clipboard, then read back from ChromeOS's clipboard.
# New text each run: the clipboard may hold an earlier run's.
clip="from the other computer $(date +%s)"
vm "rm -f $T/rd; nohup nickel-session-exec /usr/local/bin/portal-rd-clipboard $APP '$clip' > $T/rd 2>&1 &"
sleep 6
# ChromeOS asks whether the app may control the device.
nickel-vmctl key ret
sleep 12
rd=$(vm "cat $T/rd" | tr '\n' ' ')
case "$rd" in
  *"started clipboard_enabled=true"*"transfer text/plain;charset=utf-8"*"'session_is_owner': <true>"*"read: $clip"*)
    echo "Clipboard: onto ChromeOS's clipboard and back" ;;
  *) echo "Clipboard: FAILED, ${rd:0:240}" ;;
esac

echo "== 8. Location"
call Location
for _ in $(seq 45); do vm "grep -q -E '^(location|timeout|response [12])' $T/out 2>/dev/null" && break; sleep 2; done
loc=$(out)
case "$loc" in
  *"response 0"*"location "*) echo "Location: a position from GeoClue" ;;
  *) echo "Location: FAILED, ${loc:0:240}" ;;
esac
vm 'grep -q "location: allowed for apps" /var/log/messages' &&
  echo "  nickel-appd is GeoClue's agent, apps allowed" || echo "  agent: FAILED, nickel-appd did not say"
vm "nickel-session-exec timeout 40 /usr/libexec/geoclue-2.0/demos/where-am-i -t 30 2>&1" | grep -q "^Latitude:" &&
  echo "  where-am-i (GeoClue itself): a position" || echo "  where-am-i: FAILED"
vm "rm -f $T/ask; nohup nickel-session-exec gdbus call --session --timeout 60 --dest org.freedesktop.impl.portal.desktop.nickel --object-path /org/freedesktop/portal/desktop --method org.freedesktop.impl.portal.Access.AccessDialog /org/freedesktop/portal/desktop/request/1_99/nickeltest $APP '' 'Give Portal Test Access to Your Location?' 'Portal Test wants to use your location.' '' \"{'icon': <'find-location-symbolic'>, 'grant_label': <'Grant Access'>, 'deny_label': <'Deny Access'>}\" > $T/ask 2>&1 &"
sleep 5
nickel-vmshot "$V/portals-location-dialog.png" >/dev/null
nickel-vmctl key ret
sleep 3
ask=$(vm "cat $T/ask" | tr '\n' ' ')
case "$ask" in
  "(uint32 0,"*) echo "  a Flatpak app's question: ChromeOS's dialog, allowed" ;;
  *) echo "  a Flatpak app's question: FAILED, $ask" ;;
esac

echo "== 6. Background and autostart"
# A command with no spaces in its arguments: xdg-desktop-portal joins a host
# app's command line with spaces, unquoted, into the entry's Exec.
vm "printf '#!/bin/sh\\ndate > $T/autostarted\\n' > $T/autostart.sh; chmod 755 $T/autostart.sh; chown tester:tester $T/autostart.sh"
call Background "'Keeps the test running'" 1 $T/autostart.sh
sleep 6
check "Background" "allowed, autostart on" "response 0 .*'autostart': <true>"
vm "grep -q '^Exec=.*autostart.sh' /home/tester/.config/autostart/$APP.desktop" &&
  echo "  ~/.config/autostart/$APP.desktop" || echo "  autostart entry: FAILED"
vm "cat > /home/tester/.config/autostart/nickel-test-hidden.desktop <<'EOF'
[Desktop Entry]
Type=Application
Name=Hidden
Exec=sh -c 'date > $T/hidden-started'
Hidden=true
EOF
chown tester:tester /home/tester/.config/autostart/nickel-test-hidden.desktop"
relogin
sleep 10
vm "test -s $T/autostarted" && echo "after signing in again: started" || echo "after signing in again: FAILED, not started"
vm "test -e $T/hidden-started" && echo "Hidden=true: FAILED, started" || echo "Hidden=true: not started"
vm "rm -f /home/tester/.config/autostart/$APP.desktop /home/tester/.config/autostart/nickel-test-hidden.desktop /home/tester/.local/share/applications/$APP.desktop; rm -rf $T"
echo "crashes: $(vm 'ls /var/spool/crash/ 2>/dev/null | grep -c -E "xdg-desktop|nickel"')"
