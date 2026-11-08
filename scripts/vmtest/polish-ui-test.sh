#!/bin/bash
# Runs on the build host, as root: the 2026-10-10 clean-up in a signed-in
# session, from a fresh image (latest, or IMG=) in its own VM
# (NICKEL_VMTEST_DIR /srv/nickel/vmtest-b, ssh port 9224), read through
# Chrome's DevTools (cdp-eval.py):
#   1. setup and the account "Tester Nickel" (as local-account-test.sh)
#   2. first boot installs Bazaar and GNOME's Loupe, Showtime, Papers and
#      Amberol (nickel-flatpak-defaults)
#   3. Settings, Search and suggestions: no content recommendations and no
#      Google AI (Chrome 0054, no field-trial testing config); not on the
#      Privacy controls page either
#   4. Files: Desktop, Documents, Music, Pictures and Videos have their own
#      icons (Chrome 0053)
#   5. Settings > Apps > Default apps: GNOME's apps are the defaults, Gallery
#      is nowhere (Chrome 0055); App Service has no Gallery
#   6. the About page and chrome://version: NickelOS, not ChromiumOS
#   7. no Crostini (Chrome 0099, USE=-kvm_host): no Developers card on the
#      About page, nothing about Linux to set up in Settings' search, the
#      Terminal's home page has the device's shell and no Linux to set up
#      (crosh-extension 0001), no VM daemons, Chrome without Crostini
#   (2b) setup's experience saved: Minimal, the tests' choice
#        (setup-account.sh), so no search engine yet (Chrome's choice
#        screen asks, off in tests); experience-test.sh checks both
#   (2c) the account's picture: one Chromium has (chrome://theme), saved,
#        nothing downloaded (Chrome 0059)
# Screenshots: $D/flow/polish-*.png. The VM keeps running.
set -u
export NICKEL_VMTEST_DIR=${NICKEL_VMTEST_DIR:-/srv/nickel/vmtest-b}
export NICKEL_VMTEST_SSHPORT=${NICKEL_VMTEST_SSHPORT:-9224}
D=$NICKEL_VMTEST_DIR
here=$(dirname "$(readlink -f "$0")")
IMG=${IMG:-/srv/nickel/src/src/build/images/nickel/latest/chromiumos_qemu_image.bin}
NAME="Tester Nickel"
PASS=nickel-test-pw
vm() { nickel-vmssh "$@"; }
state() { vm "dbus-send --system --print-reply --dest=org.chromium.SessionManager /org/chromium/SessionManager org.chromium.SessionManagerInterface.RetrieveSessionState 2>/dev/null" | grep -oE 'string "[a-z]+"' | tail -1; }
wait_log() { for i in $(seq 1 "${2:-40}"); do vm "grep -qs '$1' /var/log/chrome/chrome" && return 0; sleep 2; done; echo "TIMEOUT waiting for: $1"; return 1; }
wait_state() { for i in $(seq 1 "${2:-30}"); do [ "$(state)" = "string \"$1\"" ] && return 0; sleep 3; done; echo "TIMEOUT waiting for session state $1 (now $(state))"; return 1; }
wait_ssh() { sleep 15; for i in $(seq 1 90); do timeout 15 nickel-vmssh true 2>/dev/null && return 0; sleep 4; done; echo "SSH NEVER CAME UP"; exit 1; }
shot() { sleep 2; nickel-vmctl shot "$D/flow/polish-$1.png"; }

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

echo "== 1. setup, account $NAME"
. "$(dirname "$0")/setup-account.sh"
setup_account "$NAME" "$PASS" polish
# NickelOS's Welcome Tour comes at a new account's first sign-in, over
# everything: no thanks (empty desktop there when it isn't up), until the
# profile says it was declined, 90 s at most.
for _ in $(seq 18); do
  nickel-vmctl click 712 594; sleep 5
  nickel-vmssh "grep -q '\"welcome_tour\":{\"v3\":{\"aborted\"' /home/chronos/user/Preferences" </dev/null && break
done
sleep 45
shot 1-session

sign_in() {
  sleep 30; nickel-vmctl click 640 400; sleep 3
  nickel-vmctl type "$PASS"; nickel-vmctl key ret
  wait_state started 40 >/dev/null; sleep 30
  # ChromeOS offers the last session's windows ("Welcome Recap"), which
  # takes the keyboard: no thanks. It comes some 35-40 s after sign-in and
  # logs nothing to wait for, so Esc every 5 s for a minute (Esc on the bare
  # desktop does nothing).
  for _ in $(seq 12); do nickel-vmctl key esc; sleep 5; done
  # A first such sign-in with nothing to restore shows the feature's
  # introduction instead, which Esc leaves: its "Got it" (empty desktop
  # when it isn't there).
  nickel-vmctl click 638 536; sleep 3
}
# Chrome with DevTools on the VM's port 9223, here on 9323.
vm 'mount -o remount,rw / 2>/dev/null
grep -q remote-debugging-port=9223 /etc/chrome_dev.conf ||
  echo --remote-debugging-port=9223 >> /etc/chrome_dev.conf
grep -q hide-crash-restore-bubble /etc/chrome_dev.conf ||
  echo --hide-crash-restore-bubble >> /etc/chrome_dev.conf'
restart_ui
sign_in
ssh -i /root/.ssh/cros_testing_rsa -p "$NICKEL_VMTEST_SSHPORT" \
  -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR \
  -N -L 9323:127.0.0.1:9223 root@127.0.0.1 &
tunnel=$!
sleep 3

# Runs JS (with the helpers below) in the page whose URL starts with $1.
js() {
  cat > /tmp/cloud-step.js <<JS
(async () => {
  const sleep = ms => new Promise(r => setTimeout(r, ms));
  const deep = (root, sel) => root.querySelector(sel) ||
      [...root.querySelectorAll('*')].reduce((hit, el) =>
          hit || (el.shadowRoot && deep(el.shadowRoot, sel)), null);
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
  # Only the value: the page's console messages come before it.
  python3 "$here/cdp-eval.py" 9323 "$1" /tmp/cloud-step.js |
    tr -d '\n' | sed -E 's/  +/ /g; s/^.*result: ?//; s/ ?console: .*$//'; echo
}
# All the text of a page, shadow roots included.
TEXT='const fullText = root => {
    let t = root.body ? root.body.innerText :
        [...root.children].map(c => c.innerText || "").join(" ");
    for (const el of root.querySelectorAll("*")) {
      if (el.shadowRoot) t += " " + fullText(el.shadowRoot);
    }
    return t;
  };'
open_page() {
  # Through DevTools: no keyboard focus needed (cdp-eval.py --open).
  python3 "$here/cdp-eval.py" 9323 --open "$1"
  sleep 5
}

echo "== 2. first boot's apps"
for i in $(seq 1 270); do
  s=$(vm 'cat /run/nickel/flatpak-defaults 2>/dev/null')
  [ "$s" = done ] || [ "$s" = failed ] && break
  sleep 10
done
echo "state: $s after ~$((i * 10))s"
echo "marker: $(vm 'cat /var/lib/nickel/flatpak-defaults' | tr '\n' ' ')"
echo "installed: $(vm 'flatpak list --app --columns=application' | tr '\n' ' ')"

echo "== 2b. setup's experience (Minimal, no search engine chosen yet)"
U=$(vm 'ls -d /home/chronos/u-* | head -1')
sleep 12
vm "python3 -c \"import json; p = json.load(open('$U/Preferences')); print('experience:', p.get('nickel', {}).get('setup', {}).get('experience')); print('search engine:', p.get('default_search_provider_data', {}).get('template_url_data', {}).get('keyword'))\""

echo "== 2c. the account's picture"
vm "python3 -c \"
import json
s = json.load(open('/home/chronos/Local State'))
for k, v in s.get('user_image_info', {}).items(): print(k, v.get('index'), v.get('url'), 'saved' if v.get('cache_updated') else 'not saved')
print('pending:', s.get('nickel', {}).get('setup', {}).get('pending_avatars'))
\""

echo "== 3. Settings: Search and suggestions, Privacy controls"
open_page chrome://os-settings/systemPreferences
echo "search card: $(js chrome://os-settings/systemPreferences "
  let card = null;
  for (let i = 0; i < 30 && !card; i++) {
    card = deep(document, 'search-and-assistant-settings-card');
    if (!card) await sleep(500);
  }
  card.scrollIntoView(); await sleep(1000);
  return {toggles: [...card.shadowRoot.querySelectorAll('settings-toggle-button')]
              .map(t => t.id),
          text: text(card.shadowRoot.querySelector('settings-card'))};")"
shot 3-search
open_page chrome://os-settings/osPrivacy/privacyHub
echo "privacy page: $(js chrome://os-settings/osPrivacy "
  await sleep(2000);
  return {recommendations: !!deep(document, '#contentRecommendationsToggle'),
          hub: !!deep(document, 'settings-privacy-hub-subpage')};")"

echo "== 4. Files: the home folder's folders"
# Through DevTools: no keyboard focus needed (cdp-eval.py --open).
python3 "$here/cdp-eval.py" 9323 --open chrome://file-manager; sleep 8
echo "sidebar: $(js chrome://file-manager "let items = [];
  for (let i = 0; i < 20; i++) {
    items = all(document, 'xf-tree-item');
    if (items.some(i => i.label === 'Videos')) break;
    await sleep(500);
  }
  return items.map(i => i.label + '=' + i.getAttribute('icon'));")"
shot 4-files

echo "== 5. Default apps; Gallery"
open_page chrome://os-settings/apps
echo "default apps: $(js chrome://os-settings/apps "
  let card = null;
  for (let i = 0; i < 40; i++) {
    card = deep(document, 'nickel-default-apps');
    if (card && card.kinds_ && card.kinds_.length) break;
    await sleep(500);
  }
  card.scrollIntoView(); await sleep(1000);
  return card.kinds_.map(k => k.id + ': ' +
      ((k.apps.find(a => a.taskId === k.defaultTaskId) || {}).title || 'none') +
      ' of [' + k.apps.map(a => a.title).join(', ') + ']');")"
shot 5-default-apps
open_page chrome://app-service-internals
echo "app service: $(js chrome://app-service-internals "$TEXT
  await sleep(3000);
  const t = fullText(document);
  return {gallery: /Gallery/.test(t), imageViewer: /Image Viewer|Loupe/.test(t)};")"

echo "== 6. names"
open_page chrome://os-settings/help
echo "about: $(js chrome://os-settings/help "$TEXT
  await sleep(3000);
  const t = fullText(document);
  const around = re => [...t.matchAll(re)].map(
      m => t.substring(Math.max(0, m.index - 40), m.index + 40)
               .replace(/\\s+/g, ' '));
  return {nickel: (t.match(/NickelOS/g) || []).length,
          chromium: around(/Chromium ?OS|ChromeOS|Chrome OS|Chromebook/g),
          title: document.title};")"
shot 6-about
open_page chrome://version
echo "version page: $(js chrome://version "$TEXT
  const t = fullText(document);
  const v = t.indexOf('Variations');
  return {variations: v < 0 ? null : t.substring(v, v + 120).replace(/\\s+/g, ' '),
          names: (t.match(/Chromium ?OS|ChromeOS|NickelOS/g) || [])};")"

echo "== 7. no Crostini"
open_page chrome://os-settings/help
echo "developers card: $(js chrome://os-settings/help "
  await sleep(2000);
  return deep(document, 'crostini-settings-card') ? 'shown' : 'none';")"
echo "settings search for linux: $(js chrome://os-settings/ "
  const box = deep(document, 'os-settings-search-box');
  const field = deep(box.shadowRoot || box, 'cr-toolbar-search-field');
  field.setValue('linux');
  await sleep(3000);
  const s16 = v => v && v.data ? String.fromCharCode(...v.data) : String(v);
  const hits = all(document, 'os-search-result-row').map(r =>
      s16(r.searchResult.canonicalText) + ' -> ' +
      r.searchResult.urlPathWithParameters);
  field.setValue('');
  return hits;")"
open_page chrome-untrusted://terminal/html/terminal_ssh.html
echo "terminal home: $(js chrome-untrusted://terminal/html/terminal_ssh.html "
  await sleep(3000);
  const app = deep(document, 'terminal-home-app');
  return app ? (app.shadowRoot || app).textContent.replace(/\\s+/g, ' ')
      .replace(/[^ -~]/g, '').trim() : null;")"
shot 7-terminal-home
# initctl's vm_ jobs, the VM binaries, kvm_host in Chrome's flags, Crostini
# on Chrome's command line.
echo "vm pieces: $(vm 'initctl list 2>/dev/null | grep -cE "^(vm_|seneschal|crosdns)"
  ls /usr/bin/vm_concierge /usr/bin/vm_cicerone /usr/bin/crosvm 2>/dev/null | wc -l
  grep -cx kvm_host /etc/ui_use_flags.txt
  tr "\0" "\n" < /proc/$(pgrep -o -x chrome)/cmdline | grep -c Crostini' | tr '\n' ' ')"

echo "crashes: $(vm 'ls /var/spool/crash/ 2>/dev/null | grep -ciE "chrome|nickel"')"
echo "screenshots in $D/flow/polish-*.png"
