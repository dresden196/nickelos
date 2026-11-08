#!/bin/bash
# Applies seccomp-additions.txt to a mounted image root ($1). Text policies get
# "<call>: 1"; compiled ones (.bpf.policy) are rebuilt from platform2 source.
# On the build host by default (the prototype); nickel-unify, in the SDK,
# passes NICKEL_SRC (the source checkout), BOARD_ROOT and UF (this directory).
set -eu
R=$1
SRC=${NICKEL_SRC:-/srv/nickel/src}
BOARD_ROOT=${BOARD_ROOT:-/srv/nickel/src/out/build/nickel}
UF=${UF:-/srv/nickel/unified}
T=$SRC/src/platform/minijail/tools/compile_seccomp_policy.py
C=$BOARD_ROOT/build/share/constants.json
declare -A extra
while read -r f s why; do
	case $f in ""|\#*) continue ;; esac
	extra[$f]="${extra[$f]:-} $s"
done < "$UF/seccomp-additions.txt"
# A call minijail doesn't know by name for this board makes the whole policy
# fail to compile, and its service fails to start (mount_setattr did).
python3 - "$C" ${extra[@]} <<'PY'
import json, sys
known = json.load(open(sys.argv[1]))['syscalls']
bad = sorted({s for s in sys.argv[2:] if s not in known})
if bad:
    sys.exit('not in minijail\'s syscall table for this board: ' + ' '.join(bad))
PY
for f in "${!extra[@]}"; do
	p=$R/usr/share/policy/$f
	if [[ $f == *.bpf.policy ]]; then
		src=$(find "$SRC/src/platform2" -path "*/amd64/${f%.bpf.policy}.policy" | head -1)
		[ -n "$src" ] || { echo "no source for $f" >&2; exit 1; }
		tmp=$(mktemp); cp "$src" "$tmp"
		for s in ${extra[$f]}; do grep -qE "^$s:" "$tmp" || echo "$s: 1" >> "$tmp"; done
		lab=$(getfattr --only-values -n security.selinux "$p" 2>/dev/null || true)
		python3 $T --arch-json $C "$tmp" "$p"
		[ -n "$lab" ] && setfattr -n security.selinux -v "$lab" "$p"
		rm -f "$tmp"; echo "$f: rebuilt with${extra[$f]}"
	else
		for s in ${extra[$f]}; do grep -qE "^$s:" "$p" || echo "$s: 1" >> "$p"; done
		echo "$f: +${extra[$f]}"
	fi
done
