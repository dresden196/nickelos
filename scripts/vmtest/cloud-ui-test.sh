#!/bin/bash
# Runs on the build host, as root: cloud storage (nickel-cloud, Chrome patch
# 0050) in a signed-in session, from a fresh image (latest, or IMG=) in its
# own VM (NICKEL_VMTEST_DIR /srv/nickel/vmtest-b, ssh port 9224), clicking
# through Chrome's DevTools (cdp-eval.py):
#   1. setup and the account "Tester Nickel" (as local-account-test.sh)
#   2. Settings > System preferences > Files: Cloud storage first, no
#      accounts; no Google Drive, OneDrive or Microsoft 365 (patch 0051)
#   3. Add account: "Nextcloud or WebDAV" against rclone serve webdav on this
#      host (10.0.2.2 in the VM; the VM downloads rclone itself), named
#      "Test cloud"; the card lists it "In Files"
#   4. the account in the VM: the server's file is there, a file written
#      there reaches the server
#   5. Files shows it in the sidebar and lists its files; no Google Drive
#      (DriveFS, patch 0052) there; 5b. its folders unfold in the folder
#      tree (its expand arrow), down a level; only folders with folders in
#      them have an arrow (Photos, not Documents); 5c. a folder the server
#      won't list (Vault, as OneDrive's Personal Vault) leaves the tree
#      working
#   6. Remove: asks first, the account, its mount and its Files entry go
#   7. Google Drive with a made-up client ID gets as far as Google's sign-in
#      page in the browser (which refuses the client); Cancel leaves nothing
# Screenshots: $D/flow/cloud-*.png. The VM keeps running. CHROME_DIR= puts
# another Chrome in the VM first (as desktop-ui-test.sh): a directory with the
# stripped chrome, its *.pak and locales/en-US.pak.
set -u
export NICKEL_VMTEST_DIR=${NICKEL_VMTEST_DIR:-/srv/nickel/vmtest-b}
export NICKEL_VMTEST_SSHPORT=${NICKEL_VMTEST_SSHPORT:-9224}
D=$NICKEL_VMTEST_DIR
here=$(dirname "$(readlink -f "$0")")
IMG=${IMG:-/srv/nickel/src/src/build/images/nickel/latest/chromiumos_qemu_image.bin}
RCLONE=${RCLONE:-/usr/local/lib/nickel-cloud-test/rclone}
DAV_PORT=18081
NAME="Tester Nickel"
PASS=nickel-test-pw
vm() { nickel-vmssh "$@"; }
state() { vm "dbus-send --system --print-reply --dest=org.chromium.SessionManager /org/chromium/SessionManager org.chromium.SessionManagerInterface.RetrieveSessionState 2>/dev/null" | grep -oE 'string "[a-z]+"' | tail -1; }
wait_log() { for i in $(seq 1 "${2:-40}"); do vm "grep -qs '$1' /var/log/chrome/chrome" && return 0; sleep 2; done; echo "TIMEOUT waiting for: $1"; return 1; }
wait_state() { for i in $(seq 1 "${2:-30}"); do [ "$(state)" = "string \"$1\"" ] && return 0; sleep 3; done; echo "TIMEOUT waiting for session state $1 (now $(state))"; return 1; }
wait_ssh() { sleep 15; for i in $(seq 1 90); do timeout 15 nickel-vmssh true 2>/dev/null && return 0; sleep 4; done; echo "SSH NEVER CAME UP"; exit 1; }
shot() { sleep 2; nickel-vmctl shot "$D/flow/cloud-$1.png"; }

[ -x "$RCLONE" ] || { echo "no rclone at $RCLONE"; exit 1; }
mkdir -p "$D/flow"
unit=nickel-$(basename "$D")
systemctl stop "$unit" 2>/dev/null; systemctl reset-failed "$unit" 2>/dev/null
systemd-run --unit "$unit" --collect -E NICKEL_VMTEST_DIR="$D" \
  -E NICKEL_VMTEST_SSHPORT="$NICKEL_VMTEST_SSHPORT" \
  /usr/local/bin/nickel-vmtest "$IMG" >/dev/null
sleep 5
systemctl is-active -q "$unit" || { echo "the VM did not start"; journalctl -u "$unit" | tail -5; exit 1; }

# The WebDAV server, on this host's loopback (the VM's 10.0.2.2), as nobody:
# Vault is a folder the server won't list (as OneDrive's Personal Vault).
dav=$D/davroot
rm -rf "$dav"; mkdir -p "$dav/Photos/2026" "$dav/Documents" "$dav/Vault"
echo "hello from the server" > "$dav/hello.txt"
: > "$D/dav.log"
chown -R nobody:nogroup "$dav" "$D/dav.log"
chmod 000 "$dav/Vault"
setpriv --reuid=nobody --regid=nogroup --clear-groups env HOME=/tmp \
  "$RCLONE" serve webdav "$dav" --addr "127.0.0.1:$DAV_PORT" --user u --pass p \
  --config /dev/null --log-file "$D/dav.log" &
davpid=$!
tunnel=
cleanup() {
  kill "$davpid" 2>/dev/null
  [ -n "$tunnel" ] && kill "$tunnel" 2>/dev/null
}
trap cleanup EXIT
wait_ssh

if [ -n "${CHROME_DIR:-}" ]; then
  wait_log "AdvanceToScreen" 60
  vm 'mount -o remount,rw / 2>/dev/null; rm -rf /usr/local/chrome.new; mkdir -p /usr/local/chrome.new'
  tar -C "$CHROME_DIR" -cf - chrome $(cd "$CHROME_DIR" && ls *.pak) locales/en-US.pak |
    vm 'tar -C /usr/local/chrome.new -xf -'
  vm 'stop ui >/dev/null; cd /usr/local/chrome.new && for f in chrome *.pak locales/en-US.pak; do mv -f "$f" "/opt/google/chrome/$f"; done
cd /; rm -rf /usr/local/chrome.new /var/log/chrome/chrome; start ui >/dev/null'
  echo "Chrome: $CHROME_DIR"
fi

echo "== 1. setup, account $NAME"
. "$(dirname "$0")/setup-account.sh"
setup_account "$NAME" "$PASS" cloud
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

# The session's user, and its runtime directory.
uid=$(vm "stat -c %u /run/user/* 2>/dev/null | grep -v '^0$' | head -1")
drives=/run/user/$uid/nickel-cloud/drives
as_user() { vm "sudo -u '#$uid' sh -c \"$1\""; }
cloud_state() {
  as_user "DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/$uid/bus gdbus call --session --dest org.nickelos.Cloud --object-path /org/nickelos/Cloud --method org.nickelos.Cloud.GetState" |
    sed -E "s/^\('//; s/',\)$//"
}

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
  let card = null;
  for (let i = 0; i < 30 && !card; i++) {
    card = deep(document, 'files-settings-card');
    if (!card) await sleep(500);
  }
  const sr = card && card.shadowRoot;
  const dialog = () => sr.querySelector('nickel-cloud-dialog');
  const field = k => dialog().shadowRoot.querySelector('cr-input[data-key="' + k + '"]');
  const accounts = () => card.nickelCloudAccounts_.map(
      a => a.name + (a.mounted ? ' (mounted)' : '') + (a.error ? ' ' + a.error : ''));
  const waitStep = async (steps, seconds) => {
    for (let i = 0; i < seconds * 2 && !steps.includes(dialog().step_); i++) {
      await sleep(500);
    }
    return dialog().step_;
  };
  $2
})()
JS
  # The page's console messages (replayed when DevTools attaches) aren't
  # the result.
  python3 "$here/cdp-eval.py" 9323 "$1" /tmp/cloud-step.js | grep -v '^console:' |
    tr -d '\n' | sed -E 's/  +/ /g; s/^result: ?//'; echo
}
settings() { js chrome://os-settings "$1"; }
files_labels() {
  js chrome://file-manager "let labels = [];
  for (let i = 0; i < 20; i++) {
    labels = all(document, 'xf-tree-item').map(i => i.label);
    if (labels.includes('$1') === $2) break;
    await sleep(500);
  }
  return labels;"
}

echo "== 2. Settings > System preferences > Files"
# Through DevTools: no keyboard focus needed (cdp-eval.py --open).
python3 "$here/cdp-eval.py" 9323 --open chrome://os-settings/systemPreferences
sleep 5
echo "card: $(settings "card.scrollIntoView(); await sleep(1000);
  const first = [...sr.querySelector('settings-card').children]
      .find(el => el.localName !== 'template');
  return {row: text(sr.querySelector('#nickelCloudRow')), accounts: accounts(),
          first: first && first.id,
          gone: ['#googleDriveRow', '#oneDriveRow', '#officeRow']
              .filter(id => !sr.querySelector(id)).length + ' of 3'};")"
shot 2-card

echo "== 3. Add account: Nextcloud or WebDAV"
echo "dialog: $(settings "sr.querySelector('#nickelCloudAddButton').click();
  await sleep(1500);
  const dr = dialog().shadowRoot;
  const services = [...dr.querySelectorAll('option')].map(o => o.value);
  const select = dr.querySelector('select');
  select.value = 'webdav'; select.dispatchEvent(new Event('change'));
  await sleep(800);
  dr.querySelector('cr-input').value = 'Test cloud';
  field('url').value = 'http://10.0.2.2:$DAV_PORT';
  field('user').value = 'u';
  field('pass').value = 'p';
  await sleep(500);
  return {services, name: dialog().name_,
          canConnect: !dr.querySelector('.action-button').disabled};")"
shot 3-dialog
echo "connect: $(settings "const dr = dialog().shadowRoot;
  dr.querySelector('.action-button').click();
  const step = await waitStep(['done', 'error', 'question'], 180);
  return {step, says: text(dr.querySelector('[slot=body]'))};")"
shot 4-done
echo "card: $(settings "dialog().shadowRoot.querySelector('.action-button').click();
  await sleep(1000);
  for (let i = 0; i < 40 && !accounts().some(a => a.includes('mounted')); i++) {
    await sleep(500);
  }
  return {dialogOpen: !!dialog(), accounts: accounts(),
          rows: [...sr.querySelectorAll('.nickel-cloud-account')].map(text)};")"
shot 5-account

echo "== 4. the account in the VM"
echo "mounted: $(vm "grep -c ' $drives/Test\\\\040cloud fuse' /proc/mounts")"
echo "files: $(as_user "ls '$drives/Test cloud'" | tr '\n' ' ')"
echo "server's file: $(as_user "cat '$drives/Test cloud/hello.txt'")"
as_user "echo written in the VM > '$drives/Test cloud/from-vm.txt'"
for i in $(seq 1 30); do [ -s "$dav/from-vm.txt" ] && break; sleep 1; done
echo "on the server after ${i}s: $(cat "$dav/from-vm.txt" 2>/dev/null || echo missing)"

echo "== 5. Files"
# Through DevTools: no keyboard focus needed (cdp-eval.py --open).
python3 "$here/cdp-eval.py" 9323 --open chrome://file-manager; sleep 8
echo "sidebar: $(files_labels "Test cloud" true)"
echo "Google Drive in the sidebar (patch 0052 says no): $(files_labels "Google Drive" false | grep -c '"Google Drive"')"
echo "listing: $(js chrome://file-manager "const item = all(document, 'xf-tree-item')
      .find(i => i.label === 'Test cloud');
  if (!item) return 'no item';
  item.shadowRoot.querySelector('.tree-row').click();
  let names = [];
  for (let i = 0; i < 30; i++) {
    names = all(document, '.entry-name').map(text);
    if (names.includes('hello.txt')) break;
    await sleep(500);
  }
  return names;")"
shot 6-files

echo "== 5b. the folder tree: unfold the account"
echo "tree: $(js chrome://file-manager "const item = all(document, 'xf-tree-item')
      .find(i => i.label === 'Test cloud');
  if (!item) return 'no item';
  const children = () => [...item.querySelectorAll(':scope > xf-tree-item')]
      .map(c => c.label);
  item.expanded = false;
  await sleep(500);
  const arrow = item.shadowRoot.querySelector('.expand-icon');
  if (!arrow) return 'no arrow';
  arrow.click();
  for (let i = 0; i < 20 && !children().includes('Photos'); i++) {
    await sleep(500);
  }
  const top = children();
  const photos = [...item.querySelectorAll(':scope > xf-tree-item')]
      .find(c => c.label === 'Photos');
  let below = null;
  if (photos) {
    photos.shadowRoot.querySelector('.expand-icon')?.click();
    for (let i = 0; i < 20; i++) {
      below = [...photos.querySelectorAll(':scope > xf-tree-item')]
          .map(c => c.label);
      if (below.includes('2026')) break;
      await sleep(500);
    }
  }
  await sleep(3000);
  const arrows = Object.fromEntries(
      [...item.querySelectorAll(':scope > xf-tree-item')]
          .map(c => [c.label, c.mayHaveChildren]));
  return {expanded: item.expanded, children: top, photos: below,
          mayHaveChildren: item.mayHaveChildren, arrows};")"
shot 6b-tree
# A folder the server won't list (OneDrive's Personal Vault): unfolding it
# leaves the rest of the tree as it was, and the account folds and unfolds
# again.
echo "unlistable folder: $(js chrome://file-manager "const item = all(document, 'xf-tree-item')
      .find(i => i.label === 'Test cloud');
  if (!item) return 'no item';
  const kids = (i) => [...i.querySelectorAll(':scope > xf-tree-item')];
  const vault = kids(item).find(c => c.label === 'Vault');
  if (!vault) return {children: kids(item).map(c => c.label)};
  vault.shadowRoot.querySelector('.expand-icon')?.click();
  await sleep(4000);
  const after = {vault: {expanded: vault.expanded,
                         mayHaveChildren: vault.mayHaveChildren,
                         children: kids(vault).map(c => c.label)},
                 children: kids(item).map(c => c.label)};
  item.shadowRoot.querySelector('.expand-icon').click();
  await sleep(1000);
  const folded = {expanded: item.expanded, children: kids(item).length};
  item.shadowRoot.querySelector('.expand-icon').click();
  for (let i = 0; i < 20 && !kids(item).some(c => c.label === 'Photos'); i++) {
    await sleep(500);
  }
  return {after, folded, again: {expanded: item.expanded,
                                 children: kids(item).map(c => c.label)}};")"
shot 6c-tree
echo "Files' console: $(vm "grep -a 'CONSOLE' /var/log/chrome/chrome | grep -a file-manager | grep -a -v -E 'Files app|olumes|Getting volumes' | sed -E 's/.*CONSOLE[^\"]*//' | sort | uniq -c | sort -rn | head -8")"

echo "== 6. Remove"
echo "remove: $(settings "sr.querySelector('.nickel-cloud-account cr-icon-button').click();
  await sleep(1000);
  const rd = sr.querySelector('#nickelCloudRemoveDialog');
  const asked = rd.open, title = text(rd.querySelector('[slot=title]'));
  rd.querySelector('.action-button').click();
  for (let i = 0; i < 90 && accounts().length; i++) await sleep(500);
  return {asked, title, accounts: accounts(), error: card.nickelCloudError_};")"
echo "mounted: $(vm "grep -c ' $drives/Test\\\\040cloud ' /proc/mounts")"
echo "sidebar: $(files_labels "Test cloud" false)"
echo "server kept: $(ls "$dav" | tr '\n' ' ')"
echo "state: $(cloud_state)"

echo "== 7. Google Drive, up to Google's sign-in page"
echo "connect: $(settings "sr.querySelector('#nickelCloudAddButton').click();
  await sleep(1500);
  const dr = dialog().shadowRoot;
  const select = dr.querySelector('select');
  select.value = 'drive'; select.dispatchEvent(new Event('change'));
  await sleep(800);
  const help = text(dr.querySelector('.note'));
  field('client_id').value = 'nickel-test.apps.googleusercontent.com';
  field('client_secret').value = 'nickel-test-secret';
  await sleep(500);
  dr.querySelector('.action-button').click();
  const step = await waitStep(['signin', 'error', 'question', 'done'], 60);
  return {help, step, says: text(dr.querySelector('[slot=body]'))};")"
sleep 10
echo "browser: $(curl -s http://127.0.0.1:9323/json/list | python3 -c '
import json, sys
print([p["url"][:60] for p in json.load(sys.stdin)
       if "google.com" in p["url"] or "53682" in p["url"]])')"
shot 7-google
echo "rclone's sign-in server: $(vm "ss -ltn | grep -c ':53682 '")"
# Back to Settings (its shelf icon), which the browser now covers: a hidden
# page gets no animation frames, so its dialog's close event would wait.
nickel-vmctl click 751 775; sleep 4
echo "cancel: $(settings "const dr = dialog().shadowRoot;
  dr.querySelector('.cancel-button').click();
  await sleep(3000);
  return {dialogOpen: !!dialog(), accounts: accounts(),
          visible: document.visibilityState};")"
sleep 5
echo "state: $(cloud_state)"
echo "rclone's sign-in server: $(vm "ss -ltn | grep -c ':53682 '")"
# rclone's configuration, where nickel-cloud keeps it.
echo "config: $(vm 'conf=$(ps -o args= -C rclone | grep -o -- "--config [^ ]*" | head -1)
  cat "${conf#--config }" 2>/dev/null' | tr '\n' ' ')"

echo "crashes: $(vm 'ls /var/spool/crash/ 2>/dev/null | grep -ciE "chrome|nickel|rclone"')"
echo "screenshots in $D/flow/cloud-*.png"
