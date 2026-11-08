#!/bin/bash
# Inside cros_sdk: symbolize thread 0 of a Chrome minidump for board nickel.
dmp=${1:-/mnt/host/source/.nickel/crash.dmp}
D=/build/nickel/usr/lib/debug/opt/google/chrome
addrs=$(minidump_stackwalk "$dmp" 2>/dev/null | sed -n '/^Thread 0 /,/^Thread 1 /p' |
  grep -oE '^ *[0-9]+  chrome \+ 0x[0-9a-f]+' | head -"${2:-18}" | awk '{print $4}')
llvm-symbolizer --obj="$D/chrome.debug" --dwp="$D/chrome.dwp" --relativenames -f -C $addrs |
  paste - - - | cut -c1-240
