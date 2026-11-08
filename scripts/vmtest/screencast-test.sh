#!/bin/bash
# Runs on nickel-build, signed in as tester in the test VM (nickel-pipewire
# running, Chrome 0022). Screen sharing through the ScreenCast portal
# (docs/vision.md):
#   1. src/screencast-test.c asks for the screen as an app does
#      (CreateSession, SelectSources, Start, OpenPipeWireRemote); ChromeOS's
#      "share your screen" picker shows, the test picks the display and
#      presses Share
#   2. the app gets frames over PipeWire: frames=10 at the screen's size,
#      lit (not black); the last frame is $V/flow/screencast-frame.ppm
# Picker (1280x800): the display 505,357, Share 888,545.
set -u
V=/srv/nickel/vmtest
SRC=/srv/nickel/src
vm() { nickel-vmssh "$@" </dev/null; }

echo "== build the test client (SDK, board sysroot)"
install -o nickel -m644 "$(dirname "$0")"/src/screencast-test.c "$SRC/.nickel/"
"$(dirname "$0")"/in-sdk.sh <<'SH' || exit 1
export PKG_CONFIG_SYSROOT_DIR=/build/nickel \
  PKG_CONFIG_LIBDIR=/build/nickel/usr/lib64/pkgconfig:/build/nickel/usr/share/pkgconfig
x86_64-cros-linux-gnu-clang --sysroot=/build/nickel -O2 -o screencast-test \
  screencast-test.c $(pkg-config --cflags --libs gio-unix-2.0 libpipewire-0.3)
SH
K=$(mktemp); install -m600 "$SRC/chromite/ssh_keys/testing_rsa" "$K"
scp -q -i "$K" -P 9222 -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \
  -o LogLevel=ERROR "$SRC"/.nickel/screencast-test root@127.0.0.1:/usr/local/bin/
rm -f "$K"

echo "== 1. ask for the screen; pick it in ChromeOS's picker"
vm 'status nickel-pipewire; rm -f /tmp/cast.out /tmp/screencast-frame.ppm
  nohup nickel-session-exec /usr/local/bin/screencast-test > /tmp/cast.out 2>&1 &'
sleep 12
nickel-vmsettle "$V/flow/screencast-picker.png" 5 >/dev/null 2>&1
nickel-vmctl click 505 357; sleep 2; nickel-vmctl click 888 545
sleep 25

echo "== 2. frames over PipeWire"
vm 'cat /tmp/cast.out; grep screencast /var/log/messages | tail -3 | cut -c60-200'
vm 'cat /tmp/screencast-frame.ppm' > "$V/flow/screencast-frame.ppm"
