#!/bin/bash
# Runs on nickel-build. Local accounts, Linux side (nickel-accounts):
#   1. root creates an administrator: uid, own group, chronos's groups, wheel,
#      sudo accepts her password
#   2. the login screen's Chrome (chronos, no session) may create a user
#   3. during a session, chronos may not
#   4. users survive a reboot (persistent disk overlay)
# NICKEL_ACCOUNTS_DEPLOY=1 copies the package from the board sysroot into the
# VM first (for images built before nickel-accounts was in them).
set -u
V=/srv/nickel/vmtest
IMG=${IMG:-/srv/nickel/src/src/build/images/nickel/latest/chromiumos_qemu_image.bin}
OV=$V/accounts.qcow2
SYSROOT=/srv/nickel/src/out/build/nickel
vm() { nickel-vmssh "$@"; }
wait_ssh() { sleep 15; for i in $(seq 1 90); do timeout 15 nickel-vmssh true 2>/dev/null && return 0; sleep 4; done; echo "SSH NEVER CAME UP"; exit 1; }
call() { # user method signature-args...
  local who=$1; shift
  vm "sudo -u $who dbus-send --system --print-reply --dest=org.nickelos.Accounts /org/nickelos/Accounts org.nickelos.Accounts.$* 2>&1 | tail -1"
}

systemctl stop nickel-vmtest 2>/dev/null; systemctl reset-failed nickel-vmtest 2>/dev/null
rm -f "$OV"
systemd-run --unit nickel-vmtest --collect -E NICKEL_VMTEST_DISK=virtio -E NICKEL_VMTEST_OVERLAY="$OV" \
  /usr/local/bin/nickel-vmtest "$IMG" >/dev/null
wait_ssh

if [ "${NICKEL_ACCOUNTS_DEPLOY:-0}" = 1 ]; then
  echo "== deploy nickel-accounts from the sysroot"
  tar -C "$SYSROOT" -cf - usr/sbin/nickel-accountsd usr/sbin/nickel-accounts-setup \
      etc/init/nickel-accounts-setup.conf etc/init/nickel-accountsd.conf \
      etc/dbus-1/system.d/org.nickelos.Accounts.conf etc/sudoers.d/90-nickel-wheel |
    vm 'mount -o remount,rw / && tar -C / -xf - && restorecon -R /usr/sbin/nickel-account* /etc/init/nickel-account* /etc/dbus-1/system.d/org.nickelos.Accounts.conf /etc/sudoers.d/90-nickel-wheel && dbus-send --system --type=method_call --dest=org.freedesktop.DBus / org.freedesktop.DBus.ReloadConfig && initctl reload-configuration && initctl start nickel-accounts-setup && initctl start nickel-accountsd; sleep 2; initctl status nickel-accountsd'
fi

echo "== 1. root creates administrator alice"
call root CreateUser string:alice "\"string:Alice Example\"" string:correct-horse boolean:true
vm 'grep "^alice:" /etc/passwd; id alice; grep "^alice:" /etc/shadow | cut -c1-12'
vm 'echo correct-horse | sudo -u alice sudo -S -k -p "" id -un 2>&1 | tail -1'
vm 'echo wrong-password | sudo -u alice sudo -S -k -p "" id -un 2>&1 | tail -1'

echo "== 2. login screen (no session): chronos creates bob"
vm 'dbus-send --system --print-reply --dest=org.chromium.SessionManager /org/chromium/SessionManager org.chromium.SessionManagerInterface.RetrieveSessionState | tail -1'
call chronos CreateUser string:bob string:Bob string:hunter2 boolean:false
vm 'id bob'

echo "== 3. in a session, chronos may not create users"
NICKEL_VMTEST_DIR=$V bash "$V/guest-login.sh" >/dev/null 2>&1
call chronos CreateUser string:mallory string:Mallory string:x boolean:true
vm 'id mallory 2>&1'

echo "== 4. reboot: users persist"
vm 'reboot' >/dev/null 2>&1
wait_ssh
vm 'id alice; id bob; mountpoint /etc/passwd; initctl status nickel-accountsd'
