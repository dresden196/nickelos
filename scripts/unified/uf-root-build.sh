#!/bin/bash
# NickelOS, step 1: the Gentoo root at today's Gentoo, with every
# package the image has that Gentoo's tree has too (gentoo-candidates.txt).
# Proof only: ACCEPT_LICENSE="*" (release licensing is a later step).
set -u
export NICKEL_GENTOO_ROOT=/srv/nickel/unified/root
run() { echo "== $1 ($(date +%T))"; nickel-gentoo-run "$2"; echo "-- rc=$? ($(date +%T))"; }
grep -q unified-proof $NICKEL_GENTOO_ROOT/etc/portage/make.conf || cat >> $NICKEL_GENTOO_ROOT/etc/portage/make.conf <<EOC
# unified-proof
ACCEPT_LICENSE="*"
EOC
run sync "emerge --sync 2>&1 | tail -5"
run world "emerge -uDN --keep-going --quiet-build @world 2>&1 | grep -aE \"^>>> (Emerging|Installing)|^!!!|^ \\* (ERROR|Messages for package)|Total:|failed\" | tail -60"
run candidates "emerge --noreplace --keep-going --quiet-build --autounmask=y --autounmask-write=y --autounmask-continue=y \$(cat /root/gentoo-candidates.txt) 2>&1 | tee /root/candidates.log | grep -aE \"^>>> (Emerging|Installing)|^!!!|^ \\* (ERROR|Messages for package)|Total:|failed|could not be|masked\" | tail -120"
echo "== done $(date +%T)"
