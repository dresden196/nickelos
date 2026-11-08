#!/bin/bash
# Runs on nickel-build. Boots the latest nickel image and checks the native
# apps integration end to end, up to a signed-in desktop:
#   1. first boot installs Bazaar from Flathub by itself (nickel-flatpak-defaults)
#   2. /tmp follows symlinks again
#   3. guest sign-in starts nickel-session (runtime dir, session bus) and
#      nickel-appd, inside Chrome's mount namespace, as the session user
#   4. nickel-appd's app list is accepted by Chrome (no "Shutting down")
#   5. screenshot of the shelf (pins: Files, Terminal, Bazaar)
# Clicking through launcher/Bazaar/Terminal is done interactively afterwards
# with nickel-vmctl; the VM keeps running.
set -u
V=/srv/nickel/vmtest
IMG=${IMG:-/srv/nickel/src/src/build/images/nickel/latest/chromiumos_qemu_image.bin}
vm() { nickel-vmssh "$@"; }
state() { vm "dbus-send --system --print-reply --dest=org.chromium.SessionManager /org/chromium/SessionManager org.chromium.SessionManagerInterface.RetrieveSessionState 2>/dev/null" | grep -oE 'string "[a-z]+"' | tail -1; }
wait_log() { for i in $(seq 1 "${2:-40}"); do vm "grep -qs '$1' /var/log/chrome/chrome" && return 0; sleep 2; done; return 1; }

systemctl stop nickel-vmtest 2>/dev/null; systemctl reset-failed nickel-vmtest 2>/dev/null
systemd-run --unit nickel-vmtest --collect -E NICKEL_VMTEST_DISK=virtio /usr/local/bin/nickel-vmtest "$IMG" >/dev/null
for i in $(seq 1 60); do timeout 20 nickel-vmssh true 2>/dev/null && break; sleep 4; done

echo "== 1. first-boot Bazaar install"
for i in $(seq 1 60); do vm 'test -e /var/lib/nickel/flatpak-defaults' && { echo "installed after ~$((i*10))s"; break; }; sleep 10; done
vm 'flatpak list --app --columns=application,origin; ls /usr/share/flatpak/remotes.d/; tail -2 /var/log/nickel-flatpak-defaults.log'

echo "== 2. /tmp"
vm 'findmnt -no OPTIONS /tmp | tr , "\n" | grep -c nosymfollow | sed "s/^0$/symfollow OK/"'

echo "== 3. guest sign-in"
wait_log "AdvanceToScreen" 40; sleep 15
nickel-vmctl click 214 649
wait_log "AdvanceToScreen user-creation" 60 && sleep 15
nickel-vmctl click 87 776
wait_log "SetCurrentScreen: guest-tos" 60 && sleep 25
nickel-vmctl click 1028 647
for i in $(seq 1 30); do [ "$(state)" = 'string "started"' ] && { echo "guest session started"; break; }; sleep 3; done
sleep 20
vm 'bash -s' <<'GUEST'
initctl status nickel-session; initctl status nickel-appd
cat /run/nickel/session/env | grep -E "^(NICKEL_SESSION_USER|XDG_RUNTIME_DIR|WAYLAND_DISPLAY)="
ls -la /run/user/1000/ | tail -n +4
C=$(pgrep -o -x chrome); B=$(pgrep -f "^/usr/bin/dbus-daemon --session" | head -1); A=$(pgrep -x nickel-appd)
echo "mount ns: chrome $(readlink /proc/$C/ns/mnt) bus $(readlink /proc/$B/ns/mnt) appd $(readlink /proc/$A/ns/mnt)"
ps -o user=,args= -p "$B,$A" | cut -c1-100
echo "== 4. app list accepted by Chrome"
grep nickel-appd /var/log/messages | tail -4 | cut -c1-200
nsenter -t "$C" -m -- ls /home/chronos/user/crostini.icons/
grep -iE "nickel|guest_os|crostini" /var/log/chrome/chrome | grep -iE "error|warn" | tail -5 | cut -c1-220
GUEST
echo "== 5. shelf"
nickel-vmctl shot "$V/flow/launcher-shelf.png"

echo "== 6. session plumbing details"
vm 'bash -s' <<'GUEST'
echo "flatpak store: $(findmnt -no SOURCE /var/lib/flatpak)"
echo "appd NoNewPrivs: $(grep NoNewPrivs /proc/$(pgrep -x nickel-appd)/status | cut -f2)"
nsenter --mount=/run/namespaces/mnt_chrome -- awk '$5=="/var/tmp" {print "/var/tmp in chrome ns:", $7}' /proc/self/mountinfo
echo "blocked symlinks: $(dmesg | grep -c 'Blocked symlink')"
GUEST

echo "== 7. Terminal opens a shell"
nickel-vmctl click 667 776
sleep 20
nickel-vmctl type 'echo SHELL-OK $(id -un) $WAYLAND_DISPLAY; flatpak list --app --columns=application | wc -l'
nickel-vmctl key ret
sleep 5
nickel-vmctl shot "$V/flow/launcher-terminal.png"
vm 'ps -o user=,args= -C bash | grep -- " -l" | head -2'

echo "== 8. Bazaar: launch from the shelf, install an app from the GUI"
nickel-vmctl click 723 776
sleep 60
nickel-vmctl click 763 22; sleep 4
nickel-vmctl click 640 75; nickel-vmctl type eyedropper; nickel-vmctl key ret
sleep 12
nickel-vmctl click 424 355
for i in $(seq 1 30); do sleep 6; vm 'flatpak list --app --columns=application | grep -qi eyedropper' && { echo "Eyedropper installed after ~$((i*6))s"; break; }; done
sleep 5
vm 'flatpak list --app --columns=application; grep nickel-appd /var/log/messages | tail -1 | cut -c1-120'
nickel-vmctl shot "$V/flow/launcher-bazaar-install.png"
