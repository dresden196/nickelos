#!/bin/bash
# Runs on nickel-build: from the OOBE welcome screen of the running test VM,
# sign in as guest (same clicks as launcher-test.sh).
vm() { nickel-vmssh "$@"; }
state() { vm "dbus-send --system --print-reply --dest=org.chromium.SessionManager /org/chromium/SessionManager org.chromium.SessionManagerInterface.RetrieveSessionState 2>/dev/null" | grep -oE 'string "[a-z]+"' | tail -1; }
wait_log() { for i in $(seq 1 "${2:-40}"); do vm "grep -qs '$1' /var/log/chrome/chrome" && return 0; sleep 2; done; return 1; }
wait_log "AdvanceToScreen" 60; sleep 15
nickel-vmctl click 214 649
wait_log "AdvanceToScreen user-creation" 60 && sleep 15
nickel-vmctl click 87 776
wait_log "SetCurrentScreen: guest-tos" 60 && sleep 25
nickel-vmctl click 1028 647
for i in $(seq 1 30); do [ "$(state)" = 'string "started"' ] && { echo "guest session started"; break; }; sleep 3; done
sleep 20
