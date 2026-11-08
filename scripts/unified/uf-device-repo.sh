#!/bin/bash
# Copies the packages in gentoo/device-repo.txt from this overlay into
# NickelOS's Gentoo repository (nickel-gentoo's files/repo/nickel). Run it
# after changing one of them, then raise nickel-gentoo's revision; the
# build stops while the copies differ (nickel-unify runs this with --check).
# One difference is made on the way: the board's Portage (ChromiumOS's
# 2.3.75) knows EAPI 7 at most, and Gentoo's Python eclasses want EAPI 8, so
# the overlay's ebuilds say 7 and the repository's say 8 (an EAPI 8 ebuild
# in the overlay masked waydroid on the board, and with it all of NickelOS's
# packages: builds 23b and 23c, 2026-10-20).
#   uf-device-repo.sh [--check]
set -euo pipefail
O=$(cd "$(dirname "$0")/../.." && pwd)
REPO=$O/chromeos-base/nickel-gentoo/files/repo/nickel
check=${1:-}
to_repo() { # overlay package dir -> its repository form in $2
	cp -r "$1" "$2"
	for e in "$2"/*.ebuild; do
		sed -i 's/^EAPI=7$/EAPI=8/' "$e"
	done
}
rc=0
for cp in $(grep -vE '^\s*(#|$)' "$O/scripts/unified/gentoo/device-repo.txt"); do
	if [ "$check" = --check ]; then
		t=$(mktemp -d)
		to_repo "$O/$cp" "$t/p"
		diff -rq "$t/p" "$REPO/$cp" >/dev/null || { echo "$cp differs from NickelOS's Gentoo repository's copy: run scripts/unified/uf-device-repo.sh" >&2; rc=1; }
		rm -rf "$t"
	else
		rm -rf "${REPO:?}/$cp"
		mkdir -p "$REPO/${cp%/*}"
		to_repo "$O/$cp" "$REPO/$cp"
		echo "$cp"
	fi
done
if [ "$check" != --check ]; then
	{ echo sys-apps; grep -vE '^\s*(#|$)' "$O/scripts/unified/gentoo/device-repo.txt" | cut -d/ -f1; } |
		sort -u > "$REPO/profiles/categories"
fi
exit $rc
