#!/bin/bash
# Runs on nickel-build, signed in to the test VM: Linux apps see only the
# standard Wayland protocols (Chrome patch 0029), and both kinds of app still
# work.
#   1. a native app's socket (the session's wayland-0) shows the standard
#      protocols and none of Exo's own (zaura_shell, surface_augmenter,
#      overlay_prioritizer, remote shell, the other zcr_* ones)
#   2. sommelier's socket (/run/chrome/wayland-sommelier) shows them all
#   3. binding a hidden one on the native socket is a protocol error, and
#      Chrome lives on
#   4. a native GTK 3 app (Wayland) and an X11 one (GTK 3 on X11, through
#      sommelier) show their windows and take the focus; a GTK 4 window
#      draws with GL
# Build of the test programs: src/wl-globals.c here; the GTK ones as in
# native-apps-test.sh.
vm() { nickel-vmssh "$@" </dev/null; }
SRC=/srv/nickel/src
install -o nickel -m644 "$(dirname "$0")"/src/wl-globals.c "$SRC/.nickel/"
# One line per command: cros_sdk turns the script's newlines into spaces.
B=/build/nickel
(cd "$SRC" && sudo -u nickel -H "$SRC/chromite/bin/cros_sdk" -- bash -c "
  cd /mnt/host/source/.nickel &&
  export PKG_CONFIG_SYSROOT_DIR=$B PKG_CONFIG_LIBDIR=$B/usr/lib64/pkgconfig:$B/usr/share/pkgconfig &&
  x86_64-cros-linux-gnu-clang --sysroot=$B -O2 -Wall -o wl-globals wl-globals.c \$(pkg-config --cflags --libs wayland-client)") || exit 1
"$(dirname "$0")"/nickel-vmpush /usr/local/bin/wl-globals \
  < "$SRC/.nickel/wl-globals"

private='zaura_|zcr_|surface_augmenter|overlay_prioritizer'
echo "== 1. what a native app is shown"
vm 'nickel-session-exec /usr/local/bin/wl-globals' > /tmp/globals-native.txt
echo "$(wc -l < /tmp/globals-native.txt) globals: $(awk '{print $2}' /tmp/globals-native.txt | sort -u | tr '\n' ' ')"
echo "Exo's own among them: $(awk '{print $2}' /tmp/globals-native.txt | grep -E "$private" | grep -v zcr_text_input_extension_v1 | tr '\n' ' ')(none expected)"

echo "== 2. what sommelier is shown"
vm 'nickel-session-exec /usr/local/bin/wl-globals /run/chrome/wayland-sommelier' > /tmp/globals-sommelier.txt
echo "$(wc -l < /tmp/globals-sommelier.txt) globals, Exo's own: $(awk '{print $2}' /tmp/globals-sommelier.txt | grep -cE "$private")"

echo "== 3. binding a hidden one on the native socket"
# Both servers create their globals in the same order, so zaura_shell has the
# same number on the native socket as on sommelier's.
n=$(awk '$2 == "zaura_shell" {print $1}' /tmp/globals-sommelier.txt)
chrome_before=$(vm 'pgrep -o -f "^/opt/google/chrome/chrome "')
vm "nickel-session-exec /usr/local/bin/wl-globals --bind zaura_shell $n | tail -1"
chrome_after=$(vm 'pgrep -o -f "^/opt/google/chrome/chrome "')
[ "$chrome_before" = "$chrome_after" ] && echo "Chrome lives on ($chrome_after)" || echo "CHROME RESTARTED ($chrome_before -> $chrome_after)"

echo "== 4. apps"
vm 'nickel-session-exec env GDK_BACKEND=wayland timeout 20 /usr/local/bin/gtk3-focus-test 2>/dev/null | tail -1 | sed "s/^/GTK 3, Wayland: /"'
vm 'nickel-session-exec env GDK_BACKEND=x11 timeout 20 /usr/local/bin/gtk3-focus-test 2>/dev/null | tail -1 | sed "s/^/GTK 3, X11 (sommelier): /"'
vm 'nickel-session-exec timeout 12 /usr/local/bin/gtk4-gl-window 2>/dev/null | grep -E "GL_RENDERER|frames" | tr "\n" " " | sed "s/^/GTK 4, GL: /"'
echo
