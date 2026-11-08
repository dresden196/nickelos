#!/bin/bash
# Runs on the build host, as root: the language chosen on the welcome screen
# (vision: "Language in setup") followed through to a new local account and
# its Linux apps, on a fresh image (latest, or IMG=) in its own VM
# (NICKEL_VMTEST_DIR /srv/nickel/vmtest-x, ssh port 9228):
#   language-test.sh [CHROME_LOCALE [XKB]]   (default: de de::ger)
#   1. the welcome screen in that language and with that keyboard (its
#      setLocaleId and setInputMethodId, through DevTools), then setup and
#      the account, Minimal
#   2. what the account got: Chrome's language (Local State and the
#      profile's intl), its keyboard layouts and input methods
#   3. Linux apps: the session's LANG and LANGUAGE, the locale there
#      (locale -a), a program's date in that language, Flatpak's languages,
#      Gentoo's L10N
# NICKEL_SESSION_DIR= puts a nickel-session files directory's
# nickel-session-setup and nickel-session-locale into the VM first (signed
# out and in again), to try them without an image.
# Screenshots: $D/flow/language-*.png. The VM keeps running.
set -u
LOCALE=${1:-de}
KEYBOARD=${2:-de::ger}
IME=_comp_ime_fgoepimhcoialccpbmpnnblemnepkkaoxkb:$KEYBOARD
export NICKEL_VMTEST_DIR=${NICKEL_VMTEST_DIR:-/srv/nickel/vmtest-x}
export NICKEL_VMTEST_SSHPORT=${NICKEL_VMTEST_SSHPORT:-9228}
export NICKEL_TEST_EXPERIENCE=minimal
D=$NICKEL_VMTEST_DIR
V=$D
here=$(dirname "$(readlink -f "$0")")
IMG=${IMG:-/srv/nickel/src/src/build/images/nickel/latest/chromiumos_qemu_image.bin}
NAME="Tester Nickel"
PASS=nickel-test-pw
DEVTOOLS=9328
vm() { nickel-vmssh "$@" </dev/null; }
state() { vm "dbus-send --system --print-reply --dest=org.chromium.SessionManager /org/chromium/SessionManager org.chromium.SessionManagerInterface.RetrieveSessionState 2>/dev/null" | grep -oE 'string "[a-z]+"' | tail -1; }
wait_log() { for i in $(seq 1 "${2:-40}"); do vm "grep -qs '$1' /var/log/chrome/chrome" && return 0; sleep 2; done; echo "TIMEOUT waiting for: $1"; return 1; }
wait_state() { for i in $(seq 1 "${2:-30}"); do [ "$(state)" = "string \"$1\"" ] && return 0; sleep 3; done; echo "TIMEOUT waiting for session state $1 (now $(state))"; return 1; }
wait_ssh() { sleep 15; for i in $(seq 1 90); do timeout 15 nickel-vmssh true 2>/dev/null && return 0; sleep 4; done; echo "SSH NEVER CAME UP"; exit 1; }
shot() { sleep 2; nickel-vmctl shot "$D/flow/language-$LOCALE-$1.png"; }
json() {  # FILE (in the VM) KEY.PATH: that value as JSON
  vm "python3 -c \"
import json
p = json.load(open('$1'))
for k in '$2'.split('.'):
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
wait_log "AdvanceToScreen" 60
# DevTools, and Chrome's search engine choice screen off as in the other
# test VMs (setup-account.sh), in one restart: another after the welcome
# screen would lose the keyboard chosen there.
vm 'mount -o remount,rw / 2>/dev/null
echo --remote-debugging-port=9223 >> /etc/chrome_dev.conf
echo --disable-search-engine-choice-screen >> /etc/chrome_dev.conf
stop ui >/dev/null; rm -f /var/log/chrome/chrome; start ui >/dev/null'
if [ -n "${NICKEL_SESSION_DIR:-}" ]; then
  for f in nickel-session-setup:/usr/sbin nickel-session-locale:/usr/libexec/nickel-session; do
    "$here"/nickel-vmpush "${f#*:}/${f%%:*}" < "$NICKEL_SESSION_DIR/${f%%:*}"
  done
  echo "nickel-session: $NICKEL_SESSION_DIR"
fi
wait_log "AdvanceToScreen" 60; sleep 10
ssh -i /root/.ssh/cros_testing_rsa -p "$NICKEL_VMTEST_SSHPORT" \
  -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR \
  -N -L $DEVTOOLS:127.0.0.1:9223 root@127.0.0.1 &
tunnel=$!
sleep 3

echo "== 1. welcome screen: $LOCALE, keyboard $KEYBOARD"
cat > /tmp/language-step.js <<JS
(async () => {
  chrome.send('login.WelcomeScreen.userActed', ['setLocaleId', '$LOCALE']);
  await new Promise(r => setTimeout(r, 8000));
  chrome.send('login.WelcomeScreen.userActed', ['setInputMethodId', '$IME']);
  await new Promise(r => setTimeout(r, 3000));
  return document.documentElement.lang;
})()
JS
echo "page language: $(python3 "$here/cdp-eval.py" $DEVTOOLS chrome://oobe /tmp/language-step.js | sed -n 's/^result: //p')"
shot 1-welcome
echo "Local State: $(json '/home/chronos/Local State' intl)"
. "$here/setup-account.sh"
# setup_account waits for the welcome screen's first AdvanceToScreen, long
# past: a fresh log line for it.
vm "echo AdvanceToScreen >> /var/log/chrome/chrome"
setup_account "$NAME" "$PASS" "language-$LOCALE" || { shot 1-setup-failed; exit 1; }
for _ in $(seq 18); do
  nickel-vmctl click 712 594; sleep 5
  vm "grep -q '\"welcome_tour\":{\"v3\":{\"aborted\"' /home/chronos/user/Preferences" && break
done
sleep 20
shot 2-session

echo "== 2. the account"
echo "Local State: $(json '/home/chronos/Local State' intl)"
echo "profile intl: $(json /home/chronos/user/Preferences intl)"
echo "profile input methods: $(vm "python3 -c \"
import json
l = json.load(open('/home/chronos/user/Preferences')).get('settings', {}).get('language', {})
print(json.dumps({k: l.get(k) for k in ('preload_engines', 'enabled_extension_imes', 'preferred_languages', 'current_input_method')}))\"")"
vm "grep -m1 -oE 'locale[=: ]+\"?[a-zA-Z-]+' /home/chronos/user/log/chrome 2>/dev/null" | sed 's/^/chrome log: /'

echo "== 3. Linux apps"
vm 'grep -E "^LANG|^LANGUAGE|^LC_" /run/nickel/session/env' | sed 's/^/session: /'
echo "locales: $(vm 'locale -a' | tr '\n' ' ')"
echo "a program's date: $(vm 'nickel-session-exec sh -c "date +%A,%B"')"
echo "Flatpak languages: $(vm 'flatpak config --system --get languages 2>&1; flatpak config --system --get extra-languages 2>&1' | tr '\n' ' ')"
echo "Gentoo L10N: $(vm 'grep -h "^L10N" /etc/portage/make.conf/0210_nickelos_languages.conf 2>&1')"
echo "== done"
