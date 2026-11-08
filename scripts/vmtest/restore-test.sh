#!/bin/bash
# Runs on nickel-build, with the VM at the login screen (tester's pod) and
# Loupe installed (flatpak install --system flathub org.gnome.Loupe).
# Linux app windows come back after a restart (Chrome 0018,
# docs/host-apps.md step 4):
#   1. Loupe started from the launcher is recorded by full restore: once
#      snapped to the right half, its window is in the profile's
#      "Full Restore Data" under Loupe's app id, with those bounds
#   2. after a UI restart and sign-in, Restore on the "Welcome back" dialog
#      starts Loupe through nickel-appd again, and its new window takes the
#      saved bounds (saved again under a new window id)
# Screenshots go to $V/flow/restore-*.png.
set -u
V=/srv/nickel/vmtest
PASS=nickel-test-pw
vm() { nickel-vmssh "$@" </dev/null; }
shot() { nickel-vmsettle >/dev/null 2>&1; nickel-vmctl shot "$V/flow/restore-$1.png"; }
signin() {
  nickel-vmctl click 640 445; sleep 1
  nickel-vmctl type "$PASS"; nickel-vmctl key ret
  sleep 45
}
launches() { vm 'grep -c "nickel-appd.*launched org.gnome.Loupe" /var/log/messages'; }
# Loupe's shelf app id and the bounds full restore holds for it, one line
# per saved window: "<window id> <x,y,w,h> <window state>".
saved() {
  vm 'python3 - <<EOF
import glob, json
home = glob.glob("/home/chronos/u-*/")[0]
reg = json.load(open(home + "Preferences"))["crostini"]["registry"]
ids = [k for k, v in reg.items() if v.get("desktop_file_id") == "org.gnome.Loupe"]
data = json.load(open(home + "FullRestoreData"))
for app in ids:
    for win, info in data.get(app, {}).items():
        print(win, ",".join(map(str, info.get("current_bounds", []))),
              info.get("window_state_type"))
EOF'
}

echo "== 1. launch, snap, saved"
signin
nickel-vmctl key esc; sleep 1
before=$(launches)
nickel-vmctl click 24 776; sleep 3
nickel-vmctl type "image viewer"; sleep 3
nickel-vmctl key ret
sleep 30
echo "Loupe launches through nickel-appd: $before before, $(launches) after"
nickel-vmctl key alt-bracket_right
sleep 10
shot 1-snapped
first=$(saved)
echo "saved for Loupe: ${first:-NOTHING}"

echo "== 2. restart, restore"
vm 'restart ui' >/dev/null
sleep 30
signin
shot 2-welcome
before=$(launches)
# The "Welcome back" dialog's Restore button (overview, 1280x800; "No
# thanks" is left of it, at x 310-410). The software-rendered VM often does
# not paint the dialog: the screenshot shows an empty overview, but the
# button is there.
nickel-vmctl click "${RESTORE_X:-465}" "${RESTORE_Y:-312}"
sleep 40
echo "Loupe launches through nickel-appd: $before before, $(launches) after"
vm 'grep -iE "NOTREACHED|full_restore.*(error|fail)" /var/log/chrome/chrome | tail -3'
shot 3-restored
second=$(saved)
echo "saved for Loupe now: ${second:-NOTHING}"
# Same bounds and state, under the new window's id.
[ -n "$first" ] && [ -n "$second" ] &&
  [ "${first#* }" = "${second#* }" ] && [ "${first%% *}" != "${second%% *}" ] &&
  echo "PASS: Loupe's window came back with its saved bounds and state" ||
  echo "FAIL: before '$first', after '$second'"
