#!/bin/sh
# NickelOS's boot animation, rendered from Dresden's logo
# (../logo/nickelos-logo-flat.svg, the one logo everywhere): the disc fades
# in on black while it grows from 88% to its full size, in 20 frames that
# frecon shows 25 ms apart (half a second), then holds on the last one until
# the sign-in screen.
# Two sets, as ChromeOS has: 160 px for 100% screens, 320 px for hi-DPI.
#   make-boot-splash.sh OUT_DIR   (writes OUT_DIR/images_{100,200}_percent)
set -eu
here=$(dirname "$0")
logo=$here/../logo/nickelos-logo-flat.svg
out=$1
frames=20
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
for density in 100 200; do
	size=$((160 * density / 100))      # the logo at full size
	canvas=$((size * 5 / 4))           # room around it, black like the screen
	dir=$out/images_${density}_percent
	mkdir -p "$dir"
	i=1
	while [ $i -le $frames ]; do
		# ease-out: fast at first, settling at the end
		read -r scale alpha <<-EOT
		$(awk -v i=$i -v n=$frames 'BEGIN { t = i / n; e = 1 - (1 - t) ^ 3; printf "%.4f %.4f\n", 0.88 + 0.12 * e, e }')
		EOT
		px=$(awk -v s=$size -v k=$scale 'BEGIN { printf "%d\n", s * k + 0.5 }')
		rsvg-convert -w "$px" -h "$px" "$logo" -o "$tmp/logo.png"
		magick -size "${canvas}x${canvas}" xc:"#000000" -colorspace sRGB \
			\( "$tmp/logo.png" -channel A -evaluate multiply "$alpha" +channel \) \
			-gravity center -composite -strip PNG24:"$dir/$(printf 'boot_splash_frame%02d.png' $i)"
		i=$((i + 1))
	done
done
printf '0xff000000\n' > "$out/splash_background"
