#!/bin/bash
# Runs on nickel-build, as root: an OS update on NickelOS and what
# it does to your layer (docs/vision.md, "Done means": "an A to B update
# that keeps your Gentoo packages, and an update that brings newer core
# libraries under apps built against the old ones"). update-test.sh checks
# the road itself (Settings, channels, the payload, the restart); this one
# checks what you installed, through a real update from N to N+1:
#   1. publish IMG_NEXT (N+1, a test image) on a local update server's
#      stable channel, signed with the test key
#   2. a VM on IMG_N (N): setup, an account, Gentoo's setup at the first
#      startup (nickel-gentoo)
#   3. your layer, one of each kind:
#      a. a Gentoo binary package (app-misc/jq)
#      b. a package built from source (app-text/tree)
#      c. a Gentoo package linked against an OS library (net-misc/socat,
#         OpenSSL's)
#      d. a program you built yourself against OS libraries (GLib, libxml2)
#         in /usr/local
#      e. an OS package rebuilt with your flags (app-arch/gzip[pic])
#      f. a newer version of an OS package, taken with one unmask line (the
#         first held one Gentoo has a newer stable version of)
#      g. a setting of yours (/etc/portage/package.use/zz-update-test)
#      h. an OS package rebuilt from source with the OS's own flags
#         (app-arch/bzip2)
#   4. the update: update_engine to the local server, check, download,
#      apply, restart: the VM comes back on N+1
#   5. after it: nickel-gentoo's records sync (what it cleared, kept and
#      left waiting: /var/log/nickel-gentoo-mount.log), the rebuild of what
#      waits at that startup, unattended (nickel-gentoo firstboot, r30;
#      /var/log/nickel-gentoo-setup.log), each of a-h, `nickel-gentoo
#      status`, a world update resolving and the world policy
#      (src/world-policy-check.py)
# Expected: a, b, c, d and g as they were; e cleared (the OS's is as new)
# and waiting for a rebuild with your flag, then rebuilt at that startup;
# f handed back if N+1's OS has that version or newer, kept otherwise; h
# cleared and not waiting (your flags are the OS's): the OS's build.
# e, f and h say whose build is installed: "the OS's" when it is the
# image's own (its record's build time), "yours" otherwise.
#   IMG_N=<image dir N> IMG_NEXT=<image dir N+1> unified-update-test.sh
# The VM (NICKEL_VMTEST_DIR /srv/nickel/vmtest-v, ssh port 9229) keeps
# running afterwards.
set -u
export NICKEL_VMTEST_DIR=${NICKEL_VMTEST_DIR:-/srv/nickel/vmtest-v}
export NICKEL_VMTEST_SSHPORT=${NICKEL_VMTEST_SSHPORT:-9229}
D=$NICKEL_VMTEST_DIR
V=$D
here=$(dirname "$(readlink -f "$0")")
update=$here/../update
DIR_N=${IMG_N:?IMG_N: the image directory to start from}
DIR_NEXT=${IMG_NEXT:?IMG_NEXT: the image directory to update to}
# release-check.sh names the images themselves.
[ -f "$DIR_N" ] && DIR_N=$(dirname "$DIR_N")
[ -f "$DIR_NEXT" ] && DIR_NEXT=$(dirname "$DIR_NEXT")
ROOT=$D/update-root
PORT=18091
NAME="Tester Nickel"
PASS=nickel-test-pw
export NICKEL_TEST_EXPERIENCE=minimal
vm() { nickel-vmssh "$@" </dev/null; }
root() { vm ". /etc/profile >/dev/null 2>&1; echo 0 > /proc/self/oom_score_adj; cd /root; $1"; }
state() { vm "dbus-send --system --print-reply --dest=org.chromium.SessionManager /org/chromium/SessionManager org.chromium.SessionManagerInterface.RetrieveSessionState 2>/dev/null" | grep -oE 'string "[a-z]+"' | tail -1; }
wait_log() { for i in $(seq 1 "${2:-40}"); do vm "grep -qs '$1' /var/log/chrome/chrome" && return 0; sleep 2; done; echo "TIMEOUT waiting for: $1"; return 1; }
wait_state() { for i in $(seq 1 "${2:-30}"); do [ "$(state)" = "string \"$1\"" ] && return 0; sleep 3; done; echo "TIMEOUT waiting for session state $1 (now $(state))"; return 1; }
wait_ssh() { sleep 15; for i in $(seq 1 90); do timeout 15 nickel-vmssh true 2>/dev/null && return 0; sleep 4; done; echo "SSH NEVER CAME UP"; exit 1; }
image_version() {
  losetup -f --show -P -r "$1" | { read -r l; m=$(mktemp -d); mount -o ro "${l}p3" "$m"
    sed -n 's/^CHROMEOS_RELEASE_VERSION=//p' "$m/etc/lsb-release"; umount "$m"; rmdir "$m"; losetup -d "$l"; }
}
# Your layer's state, the same lines before and after.
LAYER='echo "a. jq: $(jq --version 2>&1)"
echo "b. tree: $(tree --version 2>&1 | cut -d" " -f1-2)"
echo "c. socat: $(socat -V 2>&1 | sed -n "s/^socat version \([0-9.]*\).*/\1/p") $(ldd /usr/bin/socat | grep -c "libssl\|libcrypto") OpenSSL libs, $(ldd /usr/bin/socat | grep -c "not found") missing"
echo "d. own program: $(/usr/local/bin/update-test-libs 2>&1)"
echo "e. gzip: $(whose app-arch/gzip) USE=$(cat /var/db/pkg/app-arch/gzip-*/USE | tr " " "\n" | grep -cx pic) pic"
echo "f. ${F:-none newer in Gentoo}${F:+: $(whose $F)}"
echo "g. your setting: $(cat /etc/portage/package.use/zz-update-test 2>&1)"
echo "h. bzip2: $(whose app-arch/bzip2)"
echo "world: $(grep -cE "^(app-misc/jq|app-text/tree|net-misc/socat)$" /var/lib/portage/world) of 3"'
# A package's installed version, and whose build it is: the image's own
# (its record's build time in the image's records) or yours.
WHOSE='whose() { o=; for d in /var/db/pkg/$1-[0-9]*; do [ -d "$d" ] || continue
  t=$(tar --zstd -xOf /usr/share/nickel-gentoo/pkgdb.tar.zst --wildcards "*/${d#/var/db/pkg/}/BUILD_TIME" 2>/dev/null)
  [ "$t" = "$(cat $d/BUILD_TIME)" ] && w="the OS'"'"'s" || w=yours
  o="$o${o:+, }${d##*/} ($w)"; done; printf "%s" "${o:-none}"; }'
layer() { root "F='$F'; $WHOSE; $LAYER"; }

mkdir -p "$D/flow"
IMG_N_BIN=$DIR_N/chromiumos_qemu_image.bin
IMG_NEXT_BIN=$DIR_NEXT/chromiumos_test_image.bin
# N's version from its test image, or its VM image (the same system
# partition) when only that was kept, as for --prev on another build host.
if [ -f "$DIR_N/chromiumos_test_image.bin" ]; then
  v_n=$(image_version "$DIR_N/chromiumos_test_image.bin")
else
  v_n=$(image_version "$IMG_N_BIN")
fi
v_next=$(image_version "$IMG_NEXT_BIN")
echo "N: $v_n (${DIR_N##*/}), N+1: $v_next (${DIR_NEXT##*/})"
[ "$(printf '%s\n%s\n' "$v_n" "$v_next" | sort -V | tail -1)" = "$v_next" ] && [ "$v_n" != "$v_next" ] ||
  { echo "N+1 must have a newer version than N"; exit 1; }

echo "== 1. N+1 on a local update server's stable channel"
if [ -f "$ROOT/channels/stable/nickelos-$v_next-stable-full.json" ]; then
  echo "$v_next already published"
else
  "$update/nickel-update-publish" "$IMG_NEXT_BIN" stable "$ROOT" || exit 1
fi
systemctl stop nickel-uu-server 2>/dev/null; systemctl reset-failed nickel-uu-server 2>/dev/null
: > "$D/update-server.log"
systemd-run --unit nickel-uu-server --collect \
  /usr/bin/python3 "$update/nickel-update-server" --root "$ROOT" --bind 127.0.0.1 \
  --port "$PORT" --payloads-url "http://10.0.2.2:$PORT/payloads" --log "$D/update-server.log" >/dev/null
# It reads the payloads before it listens: a minute at most.
for _ in $(seq 30); do
  curl -sf "http://127.0.0.1:$PORT/healthz" >/dev/null && break
  sleep 2
done
curl -sf "http://127.0.0.1:$PORT/healthz" >/dev/null || { echo "the update server didn't start"; exit 1; }

echo "== 2. VM on N, setup and Gentoo's"
unit=nickel-$(basename "$D")
systemctl stop "$unit" 2>/dev/null; systemctl reset-failed "$unit" 2>/dev/null
systemd-run --unit "$unit" --collect -E NICKEL_VMTEST_DIR="$D" \
  -E NICKEL_VMTEST_SSHPORT="$NICKEL_VMTEST_SSHPORT" \
  /usr/local/bin/nickel-vmtest "$IMG_N_BIN" >/dev/null
sleep 5
systemctl is-active -q "$unit" || { echo "the VM did not start"; journalctl -u "$unit" | tail -5; exit 1; }
wait_ssh
. "$here/setup-account.sh"
setup_account "$NAME" "$PASS" uu || exit 1
echo "version: $(vm 'sed -n s/^CHROMEOS_RELEASE_VERSION=//p /etc/lsb-release')"
root 'nickel-gentoo wait 2700; echo "Gentoo setup: $?"'

echo "== 3. your layer on N"
# f: the first OS package held at the OS's version that Gentoo has a newer
# stable version of (src/world-policy-check.py's choice; not gzip, e's),
# taken with an exact version in package.unmask (autounmask). Like that
# check, it skips a version whose licence the device doesn't accept (unrar's
# unRAR, 17.9): that is the user's choice, not the OS's mask.
F=$(root 'python3 -c "
import portage
from portage.versions import catpkgsplit, pkgcmp
db = portage.db[portage.root][\"porttree\"].dbapi
K = [\"LICENSE\", \"SLOT\", \"repository\"]
for l in open(\"/etc/portage/profile/package.mask/00-nickel-os-versions\"):
    if not l.startswith(\">\"): continue
    cat, pn, ver, rev = catpkgsplit(l[1:].split(\":\")[0].strip())
    if pn == \"gzip\": continue
    new = [c for c in db.xmatch(\"match-all\", cat + \"/\" + pn) if not c.endswith(\"9999\")
           and pkgcmp(catpkgsplit(c)[1:], (pn, ver, rev)) > 0
           and \"amd64\" in db.aux_get(c, [\"KEYWORDS\"])[0].split()
           and not portage.settings._getMissingLicenses(c, dict(zip(K, db.aux_get(c, K)), USE=\"\"))]
    if new: print(new[-1]); break
"')
F_CPV=$F
F=$(echo "$F_CPV" | sed -E 's/-[0-9][^/]*$//')
echo "f: ${F_CPV:-none newer in Gentoo}"
root "echo 'app-arch/gzip pic' > /etc/portage/package.use/zz-update-test
emerge --quiet app-misc/jq net-misc/socat > /tmp/uu-bin.log 2>&1; echo \"a, c: \$?\"
emerge --quiet --usepkg=n --getbinpkg=n app-text/tree > /tmp/uu-src.log 2>&1; echo \"b: \$?\"
emerge --quiet --oneshot --usepkg=n --getbinpkg=n app-arch/gzip > /tmp/uu-gzip.log 2>&1; echo \"e: \$?\"
[ -n '$F_CPV' ] && { emerge --quiet --oneshot =$F_CPV > /tmp/uu-f.log 2>&1; echo \"f: \$? (\$(cat /etc/portage/package.unmask* 2>/dev/null | grep -v '^#' | tr '\n' ' '))\"; }
emerge --quiet --oneshot --usepkg=n --getbinpkg=n app-arch/bzip2 > /tmp/uu-h.log 2>&1; echo \"h: \$?\"
mkdir -p /usr/local/src && cd /usr/local/src && cat > update-test-libs.c <<'EOF'
#include <glib.h>
#include <libxml/parser.h>
#include <stdio.h>
int main(void) {
  GHashTable *t = g_hash_table_new(g_str_hash, g_str_equal);
  g_hash_table_insert(t, \"a\", \"b\");
  printf(\"GLib %u.%u (built against %d.%d), libxml2 %s, %s\\n\", glib_major_version,
         glib_minor_version, GLIB_MAJOR_VERSION, GLIB_MINOR_VERSION, xmlParserVersion,
         g_hash_table_size(t) == 1 ? \"works\" : \"BROKEN\");
  return 0;
}
EOF
gcc -o /usr/local/bin/update-test-libs update-test-libs.c \$(pkg-config --cflags --libs glib-2.0 libxml-2.0) && echo 'd: built'"
layer | tee "$D/uu-before.txt"
root 'cat /var/lib/nickel-gentoo/build 2>/dev/null | head -3'

echo "== 4. the update"
vm "mkdir -p /mnt/stateful_partition/etc
echo CHROMEOS_AUSERVER=http://10.0.2.2:$PORT/update > /mnt/stateful_partition/etc/lsb-release
restart update-engine >/dev/null; sleep 3
update_engine_client --check_for_update >/dev/null 2>&1"
op=
for i in $(seq 1 120); do
  op=$(vm 'update_engine_client --status 2>/dev/null' | sed -n 's/^CURRENT_OP=//p')
  [ "$op" = UPDATE_STATUS_UPDATED_NEED_REBOOT ] && break
  case "$op" in UPDATE_STATUS_IDLE) [ "$i" -gt 6 ] && break ;; esac
  sleep 15
done
echo "update_engine: $op"
[ "$op" = UPDATE_STATUS_UPDATED_NEED_REBOOT ] || { tail -5 "$D/update-server.log"; exit 1; }
vm 'reboot' >/dev/null 2>&1
sleep 30
wait_ssh
echo "version now: $(vm 'sed -n s/^CHROMEOS_RELEASE_VERSION=//p /etc/lsb-release')"
sign_in_again "$PASS"

echo "== 5. your layer on N+1"
# The records sync's report (nickel-gentoo-mount, at early boot), then the
# rebuild of what it left waiting, at this startup with nobody asking
# (nickel-gentoo firstboot, at idle priority once Gentoo's mirrors answer):
# the setup job ends when it is done.
root 'grep -ahE "records for this OS build|yours cleared|yours stays|waiting to be rebuilt" /var/log/nickel-gentoo-mount.log 2>/dev/null | tail -6 | cut -c1-300'
for i in $(seq 1 180); do
  root 'initctl status nickel-gentoo-setup' | grep -q "stop/waiting" && break
  sleep 10
done
root 'echo "the setup job: $(initctl status nickel-gentoo-setup) after the startup"
grep -ahE "rebuilding what|rebuilt:|rebuild failed" /var/log/nickel-gentoo-setup.log | tail -3 | cut -c1-300'
still=$(root 'cat /mnt/stateful_partition/nickel-gentoo/.rebuild 2>/dev/null | tr "\n" " "')
echo "still waiting: $still"
layer | tee "$D/uu-after.txt"
echo "-- changed:"
diff "$D/uu-before.txt" "$D/uu-after.txt" | sed -n 's/^[<>]/ &/p'
# What must hold whatever N and N+1 are (f depends on N+1's version of it).
for k in a b c g world; do
  [ "$(grep "^$k[.:]" "$D/uu-before.txt")" = "$(grep "^$k[.:]" "$D/uu-after.txt")" ] || echo "FAIL: $k changed"
done
grep -q "^d\..*works" "$D/uu-after.txt" || echo "FAIL: d, your own program, doesn't run"
grep -q "^e\..*(yours) USE=1 pic" "$D/uu-after.txt" || echo "FAIL: e is not rebuilt with your flag"
grep -q "^h\..*(the OS's)" "$D/uu-after.txt" || echo "FAIL: h is not the OS's build"
[ -z "${still// /}" ] || echo "FAIL: still waiting after the startup: $still"
root 'echo "status: $(nickel-gentoo status 2>&1 | tr "\n" " " | cut -c1-300)"
nickel-gentoo rebuild 2>&1 | tail -1'
root 'emerge -puDN --quiet @world > /tmp/uu-world.log 2>&1; echo "emerge -uDN @world resolves: $?"'
"$here"/nickel-vmpush /usr/local/bin/world-policy-check < "$here"/src/world-policy-check.py
root '/usr/local/bin/world-policy-check'
echo "== done"
