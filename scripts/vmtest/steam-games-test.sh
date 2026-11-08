#!/bin/bash
# Runs on a build host, as root, with tester signed in in the test VM
# (local-account-test.sh). Steam's games as apps (nickel-appd's steam.cc):
# a Steam library laid out as Steam's Flatpak keeps it, made up here (no
# Steam in the VM: its data is what nickel-appd reads):
#   1. an installed game gets ~/.local/share/applications/steam_app_<id>.desktop
#      (its name, Steam's icon for it, the Flatpak's command for
#      steam://rungameid/<id>, StartupWMClass steam_app_<id>); Proton and a
#      game still downloading get none; Chrome's app registry has the game
#   2. a game installed while signed in comes, a game removed goes
#   3. a window with Proton's class (steam_app_<id>) is on the shelf with
#      the game's icon (a magenta one here), not the generic one
#   4. a desktop file Steam wrote itself (Steam from Portage) for the game:
#      nickel-appd's goes, and Steam's has the window class in the registry
# NICKEL_APPD=FILE first copies that nickel-appd into the VM and restarts it.
set -u
V=${NICKEL_VMTEST_DIR:-/srv/nickel/vmtest}
here=$(dirname "$(readlink -f "$0")")
vm() { nickel-vmssh "$@" </dev/null; }
H=/home/tester
S=$H/.var/app/com.valvesoftware.Steam/.local/share
A=$H/.local/share/applications
GAME=1091500

if [ -n "${NICKEL_APPD:-}" ]; then
  vm 'mount -o remount,rw /'
  nickel-vmssh "cat > /usr/bin/nickel-appd.new; chmod 755 /usr/bin/nickel-appd.new; mv -f /usr/bin/nickel-appd.new /usr/bin/nickel-appd" < "$NICKEL_APPD"
fi
"$here"/build-test-client.sh x-class-window xcb || exit 1
"$here"/build-test-client.sh portal-client gio-2.0 || exit 1

# The registry Chrome keeps the Linux apps in (written within seconds).
registry() {   # DESKTOP_FILE_ID: "name|startup_wm_class", or nothing
  vm "python3 -c '
import json, sys
apps = json.load(open(\"/home/chronos/user/Preferences\")).get(\"crostini\", {}).get(\"registry\", {})
for app in apps.values():
    if app.get(\"desktop_file_id\") == sys.argv[1]:
        print(app.get(\"name\", {}).get(\"\", \"\") + \"|\" + app.get(\"startup_wm_class\", \"\"))
' \"$1\""
}
wait_registry() {   # DESKTOP_FILE_ID WANT
  for _ in $(seq 20); do [ "$(registry "$1")" = "$2" ] && return 0; sleep 3; done
  return 1
}
manifest() {   # ID NAME FLAGS
  vm "printf '\"AppState\"\n{\n\t\"appid\"\t\t\"$1\"\n\t\"name\"\t\t\"$2\"\n\t\"StateFlags\"\t\t\"$3\"\n\t\"SizeOnDisk\"\t\t\"123456789\"\n}\n' > $S/Steam/steamapps/appmanifest_$1.acf; chown tester:tester $S/Steam/steamapps/appmanifest_$1.acf"
}

echo "== 1. a game Steam has installed"
vm "rm -rf $H/.var/app/com.valvesoftware.Steam $A/steam_app_*.desktop; mkdir -p $S/Steam/steamapps $S/icons/hicolor/256x256/apps"
vm "printf '\"libraryfolders\"\n{\n\t\"0\"\n\t{\n\t\t\"path\"\t\t\"$S/Steam\"\n\t}\n}\n' > $S/Steam/steamapps/libraryfolders.vdf"
nickel-vmssh "cat > $S/icons/hicolor/256x256/apps/steam_icon_$GAME.png" < "$here/media/magenta.png"
vm "chown -R tester:tester $H/.var"
manifest $GAME "Test Game" 4
manifest 1493710 "Proton Experimental" 4
manifest 945360 "Still Downloading" 1026
vm 'restart nickel-appd >/dev/null'
sleep 8
f=$A/steam_app_$GAME.desktop
vm "grep -E '^(Name|Exec|Icon|StartupWMClass)=' $f" | sed "s|$S|<steam data>|"
vm "ls $A | grep -c '^steam_app_'" | sed 's/^/desktop files written: /'
wait_registry steam_app_$GAME "Test Game|steam_app_$GAME" &&
  echo "registry: Test Game, window class steam_app_$GAME" ||
  echo "registry: FAILED, '$(registry steam_app_$GAME)'"

echo "== 2. installed and removed while signed in"
manifest 440 "Second Game" 4
sleep 8
vm "test -f $A/steam_app_440.desktop" && echo "installed: its desktop file came" || echo "installed: FAILED"
vm "rm -f $S/Steam/steamapps/appmanifest_440.acf"
sleep 8
vm "test -f $A/steam_app_440.desktop" && echo "removed: FAILED, still there" || echo "removed: its desktop file went"

echo "== 3. the game's window on the shelf"
vm "nohup nickel-session-exec /usr/local/bin/x-class-window steam_app_$GAME 'Test Game' 90 >/dev/null 2>&1 &"
sleep 10
vm "rm -f /tmp/shot.out; nohup nickel-session-exec /usr/local/bin/portal-client Screenshot - - 60 > /tmp/shot.out 2>&1 &"
sleep 6; nickel-vmctl click 505 357; sleep 2; nickel-vmctl click 888 545; sleep 8
uri=$(vm 'cat /tmp/shot.out' | grep -o "file://[^ ]*" | head -1)
path=$(python3 -c "import sys, urllib.parse; print(urllib.parse.unquote(sys.argv[1][7:]))" "$uri")
vm "cat '$path'; rm -f '$path'" > "$V/flow/steam-games-shelf.png"
python3 - "$V/flow/steam-games-shelf.png" <<'PY'
import subprocess, sys
data = subprocess.run(["pngtopnm", sys.argv[1]], capture_output=True).stdout
fields, pos = [], 0
while len(fields) < 4:
    while data[pos:pos + 1].isspace(): pos += 1
    end = pos
    while not data[end:end + 1].isspace(): end += 1
    fields.append(data[pos:end]); pos = end
w, h, px = int(fields[1]), int(fields[2]), data[pos + 1:]
n = 0
for y in range(h - 48, h):
    for x in range(w):
        r, g, b = px[(y * w + x) * 3:(y * w + x) * 3 + 3]
        n += r > 200 and g < 60 and b > 200
print("shelf: the game's icon" if n > 200 else f"shelf: FAILED, {n} magenta pixels on the shelf")
PY
vm 'pkill -f "^/usr/local/bin/x-class-window"'

echo "== 4. Steam's own desktop file for the game (Steam from Portage)"
# GLib leaves out desktop files whose program is not installed: a steam
# that does nothing stands in for Portage's.
vm "printf '#!/bin/sh\n' > /usr/local/bin/steam; chmod 755 /usr/local/bin/steam"
vm "printf '[Desktop Entry]\nName=Test Game\nExec=steam steam://rungameid/$GAME\nIcon=steam_icon_$GAME\nType=Application\n' > '$A/Test Game.desktop'; chown tester:tester '$A/Test Game.desktop'"
sleep 8
vm "test -f $A/steam_app_$GAME.desktop" && echo "nickel-appd's: FAILED, still there" || echo "nickel-appd's: gone"
wait_registry "Test Game" "Test Game|steam_app_$GAME" &&
  echo "Steam's: in the registry with window class steam_app_$GAME" ||
  echo "Steam's: FAILED, '$(registry "Test Game")'"

vm "rm -rf $H/.var/app/com.valvesoftware.Steam '$A/Test Game.desktop' /usr/local/bin/steam"
sleep 6
vm "ls $A | grep -c '^steam_app_'" | sed 's/^/left behind: /'
echo "crashes: $(vm 'ls /var/spool/crash/ 2>/dev/null | grep -c nickel-appd')"
