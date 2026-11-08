#!/bin/bash
# Runs on nickel-build, in the session local-account-test.sh leaves behind
# (signed in as tester), before other apps run: with only the four default
# pins on the shelf, Bazaar's pin is at a known place. Host apps as their own
# kind of app (Chrome 0012, docs/host-apps.md):
#   1. nickel-appd's apps are in Chrome's GuestOS registry as VM type
#      NICKEL_HOST (100) under VM "nickel", and Chrome logs no unsupported type
#   2. the Bazaar pin launches Bazaar through nickel-appd (its log), and the
#      window shows under the pin
#   3. the launcher's search lists Bazaar under Apps
# Shelf (1280x800, default pins only): Chrome 555, Files 611, Terminal 667,
# Bazaar 723; launcher button 24. Screenshots go to $V/flow/hostapps-*.png.
set -u
V=/srv/nickel/vmtest
vm() { nickel-vmssh "$@" </dev/null; }
launches() { vm 'grep -c "nickel-appd.*launched io.github.kolunmi.Bazaar" /var/log/messages'; }

echo "== 1. registry"
vm 'python3 - <<EOF 2>/dev/null || echo "(no python3; raw:)" $(grep -o "\"vm_type\":[0-9]*" /home/chronos/user/Preferences | sort | uniq -c)
import json, collections
p = json.load(open("/home/chronos/user/Preferences"))
reg = p.get("crostini", {}).get("registry", {})
c = collections.Counter((a.get("vm_name"), a.get("vm_type")) for a in reg.values())
for (vm, t), n in sorted(c.items(), key=str):
    print(f"{n} apps: vm {vm} type {t}")
EOF
grep -cE "Unsupported VmType|Unknown VM type" /var/log/chrome/chrome | sed "s/^/unsupported VM type errors: /"'

echo "== 2. Bazaar from its pin"
before=$(launches)
nickel-vmctl key esc; sleep 1
nickel-vmctl click 723 776
sleep 40
echo "Bazaar launches through nickel-appd: $before before, $(launches) after"
vm 'echo "Bazaar processes: $(ps -eo args | grep -c -i -E "[k]olunmi|[b]azaar")"; grep -iE "NickelOS app launch error|nickel-appd did not launch" /var/log/chrome/chrome | tail -3'
nickel-vmctl click 1270 300; sleep 4
nickel-vmctl shot "$V/flow/hostapps-1-bazaar.png"

echo "== 3. launcher search"
nickel-vmctl key esc; sleep 1
nickel-vmctl click 24 776; sleep 6
nickel-vmctl type "bazaar"; sleep 8
# No click on the desktop first: it would close the launcher.
nickel-vmctl shot "$V/flow/hostapps-2-search.png"
nickel-vmctl key esc
