#!/bin/bash
# Runs on nickel-build, as root (it builds the test client), with tester
# signed in in the test VM (port 9222), nickel-xim installed and Chrome's
# remote debugging on (ime-test.sh turns it on). ChromeOS's input methods in
# X11 apps through XIM (nickel-xim, docs/vision.md), with src/x-ime-test.c,
# an Xlib client like Wine's, Java's or SDL's:
#   1. Pinyin in the three ways apps show a composition: drawn by the app
#      (callbacks: preedit lines, then 你好 committed), over the spot and
#      root (ChromeOS shows the composition in its candidate window, at the
#      app's spot for over-the-spot: $V/flow/xim-position.png)
#   2. Korean: the space that ends a syllable block reaches the app after
#      the block it ends ("한글 안녕 ", not " 한글 안녕"): keys the input method
#      lets through wait for its commit
#   3. Japanese (Mozc): にほんご converted to 日本語 and committed
# The session's own nickel-xim (nickel-xim job) serves; with none running
# (an image without the job) this starts one, NICKEL_XIM_DEBUG=1 logging to
# /tmp/xim.log in the VM.
set -u
V=/srv/nickel/vmtest
vm() { nickel-vmssh "$@" </dev/null; }

echo "== build the test client"
"$(dirname "$0")"/build-test-client.sh x-ime-test x11 || exit 1
# nickel-ime-select comes with ime-test.sh's tools.
vm "command -v nickel-ime-select >/dev/null" ||
  "$(dirname "$0")"/ime-test.sh --build-only >/dev/null || exit 1

# Picks an input method (nickel-ime-select in the VM). It works from an open
# page; right after a sign-in there may be none, so a browser window opens
# (Ctrl+N) and it tries again.
ime_select() {
  local out
  out=$(vm "nickel-ime-select $1" 2>&1)
  if [[ ${out} == *"no page to open Settings from"* ]]; then
    nickel-vmctl key ctrl-n
    sleep 5
    out=$(vm "nickel-ime-select $1" 2>&1)
  fi
  echo "${out}"
}

if ! vm "pgrep -x nickel-xim >/dev/null"; then
  vm "rm -f /tmp/xim.log; (nickel-session-exec env NICKEL_XIM_DEBUG=1 /usr/bin/nickel-xim > /tmp/xim.log 2>&1 &)"
  sleep 2
fi

# x-ime-test STYLE in the session for SECONDS, output in /tmp/xt.out.
client() {
  vm "rm -f /tmp/xt.out; (nickel-session-exec env XMODIFIERS=@im=nickel /usr/local/bin/x-ime-test $1 $2 > /tmp/xt.out 2>&1 &)"
  sleep 5
}
expect() {
  vm "grep -qxF 'text=$1' /tmp/xt.out" && echo "ok: $2" || {
    echo "FAIL: $2"
    vm "cat /tmp/xt.out"
  }
}

echo "== 1. Pinyin, three styles"
ime_select "pinyin"
for style in callbacks position root; do
  client "$style" 12
  nickel-vmctl type nihao
  sleep 1
  [ "$style" = position ] && nickel-vmsettle "$V/flow/xim-position.png" 3 >/dev/null 2>&1
  nickel-vmctl key spc
  sleep 8
  expect 你好 "$style commits 你好"
  [ "$style" = callbacks ] &&
    vm "grep -q '^preedit=ni hao' /tmp/xt.out" && echo "ok: callbacks draw the composition"
done

echo "== 2. Korean, in order"
ime_select "hangul_2set"
client callbacks 12
nickel-vmctl type gksrmf
nickel-vmctl key spc
nickel-vmctl type dkssud
nickel-vmctl key spc
sleep 8
expect "한글 안녕 " "the space after each block comes after it"

echo "== 3. Japanese"
ime_select "mozc_us"
client callbacks 14
nickel-vmctl type nihongo
sleep 1
nickel-vmctl key spc
sleep 1
nickel-vmctl key ret
sleep 9
expect 日本語 "にほんご converts to 日本語"
ime_select "xkb:us::eng" >/dev/null
