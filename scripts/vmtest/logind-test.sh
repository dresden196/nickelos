#!/bin/bash
# Runs on nickel-build, in the session local-account-test.sh leaves behind
# (signed in as tester). elogind knows the session, and polkit asks the user:
#   1. elogind runs; the session is tester's, on seat0, active, type wayland;
#      session programs are in its cgroup
#   2. polkit counts a session program as the active user's (pkcheck)
#   3. the polkit agent (nickel-polkit-agent: root, in the session's
#      cgroup) is the session's; `pkexec id -u`, started in the session,
#      brings up ChromeOS's own password prompt (Chrome 0103), which takes
#      the keyboard itself; a wrong password keeps it open ("Incorrect
#      password") and the agent asks again; tester's password runs the
#      command as root
# Screenshots go to $V/flow/logind-*.png.
set -u
V=/srv/nickel/vmtest
PASS=nickel-test-pw
vm() { nickel-vmssh "$@" </dev/null; }
shot() { nickel-vmctl shot "$V/flow/logind-$1.png"; }

echo "== 1. elogind and the session"
vm 'initctl status elogind; initctl status nickel-logind-session
loginctl list-sessions --no-legend
sid=$(sed -n 1p /run/nickel/session/logind); echo "session id: $sid"
loginctl show-session "$sid" -p Name -p Seat -p Type -p Class -p Active -p State
grep -E "^XDG_SESSION_ID=" /run/nickel/session/env
for p in $(pgrep -x nickel-appd) $(pgrep -x Xwayland); do echo "$(ps -o comm= -p $p): $(grep name=elogind /proc/$p/cgroup)"; done
mountpoint /run/user/5000; ls /run/systemd/sessions/'

echo "== 2. polkit sees the active user"
vm 'A=$(pgrep -x nickel-appd)
pkcheck --process "$A" --action-id org.freedesktop.login1.inhibit-handle-lid-switch && echo "active session: yes" || echo "active session: NO"
# polkit authorizes root for everything; the case without a session needs an
# ordinary user outside the session.
sudo -u nobody sleep 30 & sleep 1; N=$(pgrep -u nobody -x sleep | head -1)
pkcheck --process "$N" --action-id org.freedesktop.login1.inhibit-handle-lid-switch && echo "process outside the session: yes (unexpected)" || echo "process outside the session: no"
kill "$N" 2>/dev/null
ls -la /usr/bin/pkexec'

echo "== 3. password prompt through the agent"
vm 'initctl status nickel-polkit-agent
for p in $(pgrep -f "^/usr/libexec/nickel-polkit-agent"); do echo "agent: $(ps -o user= -p $p), $(grep name=elogind /proc/$p/cgroup)"; done
grep "nickel-polkit-agent" /var/log/messages | grep -o "the agent of session.*" | tail -1
rm -f /run/pkexec-ok
# pkexec refuses to run once its parent is gone, so the shell waits for it.
nohup nickel-session-exec sh -c "pkexec sh -c \"id -u > /run/pkexec-ok\" >/tmp/pkexec.log 2>&1; true" >/dev/null 2>&1 &'
sleep 25
shot 1-prompt
# No click: the prompt has the keyboard.
nickel-vmctl type "not-the-password"; nickel-vmctl key ret
sleep 8
shot 2-wrong
vm 'grep -c "wrong password, asking again" /var/log/messages | sed "s/^/wrong passwords asked again: /"'
nickel-vmctl type "$PASS"; nickel-vmctl key ret
sleep 10
shot 3-after
vm 'echo "pkexec ran as uid: $(cat /run/pkexec-ok 2>/dev/null || echo FAILED)"; cat /tmp/pkexec.log | tail -3'
