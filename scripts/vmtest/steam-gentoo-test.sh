#!/bin/bash
# Runs on nickel-build, with tester signed in in the test VM
# (local-account-test.sh) and Gentoo's setup done (gentoo-test.sh's step 1).
# Steam from Gentoo, the way a user installs it on NickelOS (vision
# decision 1: abi_x86_32 on Mesa and the rest, in your layer):
#   1. git from the binhost (the OS has none; the Steam overlay syncs with
#      it), then the Steam overlay as a repository, synced
#   2. steam-launcher's keyword accepted (~amd64, as on any Gentoo) and the
#      USE changes Portage asks for written (abi_x86_32), then installed:
#      how many packages came as binaries and how many were built
#   3. the 32-bit graphics stack in place: 32-bit Mesa, libdrm, Vulkan
#      loader; a 32-bit GL/Vulkan program would find its libraries
#   4. Steam starts as tester (its bootstrapper: the window downloading its
#      runtime), screenshot $V/flow/steam-gentoo-*.png
#   5. the OS untouched: Chrome and its GPU process still on the OS's
#      64-bit Mesa, no crash reports
# NICKEL_VMTEST_DIR / NICKEL_VMTEST_SSHPORT select another VM.
set -u
V=${NICKEL_VMTEST_DIR:-/srv/nickel/vmtest}
vm() { nickel-vmssh "$@" </dev/null; }
root() { vm ". /etc/profile >/dev/null 2>&1; echo 0 > /proc/self/oom_score_adj; cd /root; $1"; }
shot() { nickel-vmsettle "$V/flow/steam-gentoo-$1.png" 8 >/dev/null 2>&1; echo "$V/flow/steam-gentoo-$1.png"; }
mkdir -p "$V/flow"

echo "== 1. git, the Steam overlay"
root 'emerge --quiet --getbinpkg --usepkg dev-vcs/git > /tmp/sg-git.log 2>&1; echo "git: $? $(type -P git)"
mkdir -p /etc/portage/repos.conf
printf "[steam-overlay]\nlocation = /var/db/repos/steam-overlay\nsync-type = git\nsync-uri = https://github.com/anyc/steam-overlay.git\n" > /etc/portage/repos.conf/steam-overlay.conf
emaint sync -r steam-overlay > /tmp/sg-sync.log 2>&1; echo "synced: $? $(ls /var/db/repos/steam-overlay/games-util 2>/dev/null | grep -c steam) steam packages"'

echo "== 2. steam-launcher"
# Valve's license in package.license: test machines only (Dresden,
# 2026-10-19); Steam's own agreement in the client is never accepted here.
# Portage writes the changes it asks for (abi_x86_32) into the last file of
# each directory: a file of the user's, zzz-steam, merged with etc-update
# as a user does.
root 'mkdir -p /etc/portage/package.accept_keywords /etc/portage/package.license /etc/portage/package.use
echo "games-util/steam-launcher ~amd64" > /etc/portage/package.accept_keywords/zzz-steam
echo "games-util/steam-launcher ValveSteamLicense" > /etc/portage/package.license/zzz-steam
: > /etc/portage/package.use/zzz-steam
# A chain this deep takes Portage a few rounds (it stops at the first
# changes it finds), as for any Gentoo user: write, merge, ask again.
# Portage writes them itself only when the plan then works; in a chain this
# deep it ends on the next change it has not found yet, so the changes it
# printed are taken as a user takes them, into the zzz-steam files.
for round in 1 2 3 4 5 6 7 8 9 10 11 12; do
	emerge -p --getbinpkg --usepkg --autounmask=y --autounmask-write=y --autounmask-backtrack=y games-util/steam-launcher > /tmp/sg-plan.log 2>&1 && break
	etc-update --automode -5 >/dev/null 2>&1
	sed -E "s/\x1b\[[0-9;]*m//g" /tmp/sg-plan.log | awk "/USE changes are necessary/{u=1;k=0;next} /keyword changes are necessary/{k=1;u=0;next} /^The following|^!!!|^ \\*|^NOTE|^emerge:/{u=0;k=0} u&&/^>?=/{print > \"/tmp/sg-use.add\"} k&&/^>?=/{print > \"/tmp/sg-kw.add\"}"
	[ -s /tmp/sg-use.add ] || [ -s /tmp/sg-kw.add ] || break
	cat /tmp/sg-use.add >> /etc/portage/package.use/zzz-steam 2>/dev/null
	cat /tmp/sg-kw.add >> /etc/portage/package.accept_keywords/zzz-steam 2>/dev/null
	rm -f /tmp/sg-use.add /tmp/sg-kw.add
done
echo "plan resolved after $round round(s): $(grep -c "^\[" /tmp/sg-plan.log) packages"
echo "abi_x86_32 changes written: $(grep -c abi_x86_32 /etc/portage/package.use/zzz-steam); keywords: $(grep -vc "^#" /etc/portage/package.accept_keywords/zzz-steam)"
start=$(date +%s)
emerge --getbinpkg --usepkg games-util/steam-launcher > /tmp/sg-emerge.log 2>&1; rc=$?
echo "emerge: $rc in $(( ($(date +%s) - start) / 60 )) min"
sed -E "s/\x1b\[[0-9;]*m//g" /tmp/sg-emerge.log | grep -cE "^>>> Emerging binary" | sed "s/^/binary packages: /"
sed -E "s/\x1b\[[0-9;]*m//g" /tmp/sg-emerge.log | grep -E "^>>> Emerging \(" | sed -E "s/.*\) //; s/::.*//" | tr "\n" " " | sed "s/^/built from source: /"; echo
sed -E "s/\x1b\[[0-9;]*m//g" /tmp/sg-emerge.log | grep -E "^!!!|masked by|required by" | head -8
true'

echo "== 3. 32-bit graphics"
root 'for f in /usr/lib/libGLX_mesa.so.0 /usr/lib/libEGL_mesa.so.0 /usr/lib/libdrm.so.2 /usr/lib/libvulkan.so.1 /usr/lib/libgbm.so.1 /usr/lib/dri/radeonsi_dri.so; do printf "%s: %s\n" "$f" "$( [ -e $f ] && file -L $f | grep -o "ELF 32-bit" || echo missing)"; done
ls /usr/share/vulkan/icd.d/ | tr "\n" " "; echo'

echo "== 4. Steam starts"
vm 'nickel-session-exec sh -c "steam > /tmp/sg-steam.log 2>&1 &"' >/dev/null 2>&1
sleep 60
shot 1-start
vm 'pgrep -a -u tester -f "steam|ubuntu12_32" | head -4 | cut -c1-120; tail -5 /tmp/sg-steam.log 2>/dev/null | cut -c1-160'

echo "== 5. the OS untouched"
vm 'g=$(pgrep -f -- "--type=gpu-process" | head -1); grep -oE "/[^ ]*(libEGL_mesa|libgallium|minigbm)[^ ]*" /proc/$g/maps | sort -u
ls /var/spool/crash/ /home/chronos/u-*/crash/ 2>/dev/null | grep -E "meta$" | sort -u | head'
