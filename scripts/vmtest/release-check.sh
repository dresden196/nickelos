#!/bin/bash
# Runs on a build host (nickel-next, or nickel-build), as root: what a build
# must pass before it goes to Beta (docs/vision.md "Updates"). Every VM test
# in this directory against one build, each test's output kept and compared
# with what the last accepted build answered. A rebase onto a new ChromiumOS
# release changes code under NickelOS that no patch conflict shows; what it
# breaks shows up here as a test that fails or answers differently.
#
#   release-check.sh IMAGE_DIR [--prev PREV_IMAGE_DIR] [--only a,b] [--skip a,b]
#   release-check.sh --accept RUN_DIR
#   release-check.sh --renormalize RUN_DIR   (after normalize() changes)
#   release-check.sh --list
#
# IMAGE_DIR: the build's image directory (chromiumos_qemu_image.bin and
# chromiumos_test_image.bin). --prev: the build PCs run now; the update tests
# then take a VM from it to this one: through Settings (update), and with
# one of each kind of package in your Gentoo layer (unified-update). Tests
# are named without .sh. A run is /srv/nickel/release-check/<version>-<time>/:
#   <test>.log    the test's output
#   <test>.diff   how it differs from the accepted build's (CHANGED)
#   summary.txt   one line per test:
#     SAME     answered as the accepted build did
#     CHANGED  answered differently: read the .diff
#     FAILED   non-zero exit, timeout, a traceback, Chrome crashes: read the log
#     NEW      nothing accepted to compare with yet: read the log
#     SKIPPED  not run (--only/--skip, or a missing --prev)
# A build is ready for Beta when nothing FAILED and every CHANGED and NEW
# has been read and is right. --accept RUN_DIR then makes that run's outputs
# the ones the next build is compared with (/srv/nickel/release-check/accepted);
# tests that FAILED or were SKIPPED in it are left as they were, so a rerun
# of a few tests (--only) can be accepted on its own.
#
# Outputs are compared after taking out what changes on every run (times,
# process IDs, addresses, the version itself). The tests take hours, mostly
# setting up VMs; each has an hour at most.
set -u
here=$(dirname "$(readlink -f "$0")")
BASE=/srv/nickel/release-check
ACCEPTED=$BASE/accepted
LIMIT=${RELEASE_CHECK_LIMIT:-3600}

# In order. "session": in the main test VM (/srv/nickel/vmtest, ssh 9222),
# signed in as tester by local-account-test, which comes first; the order
# matters (hostapps wants the shelf untouched, exo-stress may restart Chrome).
# "own": boots its own VM from the build.
SUITE=(
  "local-account own      local accounts: setup, sign-in, the Linux user"
  "hostapps     session   host apps in Chrome's app registry; Bazaar's pin"
  "apps         session   X11 (GIMP), Wine, audio"
  "logind       session   elogind session, polkit agent"
  "accountsd    session   Settings > Accounts' D-Bus service and its rules"
  "portal       session   the portal backend: settings, file picker"
  "notifications session  Linux apps' notifications"
  "native-apps  session   native GTK 3/4 apps, file picker, focus"
  "exo-protocols session  Wayland protocols Linux apps see"
  "one-home     session   My files is the Linux home"
  "files-bridge session   files between Linux apps and Files"
  "ime          session   input methods in Wayland apps, middle-click paste"
  "xim          session   input methods in X11 apps"
  "screencast   session   screen sharing portal"
  "screenshot   session   screenshot portal"
  "previews     session   picture previews: desktop, Files, nickel-appd"
  "portals      session   Background/autostart, Account, Email, launchers, Wallpaper, PickColor, Clipboard"
  "tour         session   NickelOS's Welcome Tour: its steps, the guide at the end"
  "remote-desktop session remote control portal"
  "steam-games  session   Steam's games in the launcher and on the shelf"
  "phones       session   KDE Connect (Valent)"
  "exo-stress   session   odd Wayland clients must not take Chrome down"
  "gentoo       session   Gentoo at /: setup, an app from the binhost, Gentoo's commands, the worlds apart"
  "cloud-ui     own       cloud storage in Settings and Files"
  "desktop-ui   own       desktop icons"
  "polish-ui    own       the 2026-10-10 clean-up"
  "other-drives-ui own    other systems' drives"
  "installer-ui own       installing to a disk"
  "update       own       update from --prev to this build through Settings"
  "unified-update own     your Gentoo layer through an update from --prev to this build"
)

normalize() {
  sed -E \
    -e 's/[0-9]{4}-[0-9]{2}-[0-9]{2}[T ][0-9:.,]+(Z|[+-][0-9:]{4,5})?/TIME/g' \
    -e 's/\b[0-9]{1,2}:[0-9]{2}:[0-9]{2}(\.[0-9]+)?\b/TIME/g' \
    -e 's/\b[0-9]{8}[.-][0-9]{6}\b/TIME/g' \
    -e 's/\.TIME\.[0-9]+\./.TIME.PID./g' \
    -e 's/continuous build \(git version [0-9a-f]+\), build [0-9]+/continuous build (git version HASH), build N/' \
    -e 's/"before": "(Checking for updates|Updating your device to [a-z]+ channel \(([0-9]+|N)%\))"/"before": "CHECKING OR DOWNLOADING"/' \
    -e 's/R[0-9]+-[0-9]+\.[0-9]+\.[0-9]+(-d[0-9_]+)?(-a1)?/RVERSION/g' \
    -e 's/\b[0-9]{5}\.[0-9]+\.[0-9]+\b/VERSION/g' \
    -e 's/0x[0-9a-fA-F]+/0xADDR/g' \
    -e 's/[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}/UUID/g' \
    -e 's/\[[0-9]+(:[0-9]+)?\]/[PID]/g' \
    -e 's/\b(pid|PID|process|uid)( |=|: )[0-9]+/\1\2N/g' \
    -e 's/\b[0-9]+(\.[0-9]+)? ?(ms|s|sec|seconds|min)\b/N\2/g' \
    -e 's/(after|in|took) [0-9]+(\.[0-9]+)?/\1 N/g' \
    -e 's/\b[0-9]+(\.[0-9]+)? ?([KMGT]i?B|[KMG])\b/SIZE/g' \
    -e 's/(invocation ID|Running as unit)[^ ]* .*/\1 .../' \
    -e 's/\(([0-9]{3,7})\)/(PID)/g' \
    -e 's/(^|[^0-9])[0-9]{2,7}\]:/\1PID]:/g' \
    -e 's/[0-9a-f]{64}/HASH/g; s/[0-9a-f]{40}/HASH/g; s/HASH[0-9a-f]+/HASH/g' \
    -e 's/\/tmp\/tmp[a-z0-9_]{6,}/\/tmp\/TMP/g; s/\/tmp[a-z0-9_]{8}\b/\/tmpX/g; s/(payload_hash|metadata_hash)[a-z0-9_]{8}/\1X/g' \
    -e 's/publish\.[A-Za-z0-9]{6}/publish.X/g; s/\/dev\/loop[0-9]+/\/dev\/loopN/g' \
    -e '/^TIME: INFO: /d; /sign_official_build\.sh: INFO/d; /The command rsautl was deprecated/d' \
    -e "/^\['\/mnt\/host\/source\/\.nickel\/publish/d; /^== nickelos-VERSION-[a-z]+-full \(/d; /^== a test image: its channel stays/d" \
    -e 's/^== published: .* on (beta|stable)$/== published on \1/; s/^VERSION already published$/== published on stable/' \
    -e "s/^(beta|stable): (VERSION ?)+$/\1: VERSIONS/; s/catalog: \{'beta': [0-9]+, 'stable': [0-9]+\}/catalog: {...}/" \
    -e 's/ \([0-9]+%\)/ (N%)/g; s/bytes 0-[0-9]+/bytes 0-N/' \
    -e 's/(Payload hashes: |Signatures for hashes=)\[.*/\1[...]/; s/(GenerateUpdatePayload: )[0-9]+m[0-9.]+N?s/\1TIME/' \
    -e 's/\b(Jan|Feb|Mar|Apr|May|Jun|Jul|Aug|Sep|Oct|Nov|Dec) +[0-9]{1,2} +([0-9]{1,2}:[0-9]{2}|[0-9]{4})\b/DATE/g' \
    -e 's/time=[0-9.]+/time=T/g; s/:1\.[0-9]+/:1.N/g; s/serial=[0-9]+/serial=N/g' \
    -e 's/^[0-9a-f]{4}:(err|fixme|warn|trace):/TID:\1:/' \
    -e 's/session\/1_[0-9]+\//session\/1_N\//g' \
    -e 's/nickel_fake_[0-9]+/nickel_fake_N/g' \
    -e 's/[0-9a-f]{8}_[0-9a-f]{4}_[0-9a-f]{4}_[0-9a-f]{4}_[0-9a-f]{12}/UUID/g' \
    -e 's/localhost [0-9]+ chrome:\/\/theme\/IDR_PROFILE_AVATAR_[0-9]+/localhost N chrome:\/\/theme\/IDR_PROFILE_AVATAR_N/g' \
    -e 's/\/dev\/sd[a-z]/\/dev\/sdX/g' \
    -e 's/--args [0-9]+/--args N/g; s/\.mount_[A-Za-z0-9]+/.mount_X/g' \
    -e 's/^[0-9]{3,7} /PID /; s/\bshell [0-9]+:/shell PID:/; s/seat0 [0-9]+ /seat0 PID /' \
    -e 's/ +$//'
}
failed() {
  # The marks of a test that broke, as these tests print them.
  grep -n -E 'TIMEOUT|NEVER CAME UP|Traceback|FATAL|Segmentation fault|did not start|command not found|^!! |\bFAIL(ED)?\b' "$1" | head -5
}

if [ "${1:-}" = --list ]; then
  for t in "${SUITE[@]}"; do echo "$t"; done
  exit 0
fi
if [ "${1:-}" = --renormalize ]; then
  # A run's outputs again from its logs, after normalize() learned more.
  RUN=$(readlink -f "${2:?the run directory}")
  for log in "$RUN"/*.log; do normalize < "$log" > "${log%.log}.norm"; done
  echo "renormalized: $(ls "$RUN"/*.log | wc -l) tests in $RUN"
  exit 0
fi
if [ "${1:-}" = --accept ]; then
  RUN=$(readlink -f "${2:?the run directory}")
  [ -f "$RUN/summary.txt" ] || { echo "no summary.txt in $RUN" >&2; exit 1; }
  mkdir -p "$ACCEPTED"
  n=0
  while read -r verdict name _; do
    case $verdict in
      SAME|CHANGED|NEW) cp "$RUN/$name.norm" "$ACCEPTED/"; n=$((n + 1));;
      FAILED) echo "not accepted (FAILED): $name";;
    esac
  done < <(grep -E '^(SAME|CHANGED|NEW|FAILED) ' "$RUN/summary.txt")
  echo "$RUN ($n tests)" >> "$ACCEPTED/FROM"
  echo "accepted: $n tests from $RUN"
  exit 0
fi

IMGDIR=$(readlink -f "${1:?the image directory of the build}"); shift
PREV= ONLY= SKIP=
while [ $# -gt 0 ]; do
  case $1 in
    --prev) PREV=$(readlink -f "$2"); shift 2;;
    --only) ONLY=,$2,; shift 2;;
    --skip) SKIP=,$2,; shift 2;;
    *) echo "unknown: $1" >&2; exit 1;;
  esac
done
# The session tests need the VM local-account signs in: a rerun of some of
# them runs it first (17.9's rerun of screenshot and gentoo found no VM).
if [ -n "$ONLY" ] && [[ $ONLY != *,local-account,* ]]; then
  for entry in "${SUITE[@]}"; do
    read -r name kind what <<<"$entry"
    if [ "$kind" = session ] && [[ $ONLY == *",$name,"* ]]; then
      ONLY=,local-account$ONLY
      echo "--only: local-account too, for the session tests' VM"
      break
    fi
  done
fi
QEMU=$IMGDIR/chromiumos_qemu_image.bin
TESTIMG=$IMGDIR/chromiumos_test_image.bin
[ -f "$QEMU" ] && [ -f "$TESTIMG" ] || { echo "$IMGDIR needs chromiumos_qemu_image.bin and chromiumos_test_image.bin" >&2; exit 1; }
version=$(basename "$IMGDIR")
RUN=$BASE/$version-$(date +%Y%m%d-%H%M)
mkdir -p "$RUN"
echo "release check of $version -> $RUN"
[ -f "$ACCEPTED/FROM" ] && echo "compared with: $(tail -1 "$ACCEPTED/FROM")"
{
  echo "build: $IMGDIR"
  echo "previous: ${PREV:-none}"
  echo "compared with: $(tail -1 "$ACCEPTED/FROM" 2>/dev/null || echo nothing yet) (accepted runs: $ACCEPTED/FROM)"
  echo "overlay: $(git -C "$here/../.." log -1 --format='%h %s' 2>/dev/null || cat "$here/../../.synced-from" 2>/dev/null || echo unknown)"
  echo
} > "$RUN/summary.txt"

for entry in "${SUITE[@]}"; do
  read -r name kind what <<<"$entry"
  if { [ -n "$ONLY" ] && [[ $ONLY != *",$name,"* ]]; } || [[ $SKIP == *",$name,"* ]]; then
    printf '%-8s %-16s %s\n' SKIPPED "$name" "$what" >> "$RUN/summary.txt"
    continue
  fi
  if { [ "$name" = update ] || [ "$name" = unified-update ]; } && [ -z "$PREV" ]; then
    printf '%-8s %-16s %s (no --prev)\n' SKIPPED "$name" "$what" >> "$RUN/summary.txt"
    continue
  fi
  log=$RUN/$name.log
  # The update tests make the update payloads themselves (two, 12-25 min
  # each, more when the disk is busy: 17.3 ran out of its hour on them).
  limit=$LIMIT
  case $name in update|unified-update) limit=$((LIMIT * 2)) ;; esac
  echo "== $name: $what ($(date +%H:%M))"
  start=$(date +%s)
  # Each test with the build, and the VM it expects: the session tests talk
  # to the main VM, so no VM settings of another test may leak in.
  env -u NICKEL_VMTEST_DIR -u NICKEL_VMTEST_SSHPORT IMG="$QEMU" \
    ${PREV:+IMG_N="$PREV/chromiumos_qemu_image.bin"} IMG_NEXT="$TESTIMG" UPDATE_TEST_REPUBLISH=1 \
    timeout --foreground "$limit" bash "$here/$name-test.sh" \
      $([ "$name" = installer-ui ] && echo "$TESTIMG") > "$log" 2>&1
  rc=$?
  secs=$(( $(date +%s) - start ))
  normalize < "$log" > "$RUN/$name.norm"
  marks=$(failed "$log")
  if [ "$rc" -ne 0 ] || [ -n "$marks" ]; then
    verdict=FAILED
    note="exit $rc${marks:+; $(echo "$marks" | head -1 | cut -c1-90)}"
    [ "$rc" -eq 124 ] && note="timed out after ${limit}s"
  elif [ ! -f "$ACCEPTED/$name.norm" ]; then
    verdict=NEW note=
  elif diff -u "$ACCEPTED/$name.norm" "$RUN/$name.norm" > "$RUN/$name.diff"; then
    verdict=SAME note=; rm -f "$RUN/$name.diff"
  else
    verdict=CHANGED note="$(grep -c '^[-+][^-+]' "$RUN/$name.diff") lines differ"
  fi
  printf '%-8s %-16s %s (%d min)%s\n' "$verdict" "$name" "$what" $((secs / 60)) "${note:+: $note}" >> "$RUN/summary.txt"
  echo "   $verdict${note:+: $note}"
done

# The test VMs keep running for a look; stop them.
for u in nickel-vmtest nickel-vmtest-b nickel-vmtest-u nickel-update-test-server nickel-vmtest-v nickel-uu-server; do
  systemctl stop "$u" 2>/dev/null
done

echo
cat "$RUN/summary.txt"
n_bad=$(grep -c -E '^(FAILED|CHANGED|NEW)' "$RUN/summary.txt")
if grep -q '^FAILED' "$RUN/summary.txt"; then
  echo "NOT READY: tests failed."
elif [ "$n_bad" -gt 0 ]; then
  echo "READ FIRST: $n_bad tests answered differently or are new. If they are right: $0 --accept $RUN"
else
  echo "READY: every test answered as the accepted build did."
fi
