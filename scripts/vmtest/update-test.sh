#!/bin/bash
# Runs on a build host (nickel-next), as root: NickelOS's update road end to
# end in a VM (docs/vision.md "Updates"), the way a person would use it:
#   1. publish IMG_NEXT (version N+1) on the beta and stable channels of a
#      local nickel-update-server (scripts/update/), signed with the test
#      key (a test image keeps testimage-channel inside: Chrome refuses
#      test certificates on any other channel; update_engine reads it as
#      stable)
#   2. boot IMG_N (version N) in its own VM, set it up with an account, and
#      point its update_engine at that server (the stateful lsb-release
#      override, which update_engine honours on test images: cros_debug
#      makes them "unofficial" to it)
#   3. Settings > About > Additional details: the channel, whether this
#      account may change it (ChromeOS lets only the device owner) and the
#      auto-update toggle
#   4. switch to Beta there (or, if the account may not, with
#      update_engine_client, and say so)
#   5. About: "Check for updates" -> the Beta build downloads and applies ->
#      "Restart" shows
#   6. "Restart" -> the VM comes back on N+1 with the payload key
#      (nickel-update) in place; Settings: Stable, moving to Beta (a test
#      image's channel reads as stable)
#   7. back towards Stable: what the dialog offers (on a release image on
#      Beta only "Change channel and powerwash"; here, Stable being the
#      current channel, "Change channel"); cancelled. "Check for updates"
#      on Beta: up to date
#   8. the server's log: what the VM asked and reported
# Screenshots: $D/flow/update-*.png. The VM keeps running.
# UPDATE_TEST_REPUBLISH=1 makes the payloads again when they exist.
set -u
export NICKEL_VMTEST_DIR=${NICKEL_VMTEST_DIR:-/srv/nickel/vmtest-u}
export NICKEL_VMTEST_SSHPORT=${NICKEL_VMTEST_SSHPORT:-9225}
D=$NICKEL_VMTEST_DIR
here=$(dirname "$(readlink -f "$0")")
update=$here/../update
IMG_N=${IMG_N:?IMG_N: the qemu image to start from}
IMG_NEXT=${IMG_NEXT:?IMG_NEXT: the image to update to}
ROOT=$D/update-root
PORT=18090
CDP=9325
NAME="Tester Nickel"
PASS=nickel-test-pw
vm() { nickel-vmssh "$@"; }
state() { vm "dbus-send --system --print-reply --dest=org.chromium.SessionManager /org/chromium/SessionManager org.chromium.SessionManagerInterface.RetrieveSessionState 2>/dev/null" | grep -oE 'string "[a-z]+"' | tail -1; }
wait_log() { for i in $(seq 1 "${2:-40}"); do vm "grep -qs '$1' /var/log/chrome/chrome" && return 0; sleep 2; done; echo "TIMEOUT waiting for: $1"; return 1; }
wait_state() { for i in $(seq 1 "${2:-30}"); do [ "$(state)" = "string \"$1\"" ] && return 0; sleep 3; done; echo "TIMEOUT waiting for session state $1 (now $(state))"; return 1; }
wait_ssh() { sleep 15; for i in $(seq 1 90); do timeout 15 nickel-vmssh true 2>/dev/null && return 0; sleep 4; done; echo "SSH NEVER CAME UP"; exit 1; }
shot() { sleep 2; nickel-vmctl shot "$D/flow/update-$1.png"; }
ue_channel() { vm 'update_engine_client --show_channel 2>&1' | grep -o -E "(Current|Target) Channel[^:]*: [a-z-]+" | tr '\n' ' '; }
ue_status() { vm 'update_engine_client --status 2>/dev/null' | sed -n 's/^CURRENT_OP=//p'; }

mkdir -p "$D/flow"

echo "== 1. publish N+1 on beta and stable"
next_version=$(losetup -f --show -P -r "$IMG_NEXT" | { read -r l; m=$(mktemp -d); mount -o ro "${l}p3" "$m"
  sed -n 's/^CHROMEOS_RELEASE_VERSION=//p' "$m/etc/lsb-release"; umount "$m"; rmdir "$m"; losetup -d "$l"; })
for ch in beta stable; do
  if [ -f "$ROOT/channels/$ch/nickelos-$next_version-$ch-full.json" ] && [ -z "${UPDATE_TEST_REPUBLISH:-}" ]; then
    echo "$next_version already on $ch"
  else
    "$update/nickel-update-publish" "$IMG_NEXT" "$ch" "$ROOT" || exit 1
  fi
done
"$update/nickel-update-channels" list "$ROOT"
systemctl stop nickel-update-test-server 2>/dev/null; systemctl reset-failed nickel-update-test-server 2>/dev/null
: > "$D/update-server.log"
systemd-run --unit nickel-update-test-server --collect \
  /usr/bin/python3 "$update/nickel-update-server" --root "$ROOT" --bind 127.0.0.1 \
  --port "$PORT" --payloads-url "http://10.0.2.2:$PORT/payloads" --log "$D/update-server.log" >/dev/null
# It reads the payloads before it listens: a minute at most (13 s with two
# on a loaded build host).
for _ in $(seq 30); do
  curl -sf "http://127.0.0.1:$PORT/healthz" >/dev/null && break
  sleep 2
done
curl -sf "http://127.0.0.1:$PORT/healthz" >/dev/null || { echo "the update server didn't start"; exit 1; }

echo "== 2. VM on N, set up"
unit=nickel-$(basename "$D")
systemctl stop "$unit" 2>/dev/null; systemctl reset-failed "$unit" 2>/dev/null
systemd-run --unit "$unit" --collect -E NICKEL_VMTEST_DIR="$D" \
  -E NICKEL_VMTEST_SSHPORT="$NICKEL_VMTEST_SSHPORT" \
  /usr/local/bin/nickel-vmtest "$IMG_N" >/dev/null
sleep 5
systemctl is-active -q "$unit" || { echo "the VM did not start"; journalctl -u "$unit" | tail -5; exit 1; }
tunnel=
cleanup() { [ -n "$tunnel" ] && kill "$tunnel" 2>/dev/null; }
trap cleanup EXIT
wait_ssh
before=$(vm 'sed -n s/^CHROMEOS_RELEASE_VERSION=//p /etc/lsb-release')
echo "version: $before, image channel: $(vm 'sed -n s/^CHROMEOS_RELEASE_TRACK=//p /etc/lsb-release')"
echo "crossystem: debug_build=$(vm 'crossystem debug_build 2>/dev/null') devsw_boot=$(vm 'crossystem devsw_boot 2>/dev/null')"

wait_log "AdvanceToScreen" 60; sleep 15
# "Get started": a click can come before the button takes it (17.4's
# release check: setup never began); again until setup's next screen.
for _ in 1 2 3; do
  nickel-vmctl click 214 649
  wait_log "SetCurrentScreen: nickel-setup" 15 >/dev/null && break
done
sleep 5
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
# As setup-account.sh's setup_focus_name and setup_name_to_password: an
# image whose setup names the computer (Chrome 0102, nickel-hostname r1) has
# that field after the name, and the form higher up.
names_computer=0
vm 'grep -q in_setup /usr/libexec/nickel-hostname/nickel-hostnamed 2>/dev/null' &&
  names_computer=1
if [ "$names_computer" = 1 ]; then
  nickel-vmctl click 850 177; sleep 1
else
  nickel-vmctl click 850 250; sleep 1
fi
nickel-vmctl type "$NAME"; sleep 1
if [ "$names_computer" = 1 ]; then
  nickel-vmctl key tab; sleep 0.5
fi
nickel-vmctl key tab; sleep 0.5; nickel-vmctl key tab; sleep 0.5
nickel-vmctl type "$PASS"; sleep 1
nickel-vmctl key tab; sleep 0.5
nickel-vmctl type "$PASS"
sleep 8
nickel-vmctl click 1077 647
wait_state started 40 && echo "session started"
# NickelOS's Welcome Tour comes at a new account's first sign-in, over
# everything: no thanks (empty desktop there when it isn't up), until the
# profile says it was declined, 90 s at most.
for _ in $(seq 18); do
  nickel-vmctl click 712 594; sleep 5
  nickel-vmssh "grep -q '\"welcome_tour\":{\"v3\":{\"aborted\"' /home/chronos/user/Preferences 2>/dev/null" </dev/null && break
done
sleep 45

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
# Chrome with DevTools on the VM's port 9223 (the line goes on the running
# system partition, so again after the update), here on $CDP.
devtools() {
  vm "mount -o remount,rw / 2>/dev/null
grep -q remote-debugging-port=9223 /etc/chrome_dev.conf ||
  echo --remote-debugging-port=9223 >> /etc/chrome_dev.conf
grep -q hide-crash-restore-bubble /etc/chrome_dev.conf ||
  echo --hide-crash-restore-bubble >> /etc/chrome_dev.conf
restart ui >/dev/null"
  sign_in
  ssh -i /root/.ssh/cros_testing_rsa -p "$NICKEL_VMTEST_SSHPORT" \
    -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR \
    -N -L "$CDP:127.0.0.1:9223" root@127.0.0.1 &
  tunnel=$!
  sleep 3
}
# update_engine asks the local server (stateful, so it stays across the
# update).
vm "mkdir -p /mnt/stateful_partition/etc
echo CHROMEOS_AUSERVER=http://10.0.2.2:$PORT/update > /mnt/stateful_partition/etc/lsb-release
restart update-engine >/dev/null"
devtools

js() {
  cat > /tmp/update-step.js <<JS
(async () => {
  const sleep = ms => new Promise(r => setTimeout(r, ms));
  const deep = (root, sel) => root.querySelector(sel) ||
      [...root.querySelectorAll('*')].reduce((hit, el) =>
          hit || (el.shadowRoot && deep(el.shadowRoot, sel)), null);
  const text = el => el ? el.innerText.replace(/\s+/g, ' ').trim() : null;
  const visible = el => !!el && !el.hidden && el.offsetParent !== null;
  $1
})()
JS
  python3 "$here/cdp-eval.py" "$CDP" chrome://os-settings /tmp/update-step.js | grep -v '^console:' |
    tr -d '\n' | sed -E 's/  +/ /g; s/^result: ?//'; echo
}
# Settings can take a while to come up after a sign-in; opened through
# DevTools (no keyboard focus needed; see cdp-eval.py --open), waited for,
# asked again if it does not come.
open_settings() {
  for try in 1 2 3; do
    curl -s -X PUT "http://127.0.0.1:$CDP/json/new?chrome://os-settings/help" >/dev/null
    for i in $(seq 1 20); do
      curl -s "http://127.0.0.1:$CDP/json/list" | grep -q '"url": "chrome://os-settings' &&
        { sleep 5; return 0; }
      sleep 3
    done
    echo "(Settings didn't open, try $try)"
  done
  return 1
}
details() {
  js "deep(document, '#detailedBuildInfoTrigger')?.click(); await sleep(3000);
  const b = deep(document, '#changeChannelButton');
  return {channel: text(deep(document, '#currentlyOnChannelText')),
          canChange: !!b && !b.disabled,
          policyIndicator: !!deep(document, '#changeChannelPolicyIndicator'),
          autoUpdateToggle: !!deep(document, '#consumerAutoUpdateToggle')};"
}
# Opens the channel dialog and selects CHANNEL; prints the buttons and
# warning it shows; ACTION: change (press "Change channel"), cancel.
channel_dialog() {
  js "deep(document, '#detailedBuildInfoTrigger')?.click(); await sleep(2000);
  const b = deep(document, '#changeChannelButton');
  if (!b || b.disabled) return 'not allowed for this account';
  b.click(); await sleep(2000);
  const d = deep(document, 'settings-channel-switcher-dialog');
  if (!d) return 'no dialog';
  const r = [...d.shadowRoot.querySelectorAll('cr-radio-button')].find(r => r.name === '$1-channel');
  r.click(); await sleep(800);
  const btn = id => d.shadowRoot.querySelector(id);
  const w = d.shadowRoot.querySelector('#warningSelector .iron-selected');
  const seen = {change: !btn('#changeChannel').hidden,
                powerwash: !btn('#changeChannelAndPowerwash').hidden,
                warning: text(w)};
  if ('$2' === 'change' && seen.change) { btn('#changeChannel').click(); seen.pressed = 'change'; }
  else { btn('#cancel').click(); seen.pressed = 'cancel'; }
  await sleep(2000);
  // Settings asks update_engine for the channel again once the dialog
  // closes, which can answer late while it starts a check (17.34's release
  // check read stable after 2 s); whether the dialog is still up is noted.
  seen.dialogOpen = !!deep(document, 'settings-channel-switcher-dialog')?.shadowRoot
      ?.querySelector('cr-dialog')?.open;
  for (let i = 0; i < 8 && '$2' === 'change' && seen.change &&
       !(text(deep(document, '#currentlyOnChannelText')) || '').includes('$1'); i++)
    await sleep(1000);
  seen.after = text(deep(document, '#currentlyOnChannelText'));
  return seen;"
}
check_updates() {
  js "history.pushState({}, '', '/help');
  window.dispatchEvent(new PopStateEvent('popstate')); await sleep(2000);
  const b = deep(document, '#checkForUpdatesButton');
  const before = text(deep(document, '#updateStatusMessage'));
  if (!b || b.hidden) return {before, check: 'no button'};
  b.click(); await sleep(4000);
  return {before, after: text(deep(document, '#updateStatusMessage'))};"
}

echo "== 3. Settings > About > Additional details"
open_settings
shot 3-about
echo "details: $(details)"
echo "update_engine: $(ue_channel)"
shot 3-details

echo "== 4. switch to Beta"
r=$(channel_dialog beta change)
echo "dialog: $r"
case $r in *"not allowed"*|"no dialog")
  echo "!! Settings would not change the channel for this account; update_engine_client instead"
  vm 'update_engine_client --channel=beta-channel 2>&1 | tail -1';;
esac
# update_engine starts its check as soon as the channel changes, and a
# --show_channel then can answer nothing (17.8's release check): ask again.
ch=
for _ in 1 2 3 4 5; do
  ch=$(ue_channel)
  [ -n "$ch" ] && break
  sleep 2
done
echo "update_engine: $ch"
shot 4-beta

echo "== 5. About: Check for updates"
echo "check: $(check_updates)"
for i in $(seq 1 240); do
  op=$(ue_status)
  case $op in UPDATE_STATUS_UPDATED_NEED_REBOOT|UPDATE_STATUS_IDLE|UPDATE_STATUS_REPORTING_ERROR_EVENT) [ "$i" -gt 3 ] && break;; esac
  sleep 5
done
echo "update_engine: $op after $((i * 5)) s"
echo "settings: $(js "await sleep(2000);
  return {status: text(deep(document, '#updateStatusMessage')),
          restart: visible(deep(document, '#relaunchButton'))};")"
shot 5-restart

echo "== 6. Restart"
js "deep(document, '#relaunchButton')?.click(); return 'restart pressed';"
kill "$tunnel" 2>/dev/null; tunnel=
sleep 30
wait_ssh
after=$(vm 'sed -n s/^CHROMEOS_RELEASE_VERSION=//p /etc/lsb-release')
echo "version: $before -> $after, image channel: $(vm 'sed -n s/^CHROMEOS_RELEASE_TRACK=//p /etc/lsb-release')"
echo "payload key: $(vm 'ls /usr/share/update_engine/update-payload-key.pub.pem 2>&1')"
echo "update_engine: $(ue_status), $(ue_channel)"
devtools
open_settings
echo "details: $(details)"
shot 6-after

echo "== 7. back towards Stable, and Check for updates on Beta"
echo "dialog: $(channel_dialog stable cancel)"
shot 7-stable-dialog
echo "check: $(check_updates)"
sleep 20
echo "update_engine: $(ue_status)"
echo "settings: $(js "return text(deep(document, '#updateStatusMessage'));")"
shot 7-uptodate

echo "== 8. the server saw"
sed -E 's/^[0-9-]+ [0-9:,]+ //' "$D/update-server.log" | cut -c1-150 | uniq -c | tail -25
echo "crashes: $(vm 'ls /var/spool/crash/ 2>/dev/null | grep -ciE "update_engine|chrome"')"
echo "screenshots in $D/flow/update-*.png"
