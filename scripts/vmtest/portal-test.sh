#!/bin/bash
# Runs on nickel-build, in the session local-account-test.sh leaves behind
# (signed in as tester). NickelOS's portal backend (xdg-desktop-portal-nickel)
# and Chrome patch 0013:
#   1. Chrome reported the appearance to nickel-appd; the Settings portal
#      answers with it (org.freedesktop.appearance color-scheme and
#      accent-color, the gnome theme and fonts) through xdg-desktop-portal
#   2. turning on ChromeOS's dark theme (Quick Settings > Display > Dark
#      theme) changes the portal's color-scheme and sends SettingChanged;
#      Bazaar (libadwaita, Flatpak) is open to show it
#   3. turning it off again
#   4. FileChooser, asked for like an app does (portal-client): picking a
#      file in ChromeOS's file picker gives the app its file: URL (response
#      0); saving under the suggested name gives the new file's URL; Cancel
#      answers response 1
# Screen (1280x800): status tray 1230,776; in Quick Settings the arrow next
# to the brightness slider 1238,656; on its Display page "Dark theme"
# 1165,382. Screenshots go to $V/flow/portal-*.png.
set -u
V=/srv/nickel/vmtest
vm() { nickel-vmssh "$@" </dev/null; }
settle() { nickel-vmsettle "$V/flow/portal-$1.png" 20 >/dev/null 2>&1; }
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
read_one() {
  vm "nickel-session-exec gdbus call --session --dest org.freedesktop.portal.Desktop \
    --object-path /org/freedesktop/portal/desktop \
    --method org.freedesktop.portal.Settings.ReadOne $1 $2 2>&1"
}
toggle_dark() {
  nickel-vmctl key esc; sleep 1
  nickel-vmctl click 1230 776; settle "$1-qs"
  nickel-vmctl click 1238 656; settle "$1-display"
  nickel-vmctl click 1165 382; sleep 5
  nickel-vmctl key esc; nickel-vmctl key esc
}

echo "== 1. the appearance Chrome reported, through the portal"
# ChromeOS's theme follows the time of day for a new account (dark between
# sunset and sunrise), and the steps below toggle it: start from light.
if [ "$(read_one org.freedesktop.appearance color-scheme)" = "(<uint32 1>,)" ]; then
  toggle_dark 0
  sleep 5
fi
vm 'grep "nickel-appd.*appearance:" /var/log/messages | tail -1 | cut -c1-140'
for k in "org.freedesktop.appearance color-scheme" "org.freedesktop.appearance accent-color" \
         "org.gnome.desktop.interface gtk-theme" "org.gnome.desktop.interface font-name"; do
  echo "$k: $(read_one $k)"
done
vm 'pgrep -a -f xdg-desktop-portal-nickel | cut -c1-80'

echo "== 2. dark theme on"
vm 'rm -f /tmp/portal-monitor.log; nohup nickel-session-exec sh -c "exec gdbus monitor --session --dest org.freedesktop.portal.Desktop > /tmp/portal-monitor.log 2>&1" >/dev/null 2>&1 &'
vm 'nohup nickel-session-exec flatpak run io.github.kolunmi.Bazaar >/dev/null 2>&1 &'
sleep 30
settle 0-light
toggle_dark 1
sleep 5
echo "color-scheme now: $(read_one org.freedesktop.appearance color-scheme)"
vm 'grep "nickel-appd.*appearance:" /var/log/messages | tail -1 | cut -c1-140; grep -c SettingChanged /tmp/portal-monitor.log | sed "s/^/SettingChanged signals: /"; grep -m3 SettingChanged /tmp/portal-monitor.log | cut -c1-160'
nickel-vmctl click 640 300; settle 2-dark

echo "== 3. dark theme off"
toggle_dark 3
sleep 5
echo "color-scheme now: $(read_one org.freedesktop.appearance color-scheme)"
nickel-vmctl click 640 300; settle 4-light-again

echo "== 4. file picker"
# portal-client (src/portal-client.c) asks the portal like an app does and
# waits for the answer; the dialog is Chrome's Files picker. Its buttons
# (1280x800): Open/Save 1068,681, Cancel 990,681; the first file in the list
# 520,306.
"$(dirname "$0")"/build-test-client.sh portal-client gio-2.0
ask() {
  vm "nohup nickel-session-exec portal-client $1 '$2' '$3' 120 > /tmp/fc-$4.out 2>&1 &"
  sleep 20; settle "$4"
}
answer() { sleep 10; vm "cat /tmp/fc-$1.out | grep -v asked | tr '\n' ' '"; echo; }
vm 'nickel-session-exec sh -c "rm -f ~/Downloads/nickel-*; echo hello > ~/Downloads/nickel-hello.txt"'
picker_aside nickel-hello.txt
ask OpenFile "NickelOS open test" "" 5-open
pick_first_file; sleep 3; nickel-vmctl click 1068 681
echo "open a file: $(answer 5-open)"
picker_back
ask SaveFile "NickelOS save test" nickel-portal-test.txt 6-save
nickel-vmctl click 1068 681
echo "save a file: $(answer 6-save)"
ask OpenFile "NickelOS cancel test" "" 7-cancel
nickel-vmctl click 990 681
echo "cancel: $(answer 7-cancel)"
vm 'pkill -f "[g]dbus monitor --session" 2>/dev/null; true'
