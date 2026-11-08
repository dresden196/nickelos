#!/bin/bash
# Runs on nickel-build. First real Flatpak test on a NickelOS image:
#   1. kernel/LSM: an unprivileged user namespace may mount tmpfs/proc now
#   2. /var/lib/flatpak mounted exec; polkit, flatpak present
#   3. system install of Bazaar from Flathub (as root, as first boot would)
#   4. the sandbox works for chronos: run a command inside Bazaar's sandbox
#   5. GUI: start a guest session, launch Bazaar as chronos on Chrome's
#      Wayland server (Exo), screenshot
set -u
V=/srv/nickel/vmtest
IMG=${IMG:-/srv/nickel/src/src/build/images/nickel/latest/chromiumos_qemu_image.bin}
vm() { nickel-vmssh "$@"; }
state() { vm "dbus-send --system --print-reply --dest=org.chromium.SessionManager /org/chromium/SessionManager org.chromium.SessionManagerInterface.RetrieveSessionState 2>/dev/null" | grep -oE 'string "[a-z]+"' | tail -1; }
wait_log() { for i in $(seq 1 "${2:-40}"); do vm "grep -qs '$1' /var/log/chrome/chrome" && return 0; sleep 2; done; return 1; }

systemctl stop nickel-vmtest 2>/dev/null; systemctl reset-failed nickel-vmtest 2>/dev/null
systemd-run --unit nickel-vmtest --collect -E NICKEL_VMTEST_DISK=virtio /usr/local/bin/nickel-vmtest "$IMG" >/dev/null
for i in $(seq 1 60); do timeout 20 nickel-vmssh true 2>/dev/null && break; sleep 4; done

vm 'bash -s' <<'GUEST'
echo "== 1. unprivileged user namespace mounts (bubblewrap needs these)"
# proc needs its own pid namespace (bwrap makes one too). The symlink check
# avoids /tmp, which ChromeOS mounts nosymfollow.
sudo -u chronos /usr/bin/unshare -U -m -p -f -r /bin/sh -c "mount -t tmpfs none /mnt && mount -t proc proc /proc && echo OK: tmpfs + proc mounted as chronos" 2>&1 | tail -1
sudo -u chronos /usr/bin/unshare -U -m -r /bin/sh -c "mkdir -p /dev/shm/nk && ln -sfn /mnt /dev/shm/nk/link && mount -t tmpfs none /dev/shm/nk/link && echo OK: mount through a symlinked path" 2>&1 | tail -1
echo "== 2. storage + packages"
findmnt -no OPTIONS /var/lib/flatpak | tr , "\n" | grep -E "^(noexec|exec|nosuid|nodev)$" | tr "\n" " "; echo "($(findmnt -no TARGET /var/lib/flatpak))"
flatpak --version; ls -l /usr/bin/bwrap /usr/bin/fusermount3 | awk '{print $1, $NF}'
ls /etc/polkit-1/rules.d/
echo "== 3. system install of Bazaar from Flathub"
flatpak remote-add --if-not-exists flathub https://dl.flathub.org/repo/flathub.flatpakrepo && echo "flathub remote added"
time flatpak install -y --noninteractive flathub io.github.kolunmi.Bazaar > /tmp/fp-install.log 2>&1; echo "install exit=$?"; tail -3 /tmp/fp-install.log
flatpak list --app
GUEST

echo "== 4. sandbox as chronos"
# Stand-in for the per-login runtime dir the session plumbing will create.
vm 'mkdir -p /run/user/1000 && chown chronos:chronos /run/user/1000 && chmod 700 /run/user/1000; command -v dbus-run-session || echo "(no dbus-run-session)"'
vm 'sudo -u chronos env XDG_RUNTIME_DIR=/run/user/1000 flatpak run --command=sh io.github.kolunmi.Bazaar -c "echo inside sandbox as \$(id -un) on \$(uname -r); ls /app/bin | head -3" 2>&1 | tail -4'

echo "== 5. GUI: guest session, then Bazaar on Exo"
wait_log "AdvanceToScreen" 40; sleep 15
nickel-vmctl click 214 649
wait_log "AdvanceToScreen user-creation" 60 && sleep 15
nickel-vmctl click 87 776
wait_log "SetCurrentScreen: guest-tos" 60 && sleep 25
nickel-vmctl click 1028 647
for i in $(seq 1 30); do [ "$(state)" = 'string "started"' ] && { echo "guest session started"; break; }; sleep 3; done
sleep 20
vm 'ln -sfn /run/chrome/wayland-0 /run/user/1000/wayland-0; nohup sudo -u chronos env XDG_RUNTIME_DIR=/run/user/1000 WAYLAND_DISPLAY=wayland-0 dbus-run-session -- flatpak run io.github.kolunmi.Bazaar > /tmp/bazaar.log 2>&1 < /dev/null & sleep 40; pgrep -af bazaar | head -2 | cut -c1-120; tail -8 /tmp/bazaar.log | cut -c1-200'
nickel-vmctl shot "$V/flow/bazaar.png"
