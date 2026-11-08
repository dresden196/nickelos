#!/bin/bash
# Runs on nickel-build. Local accounts end to end, from a fresh image:
#   1. setup leads to "Create your account" (not Google sign-in); with
#      NICKEL_TEST_OFFLINE=1 the cable is unplugged before "Get started" and
#      the network screen's "Set up offline" leads there (in the VM the display
#      may not redraw while the network screen is entered offline; the steps
#      still happen, see Chrome's log)
#   2. creating "Tester Nickel" makes the Linux user tester (administrator,
#      first account) and starts a session with an encrypted home owned by
#      tester; Chrome restarts once and then runs as tester, and so do the
#      session plumbing and apps
#   3. the Terminal's shell runs as tester outside Chrome's sandbox
#      (no_new_privs 0), and sudo works in it
#   4. a reboot right after the first session: the login screen shows the
#      account (setup does not come back) and signing in works
#   5. after signing out the login screen's Chrome is chronos again, no
#      process of the account is left; a wrong password is refused at the
#      account's pod, the right one signs in
# Step 2 also checks the shelf: the session usually starts before the
# first-boot Bazaar install finishes, and its pin must show up afterwards.
# Shelf (1280x800): Chrome 555, Files 611, Terminal 667, Bazaar 723.
# Screenshots go to $V/flow/local-account-*.png; the VM keeps running. The VM
# renders in software, so the display lags; shot() clicks the background
# first to make it redraw.
# A second VM alongside the main one (nickel-vmtest's NICKEL_VMTEST_DIR and
# NICKEL_VMTEST_SSHPORT, which nickel-vmssh and nickel-vmctl follow): its
# unit is named after its directory.
set -u
V=${NICKEL_VMTEST_DIR:-/srv/nickel/vmtest}
UNIT=nickel-${V##*/}
IMG=${IMG:-/srv/nickel/src/src/build/images/nickel/latest/chromiumos_qemu_image.bin}
OV=$V/local-account.qcow2
NAME="Tester Nickel"
USERNAME=tester
PASS=nickel-test-pw
ACCOUNT=$USERNAME@nickel.localhost
# "Set up offline" on the network screen (1280x800).
OFFLINE_X=${OFFLINE_X:-971}
OFFLINE_Y=${OFFLINE_Y:-649}
vm() { nickel-vmssh "$@"; }
state() { vm "dbus-send --system --print-reply --dest=org.chromium.SessionManager /org/chromium/SessionManager org.chromium.SessionManagerInterface.RetrieveSessionState 2>/dev/null" | grep -oE 'string "[a-z]+"' | tail -1; }
wait_log() { for i in $(seq 1 "${2:-40}"); do vm "grep -qs '$1' /var/log/chrome/chrome" && return 0; sleep 2; done; echo "TIMEOUT waiting for: $1"; return 1; }
wait_state() { for i in $(seq 1 "${2:-30}"); do [ "$(state)" = "string \"$1\"" ] && return 0; sleep 3; done; echo "TIMEOUT waiting for session state $1 (now $(state))"; return 1; }
wait_ssh() { sleep 15; for i in $(seq 1 90); do timeout 15 nickel-vmssh true 2>/dev/null && return 0; sleep 4; done; echo "SSH NEVER CAME UP"; exit 1; }
shot() { nickel-vmctl click 1270 300; sleep 4; nickel-vmctl shot "$V/flow/local-account-$1.png"; }
crashes() {
  vm 'ls /home/chronos/crash/ /var/spool/crash/ /run/daemon-store/crash/*/ 2>/dev/null | grep -E "\.(dmp|core|meta)$" | sort -u | tr "\n" " "; echo
for m in /var/spool/crash/*.meta /run/daemon-store/crash/*/*.meta; do
  [ -e "$m" ] || continue
  echo "  ${m##*/}: $(grep -hE "^(exec_name|sig|upload_var_collector|upload_var_crash_type|upload_var_reason)=" "$m" | tr "\n" " " | cut -c1-200)"
done'
}
session_checks() {
  vm "bash -s" <<GUEST
echo "user: uid=\$(id -u $USERNAME)($USERNAME) gid=\$(id -g $USERNAME) groups=\$(id -nG $USERNAME | tr ' ' '\n' | sort | paste -sd,)"
echo "chrome runs as: \$(ps -o user= -C chrome | sort | uniq -c | tr -s ' ' | tr '\n' ';')"
echo "vault: \$(findmnt -no SOURCE /home/chronos/user) owner \$(stat -c %U:%G /home/chronos/user)"
echo "Local State owner: \$(stat -c %U '/home/chronos/Local State')"
cryptohome --action=list_auth_factors --user=$ACCOUNT 2>&1 | grep -iE "label" | head -2
grep -o '"$ACCOUNT"' /home/chronos/'Local State' | head -1
echo "session plumbing: \$(ps -o user=,comm= -C dbus-daemon,nickel-appd | grep -v messagebus | tr -s ' ' | tr '\n' ';')"
grep -iE "nickel|local.?account|Restarting to run" /var/log/chrome/chrome | grep -iE "error|fail|restart" | tail -5 | cut -c1-220
GUEST
}

mkdir -p "$V/flow"
systemctl stop "$UNIT" 2>/dev/null; systemctl reset-failed "$UNIT" 2>/dev/null
rm -f "$OV"
systemd-run --unit "$UNIT" --collect -E NICKEL_VMTEST_DISK=virtio -E NICKEL_VMTEST_OVERLAY="$OV" \
  ${NICKEL_VMTEST_DIR:+-E NICKEL_VMTEST_DIR="$V"} \
  ${NICKEL_VMTEST_SSHPORT:+-E NICKEL_VMTEST_SSHPORT="$NICKEL_VMTEST_SSHPORT"} \
  /usr/local/bin/nickel-vmtest "$IMG" >/dev/null
wait_ssh

echo "== 1. setup leads to Create your account"
wait_log "AdvanceToScreen" 60
. "$(dirname "$0")/setup-account.sh"
quiet_choice_screen
sleep 15
shot 0-welcome
if [ "${NICKEL_TEST_OFFLINE:-0}" = 1 ]; then
  nickel-vmctl link off; sleep 5
  nickel-vmctl click 214 649
  sleep 90
  nickel-vmctl shot "$V/flow/local-account-0b-network.png"
  nickel-vmctl click "$OFFLINE_X" "$OFFLINE_Y"
  sleep 30
  nickel-vmctl link on; sleep 10
else
  nickel-vmctl click 214 649
  sleep 20
fi
# Setup's choice (Chrome 0089): Minimal unless NICKEL_TEST_EXPERIENCE=full,
# so test VMs don't each download the Full experience's apps (~2 GB). Focus
# starts on Next: back to the cards, down to Minimal, on to Next.
wait_log "SetCurrentScreen: nickel-setup" 30 && { sleep 5
  if [ "${NICKEL_TEST_EXPERIENCE:-minimal}" = minimal ]; then
    nickel-vmctl key shift-tab; sleep 1; nickel-vmctl key down; sleep 1
    nickel-vmctl key tab; sleep 1
  fi
  nickel-vmctl key ret; sleep 5; }
wait_log "SetCurrentScreen: local-account-creation" 30
shot 1-create
vm "grep -E 'AdvanceToScreen|SetCurrentScreen|exited with|No screen' /var/log/chrome/chrome | sed 's/.*\] //' | cut -c1-90 | tail -8"
vm 'python3 -c "import json; print(\"setup experience:\", json.load(open(\"/home/chronos/Local State\")).get(\"nickel\", {}).get(\"setup\", {}).get(\"pending_experience\"))"' 

echo "== 2. create $NAME"
# Keyboard focus is in the page only after a click into it.
setup_focus_name
nickel-vmctl type "$NAME"; sleep 1
setup_name_to_password
nickel-vmctl type "$PASS"; sleep 1
nickel-vmctl key tab; sleep 0.5
nickel-vmctl type "$PASS"
sleep 8
shot 2-filled
nickel-vmctl click 1077 647
wait_state started 40 && echo "session started"
# NickelOS's Welcome Tour comes at a new account's first sign-in, over
# everything: no thanks (empty desktop there when it isn't up), until the
# profile says it was declined, 90 s at most.
for _ in $(seq 18); do
  nickel-vmctl click 712 594; sleep 5
  nickel-vmssh "grep -q '\"welcome_tour\":{\"v3\":{\"aborted\"' /home/chronos/user/Preferences" </dev/null && break
done
sleep 45
shot 3-session
session_checks
for i in $(seq 1 40); do vm 'test -e /var/lib/nickel/flatpak-defaults' && break; sleep 5; done
vm 'echo "Bazaar installed: $(flatpak list --app --columns=application | grep -c Bazaar)"'
sleep 10
shot 3b-shelf-after-bazaar
echo "crashes: $(crashes)"

echo "== 3. Terminal: shell as $USERNAME outside Chrome's sandbox, sudo"
nickel-vmctl key esc; sleep 1
nickel-vmctl click 667 776
sleep 25
nickel-vmctl type "echo $PASS | sudo -S sh -c 'id -u > /run/terminal-sudo-ok; mount -t tmpfs nickeltest /mnt/empty && echo mounted >> /run/terminal-sudo-ok'"
nickel-vmctl key ret
sleep 8
shot 3c-terminal
vm "bash -s" <<'GUEST'
for p in $(pgrep -x bash); do
  # A login shell: an argument that is -l, not one containing it (on
  # 16836.3.48 a root bash under bash matched too, with Gentoo's setup running).
  if tr '\0' '\n' < /proc/$p/cmdline | grep -qx -- '-l'; then
    echo "shell $p: user $(ps -o user= -p $p) parent $(ps -o comm= -p $(ps -o ppid= -p $p)) NoNewPrivs $(awk '/NoNewPrivs/{print $2}' /proc/$p/status)"
  fi
done
echo "sudo from the Terminal: $(cat /run/terminal-sudo-ok 2>/dev/null | tr '\n' ' ' || echo FAILED)"
GUEST

echo "== 4. reboot right after the first session"
vm 'ls -la /home/chronos/.oobe_completed'
vm 'reboot' >/dev/null 2>&1
wait_ssh
sleep 35
vm "grep -cE 'SetCurrentScreen' /var/log/chrome/chrome | sed 's/^/setup screens after reboot: /'"
shot 4a-boot-pod
nickel-vmctl click 640 445; nickel-vmctl type "$PASS"; nickel-vmctl key ret
wait_state started 40 && echo "signed in after reboot"
sleep 40
vm 'echo "chrome runs as: $(ps -o user= -C chrome | sort | uniq -c | tr -s " " | tr "\n" ";")"'

echo "== 5. sign out, sign in again at the pod"
vm 'dbus-send --system --print-reply --dest=org.chromium.SessionManager /org/chromium/SessionManager org.chromium.SessionManagerInterface.StopSession string: >/dev/null'
wait_state stopped 30 && echo "signed out"
sleep 25
vm 'echo "login screen chrome runs as: $(ps -o user= -C chrome | sort | uniq -c | tr -s " " | tr "\n" ";")"; echo "Local State owner: $(stat -c %U "/home/chronos/Local State")"; echo "leftover tester processes: $(ps -u tester --no-headers | wc -l)"; echo "/etc/shadow: mode $(stat -c %a /etc/shadow), readable by others: $(su -s /bin/sh nobody -c "test -r /etc/shadow" 2>/dev/null && echo YES || echo no)"'
shot 5a-pod
nickel-vmctl click 640 445; nickel-vmctl type "wrong-password"; nickel-vmctl key ret
sleep 15
echo "after wrong password: $(state)"
# No background click here: it would close the error bubble.
nickel-vmctl shot "$V/flow/local-account-5b-wrong.png"
nickel-vmctl type "$PASS"; nickel-vmctl key ret
wait_state started 40 && echo "signed in again"
sleep 45
shot 5c-relogin
session_checks
echo "crashes: $(crashes)"
