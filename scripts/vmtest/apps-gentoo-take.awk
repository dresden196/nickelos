# Reads emerge --pretend --autounmask=y output, colours stripped, and prints
# the changes it asks for as a user takes them: "use LINE" for package.use,
# "kw LINE" for package.accept_keywords. Besides the USE and keyword changes
# Portage writes out, REQUIRED_USE constraints it only reports
# ("x264? ( gpl )" under "- media-video/ffmpeg-8.1.3::gentoo ...": OBS wants
# ffmpeg with x264, and x264 wants gpl) become the flags they name for that
# version. Used by apps-gentoo-test.sh.
/USE changes are necessary/ { u = 1; k = 0; next }
/keyword changes are necessary/ { k = 1; u = 0; next }
/^The following|^!!!|^ \*|^NOTE|^emerge:/ { u = 0; k = 0 }
u && /^>?=/ { print "use " $0 }
k && /^>?=/ { print "kw " $0 }
/^- [a-z0-9-]+\/[^ ]+::/ { p = $2; sub(/::.*/, "", p) }
/REQUIRED_USE flag constraints are unsatisfied/ { r = 1; next }
r && /^ +[a-z0-9_+-]+\? \( [a-z0-9_+ -]+ \)$/ {
	f = $0
	sub(/^[^(]*\( /, "", f)
	sub(/ \)$/, "", f)
	print "use >=" p " " f
	next
}
{ r = 0 }
