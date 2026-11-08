#!/bin/sh
# The small media previews-test.sh copies into the test VM: pictures and
# videos that are blue-to-teal all over (with their format written on them),
# so a preview shows as blue-teal where a generic icon (gray tile, red or
# orange glyph) does not. Runs anywhere with ImageMagick (with HEIC) and
# FFmpeg (with x264 and x265); the results are in media/.
set -eu
out="$(cd "$(dirname "$0")" && pwd)/media"
font=$(fc-match -f '%{file}' 'sans:bold')
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
magick -size 320x200 gradient:'#2f56a0'-'#22a99c' -depth 8 "$tmp/base.png"
for f in heic jpg; do
  magick "$tmp/base.png" -font "$font" -gravity center -pointsize 48 \
    -fill white -annotate 0 "$f" -depth 8 -quality 80 "$out/photo.$f"
done
src="gradients=s=320x180:c0=0x2f56a0:c1=0x22a99c:duration=2:speed=0.001"
ffmpeg -loglevel error -y -f lavfi -i "$src" -c:v libx265 -tag:v hvc1 \
  -pix_fmt yuv420p -x265-params log-level=error "$out/clip-hevc.mov"
ffmpeg -loglevel error -y -f lavfi -i "$src" -c:v libx264 -pix_fmt yuv420p \
  "$out/clip-h264.mp4"
ls -l "$out"
