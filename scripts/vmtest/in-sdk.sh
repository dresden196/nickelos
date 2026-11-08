#!/bin/bash
# Runs on a build host, as root: the shell script on stdin, inside the SDK
# as the build user, from /mnt/host/source/.nickel (where the tests put the
# sources of their test programs). `cros_sdk -- bash -c "..."` turns the
# newlines into spaces, which breaks loops and line continuations without a
# word (the build fails, and the test goes on with an old binary); a file
# keeps them. Stops at the first failing command.
#   in-sdk.sh <<'SH'
#   ...
#   SH
set -eu
SRC=/srv/nickel/src
f=$(mktemp "$SRC/.nickel/in-sdk.XXXXXX")
trap 'rm -f "$f"' EXIT
{ echo 'set -e'; echo 'cd /mnt/host/source/.nickel'; cat; } > "$f"
chmod 755 "$f"; chown nickel:nickel "$f"
cd "$SRC" && sudo -u nickel -H "$SRC/chromite/bin/cros_sdk" -- bash "/mnt/host/source/.nickel/$(basename "$f")"
