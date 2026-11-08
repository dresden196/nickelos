#!/bin/bash
set -u
export NICKEL_GENTOO_ROOT=/srv/nickel/unified/root
echo "== candidates $(date +%T)"
nickel-gentoo-run "emerge --noreplace --keep-going --quiet-build \$(cat /root/gentoo-candidates.txt) > /root/candidates.log 2>&1; echo rc=\$?"
grep -aE "^>>> (Emerging|Installing)|^!!!|^ \* ERROR|Total:|failed" $NICKEL_GENTOO_ROOT/root/candidates.log | tail -60
echo "== done $(date +%T)"
