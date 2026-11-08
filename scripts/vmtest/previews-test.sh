#!/bin/bash
# Runs on a build host, as root, with tester signed in in the test VM (port
# 9222; local-account-test.sh). Picture previews (Chrome 0066, nickel-appd
# r19's thumbnail.cc, docs/vision.md "Picture previews"):
#   1. nickel-appd makes the previews Chrome can't: Thumbnail(HEIC) and
#      Thumbnail(H.265 MOV) answer PNGs, kept in ~/.cache/thumbnails/large/
#      under the MD5 of the file's URI, with Thumb::URI and Thumb::MTime
#   2. the desktop shows previews: an H.264 MP4, an H.265 MOV, a HEIC and a
#      JPEG on the desktop are blue-teal where their tiles are
#   3. Files shows them too: a folder with only the HEIC, then one with only
#      the H.265 MOV, has blue-teal in its file list
# The media (media/, make-previews-media.sh) are blue-teal all over; a
# generic icon (gray tile, red or orange glyph) is not. Screens are taken
# through the Screenshot portal (QEMU's screendump leaves things out). The
# desktop's own items are moved aside for the test and put back.
set -u
V=/srv/nickel/vmtest
here=$(dirname "$(readlink -f "$0")")
vm() { nickel-vmssh "$@" </dev/null; }
T=/home/tester/previews-test

"$here"/build-test-client.sh portal-client gio-2.0 || exit 1
crashes_before=$(vm 'ls /var/spool/crash/ 2>/dev/null | wc -l')
vm "rm -rf $T; mkdir -p $T/heic $T/hevc"
tar -C "$here/media" -cf - . | nickel-vmssh "tar -C $T -xf -"
vm "cp $T/photo.heic $T/heic/; cp $T/clip-hevc.mov $T/hevc/; chown -R tester:tester $T"

# The screen as Chrome draws it, through the Screenshot portal.
capture() {   # OUT.png
  vm "rm -f /tmp/shot.out; nohup nickel-session-exec /usr/local/bin/portal-client Screenshot - - 60 > /tmp/shot.out 2>&1 &"
  sleep 6; nickel-vmctl click 505 357; sleep 2; nickel-vmctl click 888 545; sleep 8
  local uri path
  uri=$(vm 'cat /tmp/shot.out' | grep -o "file://[^ ]*" | head -1)
  path=$(python3 -c "import sys, urllib.parse; print(urllib.parse.unquote(sys.argv[1][7:]))" "$uri")
  vm "cat '$path'; rm -f '$path'" > "$1"
}
# The share of blue-teal pixels in boxes of a screen: "x0 y0 x1 y1" each.
blue_teal() {   # PNG BOX...
  python3 - "$@" <<'PY'
import subprocess, sys
data = subprocess.run(["pngtopnm", sys.argv[1]], capture_output=True, check=True).stdout
fields, pos = [], 0
while len(fields) < 4:
    while data[pos:pos + 1].isspace():
        pos += 1
    end = pos
    while not data[end:end + 1].isspace():
        end += 1
    fields.append(data[pos:end]); pos = end
width, pixels = int(fields[1]), data[pos + 1:]
for box in sys.argv[2:]:
    x0, y0, x1, y1 = map(int, box.split())
    n = 0
    for y in range(y0, y1):
        for x in range(x0, x1):
            i = (y * width + x) * 3
            r, g, b = pixels[i], pixels[i + 1], pixels[i + 2]
            if r < 110 and b > r + 50 and g > 60:
                n += 1
    print(n, (x1 - x0) * (y1 - y0))
PY
}

echo "== 1. nickel-appd's thumbnails"
for f in photo.heic clip-hevc.mov; do
  out=$(vm "nickel-session-exec gdbus call --system --dest org.nickelos.Apps --object-path /org/nickelos/Apps --method org.nickelos.Apps.Thumbnail $T/$f 256 2>&1 | head -c 40")
  case "$out" in
    "([byte 0x89, 0x50, 0x4e, 0x47"*) echo "Thumbnail $f: a PNG" ;;
    *) echo "Thumbnail $f: FAILED, $out" ;;
  esac
  md5=$(python3 -c "import hashlib, sys; print(hashlib.md5(sys.argv[1].encode()).hexdigest())" "file://$T/$f")
  cached=/home/tester/.cache/thumbnails/large/$md5.png
  if vm "grep -aq 'Thumb::URI' $cached && grep -aq 'file://$T/$f' $cached && grep -aq 'Thumb::MTime' $cached"; then
    echo "  cached with Thumb::URI and Thumb::MTime"
  else
    echo "  cache: FAILED, $cached"
  fi
done

echo "== 2. the desktop"
vm "mkdir -p /home/tester/.previews-test-aside; find /home/tester/Desktop -mindepth 1 -maxdepth 1 -exec mv -t /home/tester/.previews-test-aside {} +"
vm "cp $T/clip-h264.mp4 $T/clip-hevc.mov $T/photo.heic $T/photo.jpg /home/tester/Desktop/ && chown tester:tester /home/tester/Desktop/*"
sleep 20
capture "$V/flow/previews-desktop.png"
# One column, by name: clip-h264.mp4, clip-hevc.mov, photo.heic, photo.jpg;
# tile centres at (60, 46 + 104 * row).
i=0
while read -r n total; do
  name=$(echo "clip-h264.mp4 clip-hevc.mov photo.heic photo.jpg" | cut -d" " -f$((i + 1)))
  if [ $((n * 100 / total)) -ge 40 ]; then echo "desktop $name: a preview"
  else echo "desktop $name: FAILED, $((n * 100 / total))% blue-teal"; fi
  i=$((i + 1))
done < <(blue_teal "$V/flow/previews-desktop.png" \
  "36 26 84 66" "36 130 84 170" "36 234 84 274" "36 338 84 378")
vm "rm -f /home/tester/Desktop/clip-h264.mp4 /home/tester/Desktop/clip-hevc.mov /home/tester/Desktop/photo.heic /home/tester/Desktop/photo.jpg; find /home/tester/.previews-test-aside -mindepth 1 -maxdepth 1 -exec mv -t /home/tester/Desktop {} +; rmdir /home/tester/.previews-test-aside"

echo "== 3. Files"
for d in heic hevc; do
  vm "nohup nickel-session-exec nickel-open $T/$d >/dev/null 2>&1 &"
  sleep 10
  capture "$V/flow/previews-files-$d.png"
  # The file list, right of the navigation (whose selection is blue).
  read -r n total < <(blue_teal "$V/flow/previews-files-$d.png" "360 110 1180 740")
  if [ "$n" -ge 150 ]; then echo "Files, $d: a preview"
  else echo "Files, $d: FAILED, $n blue-teal pixels"; fi
  nickel-vmctl key ctrl-w; sleep 2
done
vm "rm -rf $T"
echo "new crashes: $(( $(vm 'ls /var/spool/crash/ 2>/dev/null | wc -l') - crashes_before ))"
