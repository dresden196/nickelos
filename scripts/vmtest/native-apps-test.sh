#!/bin/bash
# Runs on nickel-build, signed in as tester in the test VM. Native (not
# Flatpak) Linux apps and the focus rules of Chrome patch 0019:
#   1. a native GTK3 app (src/gtk3-portal-test.c, cross-compiled here) gets
#      ChromeOS's file picker through the portal and the picked file's path
#   1b. a native GTK 4 + libadwaita app (src/gtk4-portal-test.c) runs on the
#      image's GTK 4 and libadwaita, reports ChromeOS's color scheme and
#      accent color, and gets the file picker and the picked file's path
#   2. an app's first window and the dialog it opens take the focus, started
#      from a shell (src/gtk3-focus-test.c prints GtkWindow:is-active)
#   3. the polkit password prompt takes the focus: the password is typed
#      without clicking into it and pkexec succeeds
# Screen (1280x800): picker's first file 518,222 in Downloads, Open 1068,681.
set -u
V=/srv/nickel/vmtest
SRC=/srv/nickel/src
vm() { nickel-vmssh "$@" </dev/null; }

echo "== build the test programs (SDK, board sysroot)"
install -o nickel -m644 "$(dirname "$0")"/src/gtk[34]-*.c "$SRC/.nickel/"
"$(dirname "$0")"/in-sdk.sh <<'SH' || exit 1
export PKG_CONFIG_SYSROOT_DIR=/build/nickel \
  PKG_CONFIG_LIBDIR=/build/nickel/usr/lib64/pkgconfig:/build/nickel/usr/share/pkgconfig
for t in gtk3-portal-test gtk3-focus-test; do
  x86_64-cros-linux-gnu-clang --sysroot=/build/nickel -O2 -o $t $t.c \
    $(pkg-config --cflags --libs gtk+-3.0) || exit 1
done
x86_64-cros-linux-gnu-clang --sysroot=/build/nickel -O2 -o gtk4-portal-test \
  gtk4-portal-test.c $(pkg-config --cflags --libs libadwaita-1) || exit 1
SH
K=$(mktemp); install -m600 "$SRC/chromite/ssh_keys/testing_rsa" "$K"
scp -q -i "$K" -P 9222 -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \
  -o LogLevel=ERROR "$SRC"/.nickel/gtk3-portal-test "$SRC"/.nickel/gtk3-focus-test \
  "$SRC"/.nickel/gtk4-portal-test root@127.0.0.1:/usr/local/bin/
rm -f "$K"

echo "== 1. native GTK3 app, portal file picker"
vm 'nickel-session-exec sh -c "echo hello > ~/Downloads/native-test.txt"'
vm 'nohup nickel-session-exec /usr/local/bin/gtk3-portal-test > /tmp/gtk3.out 2>&1 &'
sleep 15
nickel-vmctl click 518 222; sleep 3; nickel-vmctl click 1068 681; sleep 6
vm 'grep -E "theme|picked|response" /tmp/gtk3.out'

echo "== 1b. native GTK 4 + libadwaita app, portal file picker"
# The window shows first (screenshot), its file dialog 6 s later.
vm 'nohup nickel-session-exec /usr/local/bin/gtk4-portal-test > /tmp/gtk4.out 2>&1 &'
sleep 3; nickel-vmsettle "$V/flow/native-gtk4-window.png" 4 >/dev/null 2>&1
sleep 12
nickel-vmctl click 518 222; sleep 3; nickel-vmctl click 1068 681; sleep 6
vm 'cat /tmp/gtk4.out'

echo "== 2. focus: first window, then its dialog (started from a shell)"
vm 'nickel-session-exec timeout 20 /usr/local/bin/gtk3-focus-test 2>&1 | grep -E "main|dialog"'

echo "== 3. focus: the polkit password prompt"
vm 'rm -f /tmp/pk.out; nohup nickel-session-exec sh -c "pkexec /bin/true; echo pkexec=\$? > /tmp/pk.out" >/dev/null 2>&1 &'
sleep 12
nickel-vmctl type nickel-test-pw; nickel-vmctl key ret; sleep 8
vm 'cat /tmp/pk.out 2>/dev/null || echo "pkexec did not finish"'
