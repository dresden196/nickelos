#!/bin/bash
# shot.sh NAME [KEYS...]: sends keys, then a screenshot to NAME.png. A key
# is a QMP qcode ("ret", "shift+a"), "wait" (a second), or "type:TEXT"
# (typed character by character).
W=/srv/nickel/sb-proto
name=$1; shift
qc() {
  case "$1" in
    [a-z0-9]) echo "$1";; [A-Z]) echo "shift+$(echo "$1" | tr A-Z a-z)";;
    ' ') echo spc;; '$') echo shift+4;; '(') echo shift+9;; ')') echo shift+0;;
    /) echo slash;; -) echo minus;; .) echo dot;; =) echo equal;; _) echo shift+minus;;
    :) echo shift+semicolon;; ,) echo comma;; '"') echo shift+apostrophe;; '*') echo shift+8;;
    *) echo spc;;
  esac
}
send() { echo "{\"execute\":\"send-key\",\"arguments\":{\"keys\":[$(echo "$1" | sed 's/[^+]*/{"type":"qcode","data":"&"}/g; s/+/,/g')]}}"; }
{ echo '{"execute":"qmp_capabilities"}'
  for k in "$@"; do
    case "$k" in
      wait) sleep 1;;
      type:*) t=${k#type:}; for ((i = 0; i < ${#t}; i++)); do send "$(qc "${t:$i:1}")"; sleep 0.08; done;;
      *) send "$k"; sleep 0.4;;
    esac
  done
  echo "{\"execute\":\"screendump\",\"arguments\":{\"filename\":\"$W/$name.ppm\"}}"
  sleep 1; } | socat - UNIX-CONNECT:${QMP:-$W/qmp.sock} >/dev/null
pnmtopng "$W/$name.ppm" > "$W/$name.png" 2>/dev/null
echo "$W/$name.png"
