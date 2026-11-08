#!/bin/bash
# Runs on nickel-build, signed in to the test VM: phones through KDE Connect
# (nickel-phone's Valent). No phone can reach the VM, so kdeconnect-fake.py
# plays one over loopback (protocol v8, TLS). Valent is driven over its
# D-Bus API the way Settings > Connected devices does (Chrome patch 0028:
# its ObjectManager, each device's org.gtk.Actions); the card itself was
# checked on the Intel test PC with the same fake phone.
#   1. Valent runs in the session, after Chrome (started-user-session), on
#      port 1716, with KDE Connect's ports open
#   2. a new phone connects and asks to pair: Valent lists it, connected
#      and asking (State 5)
#   3. accepting (the card's Accept, action "pair"): the phone hears so and
#      reports its battery; Valent has it paired (State 3) at 64%, with
#      ring and file sending
#   4. Ring and ping reach the phone; a file sent reaches it as a share
#      request
#   5. unpairing: the phone hears so
vm() { nickel-vmssh "$@" </dev/null; }
V="nickel-session-exec gdbus call --session --dest ca.andyholmes.Valent"
id=$(printf 'nickel_fake_%024d' "$(date +%s)")
path=/ca/andyholmes/Valent/Device/${id//_/_5f}
devices() {
  vm "$V --object-path /ca/andyholmes/Valent --method org.freedesktop.DBus.ObjectManager.GetManagedObjects"
}
state() { devices | grep -o "${path}[^}]*" | grep -oE "'State': <uint32 [0-9]+>" | sed -E 's/.* ([0-9]+)>/\1/'; }
act() { vm "$V --object-path $path --method org.gtk.Actions.Activate $1 \"$2\" {}" >/dev/null; }
fake() { vm "cat /tmp/fake.out"; }

echo "== 1. Valent in the session"
vm 'status nickel-phone; pgrep -x valent >/dev/null && echo "valent running"'
vm 'ss -lnt | grep -q ":1716 " && echo "listening on tcp 1716"'
vm 'echo "firewall rules (ipv4): $(iptables -S INPUT | grep -c "1714:1764 -j ACCEPT")"'

echo "== 2. a phone asks to pair"
# A fake phone left from an earlier run (they live 120 s) would keep its
# pairing; stop it.
vm 'for p in $(pgrep -f "^python3 /usr/local/bin/kdeconnect-fake.py"); do kill $p; done'
"$(dirname "$0")"/nickel-vmpush /usr/local/bin/kdeconnect-fake.py \
  < "$(dirname "$0")/kdeconnect-fake.py"
vm "setsid -f sh -c 'python3 /usr/local/bin/kdeconnect-fake.py --name \"Pixel Test\" --id $id --battery 64 --seconds 120 > /tmp/fake.out 2>&1'"
sleep 8
fake
echo "state: $(state) (expect 5: connected, asking to pair)"

echo "== 3. accept"
act pair "[]"
sleep 3
fake | grep -E "^(paired|battery)"
echo "state: $(state) (expect 3: connected, paired)"
vm "$V --object-path $path --method org.gtk.Actions.DescribeAll" |
  tr ',' '\n' | grep -oE "'(findmyphone.ring|share.uris|ping.ping)': \(true|'percentage': <[0-9.]+>" | sort -u

echo "== 4. ring, ping, send a file"
vm 'nickel-session-exec sh -c "echo hello from NickelOS > \$HOME/Downloads/phones-test.txt"'
home=$(vm 'nickel-session-exec sh -c "echo \$HOME"')
act findmyphone.ring "[]"
act ping.ping "[]"
act share.uris "[<['file://$home/Downloads/phones-test.txt']>]"
sleep 3
fake | grep -E "^recv kdeconnect\.(findmyphone|ping|share)" | cut -c1-120

echo "== 5. unpair"
act unpair "[]"
sleep 2
fake | grep -E "^unpaired"
