#!/bin/bash
# Runs on nickel-build, signed in as tester in the test VM (Chrome 0022 and
# 0023, nickel-portal -r3). Remote control through the RemoteDesktop portal
# (docs/vision.md):
#   1. src/remote-desktop-test.c asks for keyboard, pointer and the screen as
#      AnyDesk does; ChromeOS's picker shows (the test shares the display),
#      then "Allow remote control?" (the test allows it)
#   2. the app clicks the launcher button, types "files" as keysyms and
#      presses Return: the launcher opens, searches "files" and opens the
#      top result (Settings' Files section, 2026-10-05)
# Picker (1280x800): the display 505,357, Share 888,545; the question's Yes
# 808,417. Screenshots go to $V/flow/remote-*.png.
set -u
V=/srv/nickel/vmtest
SRC=/srv/nickel/src
vm() { nickel-vmssh "$@" </dev/null; }

echo "== build the test client (SDK, board sysroot)"
install -o nickel -m644 "$(dirname "$0")"/src/remote-desktop-test.c "$SRC/.nickel/"
"$(dirname "$0")"/in-sdk.sh <<'SH' || exit 1
export PKG_CONFIG_SYSROOT_DIR=/build/nickel \
  PKG_CONFIG_LIBDIR=/build/nickel/usr/lib64/pkgconfig:/build/nickel/usr/share/pkgconfig
x86_64-cros-linux-gnu-clang --sysroot=/build/nickel -O2 -o remote-desktop-test \
  remote-desktop-test.c $(pkg-config --cflags --libs gio-2.0)
SH
K=$(mktemp); install -m600 "$SRC/chromite/ssh_keys/testing_rsa" "$K"
scp -q -i "$K" -P 9222 -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \
  -o LogLevel=ERROR "$SRC"/.nickel/remote-desktop-test root@127.0.0.1:/usr/local/bin/
rm -f "$K"

echo "== 1. ask for control and the screen"
vm 'rm -f /tmp/rd.out
  nohup nickel-session-exec /usr/local/bin/remote-desktop-test > /tmp/rd.out 2>&1 &'
sleep 12
nickel-vmctl click 505 357; sleep 2; nickel-vmctl click 888 545
sleep 6
nickel-vmsettle "$V/flow/remote-1-allow.png" 5 >/dev/null 2>&1
nickel-vmctl click 808 417

echo "== 2. click, type, Return: Files opens"
sleep 20
nickel-vmsettle "$V/flow/remote-2-files.png" 5 >/dev/null 2>&1
vm 'cat /tmp/rd.out; grep "remote desktop" /var/log/messages | tail -2 | cut -c60-200'
