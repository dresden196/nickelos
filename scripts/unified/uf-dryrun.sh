#!/bin/bash
# NickelOS: nickel-unify's dependency resolution without building an
# image, on the build host. The board's records of an image's packages (an
# image's package-sizes.json) go into a scratch root, the records step runs
# on them, then emerge --pretend as the hook runs it. Minutes, where a build
# takes 35 to reach the hook. Prints what emerge would do, or why it can't.
#   uf-dryrun.sh [PACKAGE_SIZES_JSON]
set -euo pipefail
U=/srv/nickel/unified
G=$U/root
O=/srv/nickel/src/src/overlays/overlay-nickel/scripts/unified
BR=/srv/nickel/src/out/build/nickel
B=$BR/var/db/pkg
J=${1:-$(ls -t /srv/nickel/src/src/build/images/nickel/*/chromiumos_base_image.bin-package-sizes.json | head -1)}
W=$U/work-p
R=$W/root
mountpoint -q "$G/mnt/image" && umount "$G/mnt/image"
rm -rf "$W"
mkdir -p "$R/var/db/pkg"
python3 - "$J" "$B" "$R/var/db/pkg" <<'PY'
import json, os, shutil, sys
j, b, out = sys.argv[1:]
n = 0
for k in json.load(open(j))['package_sizes']:
    cp, v = k.rsplit('/', 1)
    src = os.path.join(b, '%s-%s' % (cp, v))
    if os.path.isdir(src):
        shutil.copytree(src, os.path.join(out, '%s-%s' % (cp, v)))
        n += 1
print('records from the board: %d (%s)' % (n, j))
PY
mkdir -p "$G/mnt/image"
mount --bind "$R" "$G/mnt/image"
trap 'umount "$G/mnt/image"' EXIT
NICKEL_GENTOO_ROOT=$G nickel-gentoo-run 'ROOT=/mnt/image emaint --fix moveinst' | tail -2
python3 "$O/nickel-unify-records.py" "$R" "$G/var/db/pkg" "$O/gentoo/swap.txt" "$W/swap" "$BR" |
	grep -E "^records|swap set:|newer than|out for|other than Gentoo|development files|beside Gentoo|RUNPATH|slot operators|subslots|abi_x86_64|32-bit libraries"
{ cat "$W/swap"; grep -vE '^\s*(#|$)' "$O/gentoo/install.txt"; } > "$G/root/uf-dry-swap.txt"
{ grep -vE '^\s*(#|$)' "$O/gentoo/held-back.txt"; grep -v '^$' "$W/swap.newer" || true; } > "$G/root/uf-dry-exclude.txt"
NICKEL_GENTOO_ROOT=$G nickel-gentoo-run '
PKGDIR=/var/cache/uf-pkgs FEATURES="-news" \
emerge --root=/mnt/image --usepkgonly --getbinpkg=n --oneshot --pretend --verbose \
	--rebuild-if-new-slot=n --ignore-built-slot-operator-deps=y --with-bdeps=n \
	--binpkg-respect-use=n --binpkg-changed-deps=n \
	--exclude "$(tr "\n" " " < /root/uf-dry-exclude.txt)" \
	$(cat /root/uf-dry-swap.txt) > /root/uf-dry.log 2>&1
echo "emerge --pretend: $?"
sed -E "s/\x1b\[[0-9;]*m//g" /root/uf-dry.log | grep -E "^\[binary" | awk "{print \$2}" | sort | uniq -c
sed -E "s/\x1b\[[0-9;]*m//g" /root/uf-dry.log | grep -E "^!!!|required by|masked by" | head -30'
