#!/bin/bash
# NickelOS, one Mesa (vision decision 5), in a test VM signed in as
# tester (nickel-vmssh "bash -s" < uf-mesa-check.sh). Gentoo's Mesa for Chrome
# and Linux apps, minigbm beside it in /usr/lib64/cros:
#   1. the image: Gentoo's Mesa recorded, Google's and nickel-mesa gone
#   2. Chrome's GPU process: Gentoo's EGL and drivers, minigbm's libgbm
#      (Chrome allocates with minigbm); not Mesa's libgbm
#   3. Xwayland: Mesa's libgbm (glamor), not minigbm's
#   4. Linux apps' EGL: the vendor file the session names, Gentoo's
#   5. crash reports of Chrome's GPU process, Xwayland, sommelier
maps() { grep -oE '/[^ ]*\.so[^ ]*' "/proc/$1/maps" 2>/dev/null | sort -u; }
echo "== 1. the image"
ls -d /var/db/pkg/media-libs/mesa-* /var/db/pkg/media-libs/mesa-reven-* /var/db/pkg/media-libs/nickel-mesa-* 2>/dev/null | sed 's|/var/db/pkg/||'
ls -d /usr/lib64/nickel-mesa 2>&1 | sed 's/^/  /'
echo "libgbm.so.1: $(readlink -f /usr/lib64/libgbm.so.1) (Mesa's), $(readlink -f /usr/lib64/cros/libgbm.so.1) (minigbm)"
echo "== 2. Chrome's GPU process"
gpu=$(pgrep -f -- '--type=gpu-process' | head -1)
if [ -n "$gpu" ]; then
  maps "$gpu" | grep -E 'libEGL|libgbm|libgallium|_dri\.so|libLLVM|libglapi|drv_video' | sed 's/^/  /'
else
  echo "  no GPU process"
fi
grep -hE 'GPU process (exited|crashed|isn.t usable)|Failed to initialize GL|eglInitialize .* failed|SwiftShader|ANGLE' /var/log/chrome/chrome 2>/dev/null | tail -5 | cut -c1-200 | sed 's/^/  log: /'
echo "== 3. Xwayland"
x=$(pgrep -x Xwayland | head -1)
[ -n "$x" ] && maps "$x" | grep -E 'libgbm|libEGL|_dri\.so|libgallium' | sed 's/^/  /' || echo "  no Xwayland"
echo "== 4. Linux apps' EGL"
grep -h '^__EGL_VENDOR_LIBRARY_FILENAMES' /run/nickel/session/env 2>/dev/null
for f in /usr/share/glvnd/egl_vendor.d/*.json; do echo "  $f: $(grep -o '"library_path"[^,}]*' "$f")"; done
echo "== 5. crashes"
ls /var/spool/crash/ /home/chronos/u-*/crash/ 2>/dev/null | grep -E 'chrome|Xwayland|sommelier|gpu' | sort -u | head
