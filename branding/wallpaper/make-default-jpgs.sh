#!/bin/sh
# NickelOS's default wallpaper (the wave, Matteo Ulisse's on Unsplash since
# 2026-11-04, CREDITS.md) as the two files
# (and their dark-mode twins)
# ChromeOS's session manager hands Chrome (--default-wallpaper-large/-small):
# chromeos-base/chromeos-bsp-nickel/files/wallpaper/.
#   large: 2560 wide, in proportion (3:2), shown centre-cropped on screens
#          over 1366x800;
#   small: 1366x800 from the middle, shown centred and NOT scaled on smaller
#          screens, so it must cover 1366x800 (1366x768 left black bars on
#          1280x800).
# Needs ImageMagick.
set -eu
here=$(cd "$(dirname "$0")" && pwd)
src="$here/../../chromeos-base/chromeos-bsp-nickel/files/wallpaper/nickel/matteo-ulisse-0gYJgxcHgJ4.jpg"
out="$here/../../chromeos-base/chromeos-bsp-nickel/files/wallpaper"
mkdir -p "$out"
magick "$src" -resize 2560x -quality 88 -sampling-factor 4:2:0 -strip \
  "$out/default_large.jpg"
magick "$src" -resize 1366x -gravity center -crop 1366x800+0+0 +repage \
  -quality 88 -sampling-factor 4:2:0 -strip "$out/default_small.jpg"
# Dark mode's (Dresden's darker grade, made for the earlier wave and applied
# to this one as the same curve; framed the same, so switching modes
# cross-fades without the wave moving): the same two sizes.
dark="$here/../../chromeos-base/chromeos-bsp-nickel/files/wallpaper/nickel/matteo-ulisse-0gYJgxcHgJ4-dark.jpg"
magick "$dark" -resize 2560x -quality 88 -sampling-factor 4:2:0 -strip \
  "$out/default_dark_large.jpg"
magick "$dark" -resize 1366x -gravity center -crop 1366x800+0+0 +repage \
  -quality 88 -sampling-factor 4:2:0 -strip "$out/default_dark_small.jpg"
ls -l "$out"
