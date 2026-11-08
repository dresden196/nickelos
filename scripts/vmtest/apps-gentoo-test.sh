#!/bin/bash
# Runs on nickel-build, with tester signed in in the test VM and Gentoo's
# setup done. Popular desktop apps from Gentoo, installed as a user installs
# them (step 4 of NickelOS's plan):
#   1. each app emerged (binaries where Gentoo's binhost or ours has them),
#      with what came as binaries and what was built, and how long it took;
#      keywords a user would accept are accepted (OBS and Slack are ~amd64),
#      and the USE changes Portage asks for taken into package.use as a user
#      takes them (OBS wants ffmpeg with x264 and opus, and x264 wants gpl:
#      apps-gentoo-take.awk). Discord's and
#      Slack's licenses are accepted: test machines only (Dresden,
#      2026-10-20); their in-app terms are never clicked through
#   2. each app started as tester: still running after 20 s, its window's
#      process tree, no crash reports
# NICKEL_VMTEST_DIR / NICKEL_VMTEST_SSHPORT select another VM. APPS (env)
# overrides the list: "cat/pkg:command[:process] ..." (+ between a command's
# words; the process to look for when it isn't the command's, LibreOffice's
# soffice.bin).
set -u
APPS=${APPS:-"www-client/firefox-bin:firefox-bin media-video/mpv:mpv+--idle=yes+--force-window=yes media-video/vlc:vlc app-office/libreoffice-bin:libreoffice-bin+--writer:soffice.bin media-video/obs-studio:obs net-im/discord:discord net-im/slack:slack"}
vm() { nickel-vmssh "$@" </dev/null; }
root() { vm ". /etc/profile >/dev/null 2>&1; echo 0 > /proc/self/oom_score_adj; cd /root; $1"; }

echo "== 1. install"
root 'mkdir -p /etc/portage/package.accept_keywords /etc/portage/package.license /etc/portage/package.use
printf "media-video/obs-studio ~amd64\nnet-im/slack ~amd64\n" > /etc/portage/package.accept_keywords/zzz-apps
printf "net-im/discord all-rights-reserved\nnet-im/slack all-rights-reserved\n" > /etc/portage/package.license/zzz-apps
: > /etc/portage/package.use/zzz-apps'
nickel-vmssh 'cat > /tmp/ag-take.awk' < "$(dirname "$0")/apps-gentoo-take.awk"
for a in $APPS; do
	pkg=${a%%:*}
	# The changes Portage prints, taken as a user takes them, a few rounds,
	# until it asks for nothing new.
	root "for round in 1 2 3 4 5 6; do
emerge -p --getbinpkg --usepkg --autounmask=y $pkg > /tmp/ag-plan.log 2>&1 && break
sed -E 's/\x1b\[[0-9;]*m//g' /tmp/ag-plan.log | awk -f /tmp/ag-take.awk > /tmp/ag-take
cat /etc/portage/package.use/zzz-apps /etc/portage/package.accept_keywords/zzz-apps | sed 's/^/x /' > /tmp/ag-have
sed 's/^[a-z]* /x /' /tmp/ag-take | grep -qvxFf /tmp/ag-have || break
sed -n 's/^use //p' /tmp/ag-take >> /etc/portage/package.use/zzz-apps
sed -n 's/^kw //p' /tmp/ag-take >> /etc/portage/package.accept_keywords/zzz-apps
done"
	root "start=\$(date +%s); emerge --getbinpkg --usepkg --quiet-build $pkg > /tmp/ag-\$(basename $pkg).log 2>&1; rc=\$?
L=/tmp/ag-\$(basename $pkg).log
b=\$(sed -E 's/\x1b\[[0-9;]*m//g' \$L | grep -cE '^>>> Emerging binary')
s=\$(sed -E 's/\x1b\[[0-9;]*m//g' \$L | grep -E '^>>> Emerging \(' | sed -E 's/.*\) //; s/::.*//' | tr '\n' ' ')
echo \"$pkg: rc=\$rc in \$(( (\$(date +%s) - start) / 60 )) min, \$b binaries, built: \${s:-none}\"
[ \$rc = 0 ] || sed -E 's/\x1b\[[0-9;]*m//g' \$L | grep -E '^!!!|masked by|required by|unsatisfied|^ \\* (ERROR|Detected file collision)' | grep -v 'binary packages have been ignored' | head -6"
done

echo "== 2. start"
vm 'ls /var/spool/crash/ /home/chronos/u-*/crash/ 2>/dev/null | grep -c "meta$"' > /tmp/ag-crash-before
for a in $APPS; do
	pkg=${a%%:*}
	cmd=${a#*:}
	proc=
	[[ $cmd == *:* ]] && { proc=${cmd##*:}; cmd=${cmd%:*}; }
	cmd=${cmd//+/ }
	bin=${cmd%% *}
	proc=${proc:-$bin}
	vm "nickel-session-exec sh -c '$cmd > /tmp/ag-run-$bin.log 2>&1 &'" >/dev/null 2>&1
	sleep 20
	vm "p=\$(pgrep -u tester -f '$proc' | head -1); if [ -n \"\$p\" ]; then echo \"$pkg: running ($proc, \$(pgrep -u tester -f '$proc' | wc -l) processes)\"; else echo \"$pkg: NOT running\"; tail -3 /tmp/ag-run-$bin.log | cut -c1-160; fi
pkill -u tester -f '$proc' 2>/dev/null; true"
	sleep 3
done
after=$(vm 'ls /var/spool/crash/ /home/chronos/u-*/crash/ 2>/dev/null | grep -c "meta$"')
echo "crash reports: $(cat /tmp/ag-crash-before) before, $after after"
vm 'ls -t /var/spool/crash/ /home/chronos/u-*/crash/ 2>/dev/null | grep "meta$" | head -5'
