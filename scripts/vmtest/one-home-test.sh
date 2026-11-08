#!/bin/bash
# Runs on nickel-build, tester signed in in the test VM (port 9222). One home
# (docs/vision.md): My files is the Linux home, /home/tester.
#   1. the home: passwd, the session environment and a login shell say
#      /home/tester, which is the vault's My files mounted for the session
#   2. the one-time move from the old home (the profile directory): Linux
#      dotfiles moved with links left behind, Chrome's own files kept
#   3. the folders: user-dirs.dirs names ~/Downloads, ~/Documents..., which
#      exist; a file a Linux app saves in ~/Documents is in Files' My files
#   4. Chrome's bridges (Chrome 0037): `nickel-open ~/Documents` opens Files
#      in Documents; ChromeOS's file picker gives an app /home/tester/...
#   5. search skips hidden folders: the launcher finds ~/Documents' probe
#      file, not the one in ~/.local
#   6. signing out unmounts /home/tester before the vault, signing in mounts
#      it again
# Screenshots go to $V/flow/onehome-*.png. Screen 1280x800; the file
# picker's Open button 1068,681, its first file 520,222.
set -u
V=/srv/nickel/vmtest
vm() { nickel-vmssh "$@" </dev/null; }
settle() { nickel-vmsettle "$V/flow/onehome-$1.png" 20 >/dev/null 2>&1; }
# The file in the picker's list: Files sometimes shows its "Create a
# shortcut for your files" banner above the list, which moves the first row
# from y 222 down to y 306 (17.11's one-home missed it). Click where it is
# with the banner, then without: with it, the second click lands on the
# banner's text; without it, the first lands on the empty list.
pick_first_file() { nickel-vmctl click 520 306; sleep 1; nickel-vmctl click 520 222; }
# Only the test's own file in Downloads while a picker is open:
# pick_first_file clicks the first row, and files earlier tests left there
# came first (17.14's one-home took native-test.txt). The others wait in a
# hidden folder, which Files doesn't show, and come back afterwards.
picker_aside() {
  vm "nickel-session-exec sh -c 'mkdir -p ~/Downloads/.picker-aside && find ~/Downloads -mindepth 1 -maxdepth 1 ! -name \".*\" ! -name \"$1\" -exec mv -t ~/Downloads/.picker-aside {} +'"
}
picker_back() {
  vm "nickel-session-exec sh -c 'mv ~/Downloads/.picker-aside/* ~/Downloads/ 2>/dev/null; rmdir ~/Downloads/.picker-aside 2>/dev/null; true'"
}
session() {
  vm 'dbus-send --system --print-reply --dest=org.chromium.SessionManager /org/chromium/SessionManager org.chromium.SessionManagerInterface.RetrieveSessionState' |
    sed -n 's/.*string "\(.*\)"/\1/p'
}
sign_in() {
  for _ in $(seq 60); do
    [ "$(session)" = stopped ] && break
    sleep 2
  done
  sleep 12
  nickel-vmctl type nickel-test-pw
  nickel-vmctl key ret
  for _ in $(seq 60); do
    [ "$(session)" = started ] && vm 'test -S /run/chrome/wayland-0' && break
    sleep 2
  done
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

echo "== 1. the home"
vm 'grep ^tester: /etc/passwd | cut -d: -f6; grep ^HOME= /run/nickel/session/env
  nickel-session-exec bash -lc "cd; pwd"
  findmnt -no SOURCE /home/tester
  [ "$(stat -c %i /home/tester)" = "$(stat -c %i /home/chronos/user/MyFiles)" ] &&
    echo "/home/tester is My files"'

echo "== 2. the move"
vm 'cd /home/chronos/user; ls -la | grep -- "->" | sed "s/.* \([^ ]* -> [^ ]*\)$/\1/" | tr "\n" " "; echo
  for f in .pki .early_prefs .variations-list.txt; do
    [ -e "$f" ] && [ ! -L "$f" ] && echo "kept: $f"
  done
  ls -l .nickel-one-home | cut -d" " -f1,3'

echo "== 3. the folders"
vm 'cat /home/tester/.config/user-dirs.dirs | tr "\n" " "; echo
  cd /home/tester && ls -d Desktop Documents Downloads Music Pictures Videos | tr "\n" " "; echo
  nickel-session-exec sh -c "echo one home > ~/Documents/onehome-probe.txt"
  cat /home/chronos/user/MyFiles/Documents/onehome-probe.txt'

echo "== 4. Chrome's bridges"
nickel-vmctl key esc
vm 'nickel-session-exec sh -c "nickel-open \$HOME/Documents" && echo "nickel-open: ok"'
sleep 5; settle 1-open-documents
nickel-vmctl key ctrl-w; sleep 2
"$(dirname "$0")"/build-test-client.sh portal-client gio-2.0
vm 'nickel-session-exec sh -c "rm -f ~/Downloads/onehome-*; echo hello > ~/Downloads/onehome-pick.txt"'
picker_aside onehome-pick.txt
vm "nohup nickel-session-exec portal-client OpenFile 'One home test' '' 120 > /tmp/fc-onehome.out 2>&1 &"
sleep 20; settle 2-picker
pick_first_file; sleep 3; nickel-vmctl click 1068 681; sleep 10
vm "grep -v asked /tmp/fc-onehome.out | tr '\n' ' '"; echo
picker_back

echo "== 5. search skips hidden folders"
vm 'nickel-session-exec sh -c "echo hidden > ~/.local/share/onehome-probe-hidden.txt"'
nickel-vmctl key meta_l; sleep 2
nickel-vmctl type onehome-probe; sleep 4; settle 3-launcher-search
nickel-vmctl key esc; nickel-vmctl key esc

echo "== 6. sign out and in"
vm 'restart ui >/dev/null'
for _ in $(seq 60); do
  [ "$(session)" = stopped ] && break
  sleep 2
done
vm 'mountpoint -q /home/tester && echo "still mounted: /home/tester" || echo "/home/tester unmounted"
  mountpoint -q /home/chronos/user && echo "still mounted: the vault" || echo "vault unmounted"
  grep -h "cryptohome.*nmount" /var/log/messages | tail -2 | cut -c1-160'
sign_in
vm 'findmnt -no SOURCE /home/tester; nickel-session-exec sh -c "cat ~/Documents/onehome-probe.txt"'
