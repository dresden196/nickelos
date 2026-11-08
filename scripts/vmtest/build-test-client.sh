#!/bin/bash
# Runs on nickel-build: builds a test client from src/NAME.c for the board
# (the SDK's clang against /build/nickel) and installs it in the test VM as
# /usr/local/bin/NAME.
#   build-test-client.sh NAME PKG-CONFIG-PACKAGE...
set -eu
name=$1
shift
SRC=/srv/nickel/src
B=/build/nickel
install -o nickel -m644 "$(dirname "$0")/src/$name.c" "$SRC/.nickel/"
# One line per command: cros_sdk turns the script's newlines into spaces.
(cd "$SRC" && sudo -u nickel -H "$SRC/chromite/bin/cros_sdk" -- bash -c "
  cd /mnt/host/source/.nickel &&
  export PKG_CONFIG_SYSROOT_DIR=$B PKG_CONFIG_LIBDIR=$B/usr/lib64/pkgconfig:$B/usr/share/pkgconfig &&
  x86_64-cros-linux-gnu-clang --sysroot=$B -O2 -Wall -o $name $name.c \$(pkg-config --cflags --libs $*)")
"$(dirname "$0")"/nickel-vmpush /usr/local/bin/$name \
  < "$SRC/.nickel/$name"
