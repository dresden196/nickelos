#!/bin/bash
# Runs on the build host, as root: what setup's two ways to start (Chrome
# 0089, 0090, 0092) leave on a fresh image (latest, or IMG=), in its own VM
# (NICKEL_VMTEST_DIR /srv/nickel/vmtest-x, ssh port 9228), with Chrome's
# DevTools for what only Chrome knows:
#   experience-test.sh full      the Full experience:
#     1. setup with Full experience (the card it starts on); the device's
#        choice in local state
#     2. DuckDuckGo is the browser's search engine (so the launcher's)
#     3. Proton's six web apps installed (chrome://web-app-internals: Mail,
#        Calendar, Drive, Docs, Pass, Lumo; from the image's configs with
#        their manifests and icons, no visit to Proton's sites)
#     4. uBlock Origin installed, enabled, Manifest V2, its background page
#        running
#     5. the Flatpaks: the five every device gets, then the Full
#        experience's nine (nickel-flatpak-defaults; ~2 GB, an hour at most)
#   experience-test.sh minimal   Minimal:
#     1. setup with Minimal; the choice in local state
#     2. the launcher's web results off
#     3. no Proton web apps, no uBlock Origin
#     4. the browser's first window asks for a search engine (Chrome's
#        choice screen, 0092): the engines it offers; choosing DuckDuckGo
#        makes it the default
#     5. the Flatpaks: the five, none of Full's
# NICKEL_WEB_APPS_DIR= tries other preinstalled web app configs (see below).
# Screenshots: $D/flow/experience-<mode>-*.png. The VM keeps running.
set -u
MODE=${1:-full}
case "$MODE" in full | minimal) ;; *) echo "usage: $0 full|minimal"; exit 2 ;; esac
export NICKEL_VMTEST_DIR=${NICKEL_VMTEST_DIR:-/srv/nickel/vmtest-x}
export NICKEL_VMTEST_SSHPORT=${NICKEL_VMTEST_SSHPORT:-9228}
export NICKEL_TEST_EXPERIENCE=$MODE
# Minimal checks Chrome's search engine choice screen: not turned off.
[ "$MODE" = minimal ] && export NICKEL_TEST_CHOICE=1
D=$NICKEL_VMTEST_DIR
V=$D
here=$(dirname "$(readlink -f "$0")")
IMG=${IMG:-/srv/nickel/src/src/build/images/nickel/latest/chromiumos_qemu_image.bin}
NAME="Tester Nickel"
PASS=nickel-test-pw
DEVTOOLS=9328
UBO=fkgkibajhfbepljeaefdnfnegdcjomkh
vm() { nickel-vmssh "$@" </dev/null; }
state() { vm "dbus-send --system --print-reply --dest=org.chromium.SessionManager /org/chromium/SessionManager org.chromium.SessionManagerInterface.RetrieveSessionState 2>/dev/null" | grep -oE 'string "[a-z]+"' | tail -1; }
wait_log() { for i in $(seq 1 "${2:-40}"); do vm "grep -qs '$1' /var/log/chrome/chrome" && return 0; sleep 2; done; echo "TIMEOUT waiting for: $1"; return 1; }
wait_state() { for i in $(seq 1 "${2:-30}"); do [ "$(state)" = "string \"$1\"" ] && return 0; sleep 3; done; echo "TIMEOUT waiting for session state $1 (now $(state))"; return 1; }
wait_ssh() { sleep 15; for i in $(seq 1 90); do timeout 15 nickel-vmssh true 2>/dev/null && return 0; sleep 4; done; echo "SSH NEVER CAME UP"; exit 1; }
shot() { sleep 2; nickel-vmctl shot "$D/flow/experience-$MODE-$1.png"; }
# A pref of the account (dotted path in Preferences), as JSON.
pref() {
  vm "python3 -c \"
import json
p = json.load(open('/home/chronos/user/Preferences'))
for k in '$1'.split('.'):
    p = p.get(k) if isinstance(p, dict) else None
print(json.dumps(p, sort_keys=True))\""
}

mkdir -p "$D/flow"
unit=nickel-$(basename "$D")
systemctl stop "$unit" 2>/dev/null; systemctl reset-failed "$unit" 2>/dev/null
systemd-run --unit "$unit" --collect -E NICKEL_VMTEST_DIR="$D" \
  -E NICKEL_VMTEST_SSHPORT="$NICKEL_VMTEST_SSHPORT" \
  /usr/local/bin/nickel-vmtest "$IMG" >/dev/null
sleep 5
systemctl is-active -q "$unit" || { echo "the VM did not start"; journalctl -u "$unit" | tail -5; exit 1; }

tunnel=
cleanup() { [ -n "$tunnel" ] && kill "$tunnel" 2>/dev/null; }
trap cleanup EXIT
wait_ssh
# DevTools from the start (the first window's choice screen comes once): the
# welcome screen again, with Chrome's port.
wait_log "AdvanceToScreen" 60
vm 'mount -o remount,rw / 2>/dev/null
echo --remote-debugging-port=9223 >> /etc/chrome_dev.conf
stop ui >/dev/null; rm -f /var/log/chrome/chrome; start ui >/dev/null'
# NICKEL_WEB_APPS_DIR=: preinstalled web app configs (and their icons/) to
# try in place of the image's, before setup.
if [ -n "${NICKEL_WEB_APPS_DIR:-}" ]; then
  tar -C "$NICKEL_WEB_APPS_DIR" -cf - . |
    nickel-vmssh 'rm -rf /usr/share/chromium/extensions/web_apps && mkdir -p /usr/share/chromium/extensions/web_apps && tar -C /usr/share/chromium/extensions/web_apps -xf - && chown -R root:root /usr/share/chromium/extensions/web_apps'
  echo "web app configs: $NICKEL_WEB_APPS_DIR"
fi

echo "== 1. setup ($MODE), account $NAME"
. "$here/setup-account.sh"
setup_account "$NAME" "$PASS" "experience-$MODE" || exit 1
echo "device's choice: $(vm "python3 -c \"import json; print(json.load(open('/home/chronos/Local State')).get('nickel', {}).get('setup', {}).get('pending_experience'))\"")"
# The Welcome Tour, at a new account's first sign-in: no thanks.
for _ in $(seq 18); do
  nickel-vmctl click 712 594; sleep 5
  vm "grep -q '\"welcome_tour\":{\"v3\":{\"aborted\"' /home/chronos/user/Preferences" && break
done
sleep 20
shot 1-session
echo "account's choice: $(pref nickel.setup.experience)"

ssh -i /root/.ssh/cros_testing_rsa -p "$NICKEL_VMTEST_SSHPORT" \
  -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR \
  -N -L $DEVTOOLS:127.0.0.1:9223 root@127.0.0.1 &
tunnel=$!
sleep 3
targets() { curl -s "http://127.0.0.1:$DEVTOOLS/json/list"; }
# Runs JS (with the helpers below) in the page whose URL starts with $1.
js() {
  cat > /tmp/experience-step.js <<JS
(async () => {
  const sleep = ms => new Promise(r => setTimeout(r, ms));
  const all = (root, sel, out = []) => {
    out.push(...root.querySelectorAll(sel));
    root.querySelectorAll('*').forEach(
        el => el.shadowRoot && all(el.shadowRoot, sel, out));
    return out;
  };
  const text = el => el ? el.innerText.replace(/\s+/g, ' ').trim() : null;
  $2
})()
JS
  python3 "$here/cdp-eval.py" $DEVTOOLS "$1" /tmp/experience-step.js |
    tr -d '\n' | sed -E 's/  +/ /g; s/^.*result: ?//; s/ ?console: .*$//'; echo
}
# The installed web apps named Proton's (none: "[]"): chrome://web-app-
# internals' index of installed web apps, in its element's shadow root (the
# page's own text is empty). The page is opened first: cdp-eval.py waits for
# a chrome:// page, it doesn't open one.
proton_apps() {
  python3 "$here/cdp-eval.py" $DEVTOOLS --open chrome://web-app-internals >/dev/null 2>&1
  js chrome://web-app-internals "await sleep(3000);
  const t = document.querySelector('web-app-internals-app').shadowRoot.textContent;
  const i = t.indexOf('\"!Index\"');
  const index = i < 0 ? '' : t.slice(i, t.indexOf('}', i));
  return (index.match(/\"(Proton [A-Za-z]+|Lumo)\"/g) || []).map(n => n.replace(/\"/g, '')).sort();"
}
# uBlock Origin as chrome://extensions-internals has it (null: not there).
ubo() {
  python3 "$here/cdp-eval.py" $DEVTOOLS --open chrome://extensions-internals >/dev/null 2>&1
  js chrome://extensions-internals "await sleep(2000);
  const list = JSON.parse(document.body.innerText.slice(document.body.innerText.indexOf('[')));
  const e = list.find(x => x.id === '$UBO');
  return e ? {version: e.version, location: e.location, manifest_version: e.manifest_version,
              disable_reasons: e.disable_reasons, blocklisted: e.blocklist_state} : null;"
}
flatpaks() { vm 'cat /var/lib/nickel/flatpak-defaults 2>/dev/null' | tr '\n' ' '; }
# nickel-flatpak-defaults to its end: done or failed (MAX minutes).
wait_flatpaks() {
  local s
  for i in $(seq 1 $(($1 * 2))); do
    s=$(vm 'cat /run/nickel/flatpak-defaults 2>/dev/null')
    case "$s" in done | failed) echo "nickel-flatpak-defaults: $s"; return 0 ;; esac
    [ $((i % 10)) = 0 ] && echo "  ($((i / 2)) min: ${s:-not started}, $(flatpaks | wc -w) installed)"
    sleep 30
  done
  echo "TIMEOUT: nickel-flatpak-defaults still ${s:-not started} after $1 min"
}
FULL_APPS="org.libreoffice.LibreOffice com.obsproject.Studio org.gimp.GIMP org.kde.kdenlive org.audacityteam.Audacity io.freetubeapp.FreeTube org.localsend.localsend_app io.missioncenter.MissionCenter com.github.tchx84.Flatseal"

if [ "$MODE" = full ]; then
  echo "== 2. search engine: $(pref default_search_provider_data.template_url_data.keyword)"
  echo "== 3. Proton's web apps (Chrome installs them some time after sign-in)"
  for _ in $(seq 20); do
    apps=$(proton_apps)
    [ "$(echo "$apps" | grep -oE 'Proton [A-Za-z]+|Lumo' | wc -l)" -ge 6 ] && break
    sleep 15
  done
  echo "web apps: $apps"
  echo "== 4. uBlock Origin"
  echo "extension: $(ubo)"
  echo "on disk: $(vm "ls /home/chronos/user/Extensions/$UBO 2>&1")"
  targets | python3 -c "
import json, sys
bg = [t for t in json.load(sys.stdin) if t['url'].startswith('chrome-extension://$UBO/')]
print('background page:', ', '.join(t['type'] + ' ' + t['url'].split('/')[-1] for t in bg) or 'NONE')"
  shot 2-desktop
  echo "== 5. Flatpaks"
  wait_flatpaks 60
  got=$(flatpaks)
  echo "installed: $got"
  for a in $FULL_APPS; do echo " $got " | grep -q " $a " || echo "MISSING: $a"; done
  vm 'grep -c "^Installing\|Installation complete" /var/log/nickel-flatpak-defaults.log; grep -iE "error|retrying" /var/log/nickel-flatpak-defaults.log | tail -3'
else
  echo "== 2. launcher web results: $(pref launcher.search_category_control_status)"
  echo "== 3. nothing of Full's"
  echo "search engine set by setup: $(pref default_search_provider_data.template_url_data.keyword)"
  echo "uBlock Origin on disk: $(vm "ls /home/chronos/user/Extensions/$UBO 2>&1 | head -1")"
  echo "== 4. the browser's first window"
  python3 "$here/cdp-eval.py" $DEVTOOLS --open about:blank
  for _ in $(seq 15); do
    targets | grep -q '"url": "chrome://search-engine-choice' && break
    sleep 2
  done
  shot 3-choice
  if targets | grep -q '"url": "chrome://search-engine-choice'; then
    echo "choice screen: shown"
    echo "engines: $(js chrome://search-engine-choice "await sleep(1000);
    return all(document, 'cr-radio-button, [role=radio]').map(text).filter(Boolean);")"
    echo "choose DuckDuckGo: $(js chrome://search-engine-choice "
    const pick = all(document, 'cr-radio-button, [role=radio]').find(e => /DuckDuckGo/.test(text(e)));
    if (!pick) return 'no DuckDuckGo';
    pick.click(); await sleep(1000);
    // Set as default: the page scrolls through the list first (More),
    // then the action button chooses.
    for (let i = 0; i < 4; i++) {
      const b = all(document, 'cr-button.action-button, #actionButton').find(x => !x.disabled && x.offsetParent);
      if (!b) break;
      const label = text(b); b.click(); await sleep(1500);
      if (!/more/i.test(label)) return 'clicked ' + label;
    }
    return 'no button';")"
    sleep 12
    echo "search engine now: $(pref default_search_provider_data.template_url_data.keyword)"
    shot 4-chosen
  else
    echo "choice screen: NOT SHOWN"
    targets | python3 -c "import json, sys; [print(' ', t['type'], t['url'][:80]) for t in json.load(sys.stdin)]"
  fi
  echo "== 5. Flatpaks"
  wait_flatpaks 30
  got=$(flatpaks)
  echo "installed: $got"
  for a in $FULL_APPS; do echo " $got " | grep -q " $a " && echo "UNEXPECTED: $a"; done
fi
echo "== done"
