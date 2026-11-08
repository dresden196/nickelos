# Copyright 2026 The NickelOS Authors
# Distributed under the terms of the GNU General Public License v2

# uBlock Origin, the full Manifest V2 one, for setup's Full experience
# (Dresden, 2026-10-23). The CRX is its author's own signed Chromium release
# on GitHub (extension id fkgkibajhfbepljeaefdnfnegdcjomkh; the Web Store
# no longer serves Manifest V2 extensions). Chrome installs it as an external
# extension on a device whose setup chose the Full experience ("nickel_
# experience", Chrome 0090, which also keeps Manifest V2 working), updates it
# when a NickelOS image carries a newer one, and remembers a user's
# uninstall. Its filter lists update by themselves.

EAPI=7

DESCRIPTION="uBlock Origin (Manifest V2) for NickelOS's Full experience"
HOMEPAGE="https://github.com/gorhill/uBlock"
SRC_URI="https://github.com/gorhill/uBlock/releases/download/${PV}/uBlock0_${PV}.chromium.crx -> ublock-origin-${PV}.crx"

LICENSE="GPL-3"
SLOT="0"
KEYWORDS="*"
# Not on Google's mirrors, which the board's Portage otherwise fetches from.
RESTRICT="mirror"

S="${WORKDIR}"

EXTENSION_ID="fkgkibajhfbepljeaefdnfnegdcjomkh"

src_unpack() {
	cp "${DISTDIR}/ublock-origin-${PV}.crx" "${S}/ublock-origin.crx" || die
}

src_compile() {
	# The id in the CRX's signed header must be the one Chrome expects from
	# the file name (a release signed with another key would not install).
	local id
	id=$(python3 - ublock-origin.crx <<-'PY'
	import struct, sys
	d = open(sys.argv[1], "rb").read()
	assert d[:4] == b"Cr24" and struct.unpack("<I", d[4:8])[0] == 3
	hdr = d[12:12 + struct.unpack("<I", d[8:12])[0]]
	def fields(b):
	    i = 0
	    while i < len(b):
	        k = s = 0
	        while True:
	            c = b[i]; i += 1; k |= (c & 0x7f) << s; s += 7
	            if c < 0x80: break
	        if k & 7 == 2:
	            n = s = 0
	            while True:
	                c = b[i]; i += 1; n |= (c & 0x7f) << s; s += 7
	                if c < 0x80: break
	            yield k >> 3, b[i:i + n]; i += n
	        else:
	            while b[i] & 0x80: i += 1
	            i += 1
	for tag, val in fields(hdr):
	    if tag == 10000:
	        for t, v in fields(val):
	            if t == 1:
	                print("".join(chr(97 + int(c, 16)) for c in v.hex()))
	PY
	) || die
	[[ ${id} == "${EXTENSION_ID}" ]] || die "uBlock Origin's CRX has id '${id}', not ${EXTENSION_ID}"
	sed "s/@VERSION@/${PV}/" "${FILESDIR}/${EXTENSION_ID}.json.in" \
		> "${EXTENSION_ID}.json" || die
}

src_install() {
	insinto /usr/share/nickel/extensions
	doins ublock-origin.crx
	insinto /usr/share/chromium/extensions
	doins "${EXTENSION_ID}.json"
}
