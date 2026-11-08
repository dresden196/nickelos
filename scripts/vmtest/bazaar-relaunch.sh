#!/bin/bash
# Runs inside the test VM: stop the Bazaar started by the test, relaunch it
# with extra environment ("$@" as VAR=value), log to /tmp/bazaar-relaunch.log.
for p in $(pgrep -x bwrap) $(pgrep -x dbus-run-session); do kill "$p" 2>/dev/null; done
sleep 3
nohup sudo -u chronos env XDG_RUNTIME_DIR=/run/user/1000 WAYLAND_DISPLAY=wayland-0 "$@" \
  dbus-run-session -- flatpak run io.github.kolunmi.Bazaar > /tmp/bazaar-relaunch.log 2>&1 < /dev/null &
sleep 25
pgrep -ax bazaar | head -2
