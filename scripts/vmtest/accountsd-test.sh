#!/bin/bash
# Runs on nickel-build, signed in as tester (the VM's owner and administrator,
# password nickel-test-pw) in the test VM with nickel-accounts -r2.
# nickel-accountsd's D-Bus interface, as Settings > Accounts (Chrome 0026)
# uses it, and who may do what:
#   - ListUsers: open to all
#   - SetPassword: only the user, with their current password (a wrong one
#     is answered after a second)
#   - SetAdmin / RemoveUser: only an administrator, with their password;
#     the last administrator stays one, nobody removes themselves
#   - an administrator is in wheel (sudo), a standard user is not
# tester2 is a Linux user created for the test (root's CreateUser) and
# removed at the end.
set -u
cat > /tmp/accountsd-test.inner <<'EOF'
D="--system --dest org.nickelos.Accounts --object-path /org/nickelos/Accounts --method org.nickelos.Accounts"
as_tester() { nickel-session-exec gdbus call $D."$@" 2>&1 | sed 's/^Error: GDBus.Error://' | cut -c1-110; }
as_tester2() { sudo -u tester2 gdbus call $D."$@" 2>&1 | sed 's/^Error: GDBus.Error://' | cut -c1-110; }
wheel() { grep ^wheel: /etc/group | cut -d: -f4; }
P=nickel-test-pw
gdbus call $D.DeleteUser tester2 >/dev/null 2>&1
echo "create tester2 (root):        $(gdbus call $D.CreateUser tester2 'Second Tester' pw2 false 2>&1)"
echo "list:                         $(as_tester ListUsers)"
T=$(date +%s); echo "password, wrong current:      $(as_tester SetPassword tester wrong new) ($(( $(date +%s) - T ))s)"
echo "password, right current:      $(as_tester SetPassword tester $P $P)"
echo "admin, wrong password:        $(as_tester SetAdmin wrong tester2 true)"
echo "make tester2 admin:           $(as_tester SetAdmin $P tester2 true) wheel=$(wheel)"
echo "tester2's sudo:               $(echo pw2 | sudo -u tester2 sudo -S -k id -un 2>&1 | tail -1)"
echo "make tester2 standard:        $(as_tester SetAdmin $P tester2 false) wheel=$(wheel)"
echo "tester2's sudo:               $(echo pw2 | sudo -u tester2 sudo -S -k id -un 2>&1 | tail -1)"
echo "demote the last admin:        $(as_tester SetAdmin $P tester false)"
echo "tester2 promotes itself:      $(as_tester2 SetAdmin pw2 tester2 true)"
echo "tester2 sets tester's pw:     $(as_tester2 SetPassword tester pw2 x)"
echo "tester removes itself:        $(as_tester RemoveUser $P tester)"
echo "remove tester2:               $(as_tester RemoveUser $P tester2) passwd=$(grep -c ^tester2: /etc/passwd)"
EOF
K=$(mktemp); install -m600 /srv/nickel/src/chromite/ssh_keys/testing_rsa "$K"
scp -q -i "$K" -P "${NICKEL_VMTEST_SSHPORT:-9222}" -o StrictHostKeyChecking=no \
  -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR /tmp/accountsd-test.inner \
  root@127.0.0.1:/usr/local/accountsd-test.inner
rm -f "$K"
nickel-vmssh 'bash /usr/local/accountsd-test.inner; rm -f /usr/local/accountsd-test.inner' </dev/null
