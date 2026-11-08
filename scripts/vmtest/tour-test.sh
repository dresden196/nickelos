#!/bin/bash
# Runs on a build host, as root, with tester signed in in the test VM (port
# 9222; local-account-test.sh, which declined the tour). NickelOS's own
# Welcome Tour (Chrome 0071), as at a new account's first sign-in: the
# tour's record reset in the profile and Chrome restarted in the session
# (it then offers the tour, as to a user never offered it), "Quick tour",
# then Enter on each step's bubble (its default button is Next):
#   - the steps shown, in Chrome's histogram Ash.WelcomeTour.Step.Shown:
#     dialog, shelf, launcher, search, Files, Bazaar, Android (Waydroid,
#     Chrome 0073), Terminal, Settings
#   - the tour recorded as completed
#   - its last button opened https://nickelos.dev/help in the browser
# Through DevTools (chrome_dev.conf gets --remote-debugging-port for this,
# taken out again at the end, signing in again each time). The VM's screen
# captures don't show help bubbles, hence the histogram.
set -u
here=$(dirname "$(readlink -f "$0")")
vm() { nickel-vmssh "$@" </dev/null; }
session() { vm 'dbus-send --system --print-reply --dest=org.chromium.SessionManager /org/chromium/SessionManager org.chromium.SessionManagerInterface.RetrieveSessionState' | sed -n 's/.*string "\(.*\)"/\1/p'; }
relogin() {
  vm 'restart ui >/dev/null'
  for _ in $(seq 60); do [ "$(session)" = stopped ] && vm 'pgrep -o -f "^/opt/google/chrome/chrome "' >/dev/null && break; sleep 2; done
  sleep 12
  nickel-vmctl key esc; sleep 1
  nickel-vmctl type nickel-test-pw; nickel-vmctl key ret
  for _ in $(seq 60); do [ "$(session)" = started ] && break; sleep 2; done
  sleep 30
  # ChromeOS's restore prompt and its introduction (see portals-test.sh).
  for _ in $(seq 12); do nickel-vmctl key esc; sleep 5; done
  nickel-vmctl click 638 536; sleep 3
}
vm "mkdir -p /usr/local/nickel-test" && nickel-vmssh "cat > /usr/local/nickel-test/cdp-eval.py" < "$here/cdp-eval.py"
cdp() { vm "python3 /usr/local/nickel-test/cdp-eval.py 9223 $*"; }

echo "== DevTools on, signed in again"
vm 'mount -o remount,rw / 2>/dev/null; cp /etc/chrome_dev.conf /tmp/chrome_dev.conf.tour; grep -q remote-debugging-port=9223 /etc/chrome_dev.conf || echo --remote-debugging-port=9223 >> /etc/chrome_dev.conf'
relogin

echo "== the tour offered again"
vm 'cat > /tmp/tour-reset.py <<"EOF"
import json
p = "/home/chronos/user/Preferences"
d = json.load(open(p))
d.get("ash", {}).pop("welcome_tour", None)
json.dump(d, open(p, "w"))
EOF
python3 /tmp/tour-reset.py && kill -9 $(pgrep -o -f "^/opt/google/chrome/chrome ")'
sleep 45
nickel-vmctl click 814 594   # "Quick tour"
sleep 8
for _ in $(seq 8); do nickel-vmctl key ret; sleep 6; done
sleep 4

echo "== what it did"
vm "printf '(() => { const t = document.body.innerText; const i = t.indexOf(\"Ash.WelcomeTour.Step.Shown\"); return i < 0 ? \"none\" : t.slice(i, i + 1600); })()' > /tmp/tour-steps.js"
cdp --open chrome://histograms/ >/dev/null
sleep 3
steps=$(cdp chrome://histograms /tmp/tour-steps.js | python3 -c '
import re, sys
t = sys.stdin.read().split("Histogram:")[0]
names = {"0": "dialog", "6": "shelf", "3": "launcher", "4": "search", "8": "Files",
         "1": "Bazaar", "2": "Android", "7": "Terminal", "5": "Settings"}
shown = set(re.findall(r"\\n(\d+)\s+-O", t))
print(", ".join(n for k, n in names.items() if k in shown))')
echo "steps shown: $steps"
vm "grep -o '\"welcome_tour\":{\"v3\":{\"[a-z]*' /home/chronos/user/Preferences" | sed 's/.*"/record: /'
vm "curl -s http://127.0.0.1:9223/json/list" | grep -q '"url": "https://nickelos.dev/help' &&
  echo "the guide opened: https://nickelos.dev/help" || echo "the guide: FAILED, no tab"

echo "== DevTools off again"
vm 'cp /tmp/chrome_dev.conf.tour /etc/chrome_dev.conf'
relogin
echo "crashes: $(vm 'ls /var/spool/crash/ 2>/dev/null | grep -c chrome')"
