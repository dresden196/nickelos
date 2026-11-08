#!/bin/bash
# Runs on nickel-build, as root (it builds the test client), with tester
# signed in in the test VM (port 9222) and Bazaar installed (a Flatpak app
# without access to the home). Files between Linux apps and ChromeOS:
#   1. files copied in a Linux app paste into Files (Chrome 0038):
#      wl-ime-test copies ~/Documents/bridge-paste.txt as text/uri-list, and
#      Ctrl+V in Files' Downloads makes a copy there
#   2. an app in a sandbox hands over only files it can see itself: in
#      Bazaar's sandbox the client copies ~/Documents/bridge-secret.txt
#      (refused) and a file in its own ~/.var/app directory (pasted)
#   3. one Trash (Chrome 0039, xdg-desktop-portal r2, nickel-session r21):
#      gio trash on the host and in a Flatpak app (both through the Trash
#      portal) and Files' own trashing all land in ~/.local/share/Trash, with
#      the paths in the home; Files' Trash lists them
#      ($V/flow/bridge-3-trash.png); its context menu restores one
#   4. Files' old trash (My files/.Trash, Downloads/.Trash) moves into it
#      with its paths rewritten (nickel-home-move)
# Screen 1280x800; the Trash entry in Files' sidebar is at 212,626.
set -u
V=/srv/nickel/vmtest
vm() { nickel-vmssh "$@" </dev/null; }
settle() { nickel-vmsettle "$V/flow/bridge-$1.png" 20 >/dev/null 2>&1; }
H=/home/tester
BAZAAR=$H/.var/app/io.github.kolunmi.Bazaar

echo "== build the test client"
"$(dirname "$0")"/ime-test.sh --build-only >/dev/null || exit 1

paste_into_downloads() {
  vm 'nickel-session-exec sh -c "nickel-open \$HOME/Downloads"'
  sleep 4
  nickel-vmctl key ctrl-v
  sleep 6
  nickel-vmctl key ctrl-w
}

vm "nickel-session-exec sh -c 'echo paste me > ~/Documents/bridge-paste.txt;
  echo secret > ~/Documents/bridge-secret.txt; rm -f ~/Downloads/bridge-*;
  mkdir -p $BAZAAR; cp /usr/local/bin/wl-ime-test $BAZAAR/;
  echo own > $BAZAAR/bridge-own.txt'"

echo "== 1. paste files from a Linux app"
vm "(nickel-session-exec wl-ime-test clipboard-set file://$H/Documents/bridge-paste.txt 20 > /tmp/clip1.out 2>&1 &)"
sleep 4
paste_into_downloads
vm "ls $H/Downloads | grep bridge-paste || echo 'FAIL: nothing pasted'"

echo "== 2. a sandboxed app's files"
vm "(nickel-session-exec flatpak run --command=$BAZAAR/wl-ime-test io.github.kolunmi.Bazaar clipboard-set file://$H/Documents/bridge-secret.txt 20 > /tmp/clip2.out 2>&1 &)"
sleep 8
paste_into_downloads
vm "ls $H/Downloads | grep -q bridge-secret && echo 'FAIL: a file the app cannot see was pasted' || echo 'refused: bridge-secret.txt'"
vm "(nickel-session-exec flatpak run --command=$BAZAAR/wl-ime-test io.github.kolunmi.Bazaar clipboard-set file://$BAZAAR/bridge-own.txt 20 > /tmp/clip3.out 2>&1 &)"
sleep 8
paste_into_downloads
vm "ls $H/Downloads | grep bridge-own || echo 'FAIL: its own file did not paste'"

echo "== 3. one Trash"
vm "nickel-session-exec sh -c 'cd ~/Documents && echo a > bridge-host.txt && echo b > bridge-flatpak.txt'"
vm "nickel-session-exec gio trash $H/Documents/bridge-host.txt && echo 'gio trash (host): ok'"
vm "nickel-session-exec flatpak run --command=gio org.gnome.Loupe trash $H/Documents/bridge-flatpak.txt && echo 'gio trash (Flatpak): ok'"
vm "grep -h '^Path=' $H/.local/share/Trash/info/bridge-*.trashinfo"
nickel-vmctl key alt-shift-m; sleep 5
nickel-vmctl click 212 626; sleep 6; settle 3-trash

echo "== 4. Files' old trash moves in"
vm "nickel-session-exec sh -c 'mkdir -p ~/.Trash/files ~/.Trash/info;
  echo old > ~/.Trash/files/bridge-old.txt;
  printf \"[Trash Info]\nPath=/Documents/bridge-old.txt\nDeletionDate=2026-10-01T10:00:00.000Z\n\" > ~/.Trash/info/bridge-old.txt.trashinfo;
  /usr/libexec/nickel-session/nickel-home-move;
  grep ^Path= ~/.local/share/Trash/info/bridge-old.txt.trashinfo;
  [ -e ~/.Trash ] && echo \"FAIL: ~/.Trash is still there\" || echo \"~/.Trash moved\"'"
