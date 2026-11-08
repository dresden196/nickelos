#!/bin/bash
# NickelOS proof: runs in a test VM as root (nickel-vmssh "bash -s" <
# uf-vm-audit.sh), signed in. What the release check doesn't look at:
#   1. glibc: the version, and what ChromeOS's own services and Chrome run on
#   2. sandbox kills: seccomp's audit records (minijail, Chrome) since boot
#   3. crashes: crash reports, segfaults, Upstart jobs killed by a signal
#   4. Upstart jobs that aren't running (to compare with stock 3.36's)
#   5. minijail's refusals: a sandbox minijail won't set up fails before
#      its program logs a word (merged /usr's /sbin once kept dns-proxy, so
#      all DNS, down)
#   6. Upstart jobs "running" with no process (dns-proxy looked like that)
echo 0 > /proc/self/oom_score_adj
echo "== 1. glibc"
/lib64/libc.so.6 2>/dev/null | head -1
for n in chrome session_manager shill powerd cras cryptohomed dbus-daemon udevd spaced update_engine; do
	p=$(pgrep -x "$n" | head -1)
	[ -n "$p" ] || { echo "$n: not running"; continue; }
	echo "$n: $(grep -oE '/[^ ]*/libc\.so\.6' /proc/$p/maps | sort -u | tr '\n' ' ')$(readlink -f /proc/$p/root 2>/dev/null | grep -v '^/$' | sed 's/^/(root &) /')"
done
echo "== 2. sandbox kills (seccomp) since boot"
dmesg | grep -c 'type=1326' | sed 's/^/seccomp records: /'
dmesg | grep 'type=1326' | grep -oE 'comm="[^"]+".*syscall=[0-9]+' | sed -E 's/ exe="[^"]*"//; s/ sig=[0-9]+ arch=[0-9a-f]+//' | sort | uniq -c | sort -rn | head -15
grep -h 'blocked syscall\|seccomp' /var/log/messages 2>/dev/null | grep -v 'type=1326' | sed -E 's/^[^ ]+ [^ ]+ //' | cut -c1-160 | sort | uniq -c | sort -rn | head -8
echo "== 3. crashes"
echo "crash reports: $(ls /var/spool/crash/ /home/chronos/crash/ /home/chronos/u-*/crash/ 2>/dev/null | grep -cE '\.meta$')"
ls /var/spool/crash/ /home/chronos/crash/ /home/chronos/u-*/crash/ 2>/dev/null | grep -E '\.meta$' | sed -E 's/\.[0-9]{8}\.[0-9]+\.[0-9]+\.meta$//; s/\.[0-9]+\.meta$//' | sort | uniq -c | head -15
echo "segfaults: $(dmesg | grep -c 'segfault at')"
dmesg | grep 'segfault at' | grep -oE '[a-zA-Z0-9_.-]+\[[0-9]+\]: segfault' | sed 's/\[[0-9]*\]//' | sort | uniq -c | head
grep -h 'killed by\|terminated with status' /var/log/messages 2>/dev/null | grep -i 'init:' | sed -E 's/^.*init: //; s/[0-9]+ (killed|terminated)/PID \1/' | sort | uniq -c | sort -rn | head -12
echo "== 4. Upstart jobs not running (task jobs are normally stopped)"
initctl list 2>/dev/null | grep -v 'start/running' | awk '{print $1}' | sort | tr '\n' ' '
echo
echo "== 5. minijail refusals"
grep -ah 'libminijail' /var/log/messages 2>/dev/null | grep -E 'not a canonical path|not a valid bind|cannot (mount|create mount|resolve)|failed' | sed -E 's/^[^ ]+ [^ ]+ //; s/\[[0-9]+\]//g' | cut -c1-160 | sort | uniq -c | sort -rn | head -15
echo "== 6. Upstart jobs running with no process"
initctl list 2>/dev/null | grep 'start/running$' | awk '{print $1}' | sort | tr '\n' ' '
echo
