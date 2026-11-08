#!/bin/bash
# Runs on nickel-build. Boots the latest nickel image (snapshot, normal boot),
# starts a guest session through OOBE, then checks the user's home inside
# Chrome's mount namespace: no noexec/nosymfollow, a script and a symlink to it
# run from ~/MyFiles/Downloads.
set -u
V=/srv/nickel/vmtest
IMG=${IMG:-/srv/nickel/src/src/build/images/nickel/latest/chromiumos_qemu_image.bin}
vm() { nickel-vmssh "$@"; }
state() { vm "dbus-send --system --print-reply --dest=org.chromium.SessionManager /org/chromium/SessionManager org.chromium.SessionManagerInterface.RetrieveSessionState 2>/dev/null" | grep -oE 'string "[a-z]+"' | tail -1; }
wait_log() { for i in $(seq 1 "${2:-40}"); do vm "grep -qs '$1' /var/log/chrome/chrome" && return 0; sleep 2; done; return 1; }

systemctl stop nickel-vmtest 2>/dev/null; systemctl reset-failed nickel-vmtest 2>/dev/null
systemd-run --unit nickel-vmtest --collect -E NICKEL_VMTEST_DISK=virtio /usr/local/bin/nickel-vmtest "$IMG" >/dev/null
for i in $(seq 1 60); do timeout 8 vm true 2>/dev/null && break; sleep 3; done
wait_log "AdvanceToScreen" 40; sleep 20
nickel-vmctl click 214 649                       # Get started
wait_log "AdvanceToScreen user-creation" 60 && sleep 15
nickel-vmctl click 87 776                        # Browse as Guest
wait_log "SetCurrentScreen: guest-tos" 60 && sleep 25
nickel-vmctl click 1028 647                      # Accept and continue
for i in $(seq 1 30); do [ "$(state)" = 'string "started"' ] && { echo "guest session started"; break; }; sleep 3; done
sleep 20

vm 'bash -s' <<'GUEST'
P=$(pgrep -o -x chrome)
nsenter -t "$P" -m -- sh -c '
  echo "== mount flags (want: no noexec, no nosymfollow)"
  for m in /home/chronos/user $(ls -d /home/chronos/u-* /home/user/*/ 2>/dev/null); do
    printf "%-60s %s\n" "$m" "$(findmnt -no OPTIONS "${m%/}" | tr "," "\n" | grep -E "^(noexec|nosymfollow|nosuid|nodev)$" | tr "\n" " ")"
  done
  d=$(ls -d /home/chronos/u-*/MyFiles/Downloads | head -1)
  printf "#!/bin/sh\necho script ran from \$0\n" > "$d/t.sh"; chmod +x "$d/t.sh"
  ln -sf t.sh "$d/link.sh"
  echo "== run script from Downloads"; "$d/t.sh"
  echo "== run it through a symlink"; "$d/link.sh"
'
GUEST
