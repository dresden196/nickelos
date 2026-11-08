#!/bin/bash
# Runs on nickel-build, signed in to the test VM: Linux apps' notifications
# (nickel-portal's org.freedesktop.Notifications and Notification portal,
# shown by Chrome as ChromeOS notifications, patch 0027).
#   1. the server answers GetServerInformation and GetCapabilities
#   2. a native app's notification (src/notif-client.c, Notify as libnotify
#      sends it) shows up in ChromeOS's notification center
#   3-5. what the user does with it reaches the app: a button (with an
#      activation token), the notification itself, the user closing it
#   6. a Flatpak app's (the portal backend's AddNotification): a button
# The VM's GPU does not paint ash's notification popups, so 3-6 stand in for
# Chrome's clicks by calling nickel-appd's NotificationEvent as Chrome does;
# clicking the popups themselves is checked on hardware (nickel-hwctl).
vm() { nickel-vmssh "$@" </dev/null; }
SRC=/srv/nickel/src
install -o nickel -m644 "$(dirname "$0")"/src/notif-client.c "$SRC/.nickel/"
# One line per command: cros_sdk turns the script's newlines into spaces.
B=/build/nickel
(cd "$SRC" && sudo -u nickel -H "$SRC/chromite/bin/cros_sdk" -- bash -c "
  cd /mnt/host/source/.nickel &&
  export PKG_CONFIG_SYSROOT_DIR=$B PKG_CONFIG_LIBDIR=$B/usr/lib64/pkgconfig:$B/usr/share/pkgconfig &&
  x86_64-cros-linux-gnu-clang --sysroot=$B -O2 -Wall -o notif-client notif-client.c \$(pkg-config --cflags --libs gio-2.0)") || exit 1
"$(dirname "$0")"/nickel-vmpush /usr/local/bin/notif-client \
  < "$SRC/.nickel/notif-client"

F="--dest org.freedesktop.Notifications --object-path /org/freedesktop/Notifications --method org.freedesktop.Notifications"
echo "== 1. server"
vm "nickel-session-exec gdbus call --session $F.GetServerInformation"
vm "nickel-session-exec gdbus call --session $F.GetCapabilities"

# Chrome's report of a click or close, through nickel-appd.
event() {
  vm "nickel-session-exec dbus-send --system --print-reply --dest=org.nickelos.Apps \
    /org/nickelos/Apps org.nickelos.Apps.NotificationEvent \
    string:$1 string:$2 int32:$3 string:$4 >/dev/null"
}
client() {
  vm "setsid -f sh -c 'nickel-session-exec /usr/local/bin/notif-client $1 urgent 60 > /tmp/$2.out 2>&1'"
  sleep 3
}
out() { vm "cat /tmp/$1.out" | tr '\n' ' '; echo; }

echo "== 2. a native app's notification"
client fdo n1
id=$(vm "sed -n 's/^id //p' /tmp/n1.out")
echo "notify: $(out n1)"
echo "== 3. its button (expect token, action open)"
event "fdo-$id" clicked 0 tok-button; sleep 1
echo "button: $(out n1)"
echo "== 4. the notification itself (expect token, action default)"
event "fdo-$id" clicked -1 tok-body; sleep 1
echo "click: $(out n1)"
echo "== 5. closed by the user (expect closed $id 2)"
event "fdo-$id" closed 1 ""; sleep 1
echo "closed: $(out n1)"
echo "== 6. a Flatpak app's, its button (expect portal-action ... app.open)"
client portal p1
event portal-org.gnome.Loupe-dl1 clicked 0 tok-portal; sleep 1
echo "portal: $(out p1)"
