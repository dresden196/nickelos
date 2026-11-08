#!/bin/bash
# Inside cros_sdk: compile individual Chrome objects (fast check of patched
# files without an emerge). Args: source paths relative to Chrome's src/
# (chrome/browser/foo.cc) or ninja targets (obj/...).
set -euo pipefail
src=/var/cache/chromeos-cache/distfiles/chrome-src/src
out=/var/cache/chromeos-chrome/chrome-src/src/out_nickel/Release
# BUILD.gn changes (new files) need regenerated ninja files first.
(cd "$out" && ninja build.ninja >/dev/null)
targets=()
for t in "$@"; do
  if [[ $t == *.cc ]]; then
    obj=$(grep -rhoE "^build (obj/[^ ]+\.o) [^:]*: [a-z_]+ [./]*/chromeos-cache/distfiles/chrome-src/src/${t//./\\.}( |\$)" \
            --include='*.ninja' "$out/obj" | head -1 | awk '{print $2}')
    [[ -n $obj ]] || { echo "no object for $t" >&2; exit 1; }
    targets+=("$obj")
  else
    targets+=("$t")
  fi
done
cd "$out"
exec ninja "${targets[@]}"
