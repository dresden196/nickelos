# Copyright 2026 NickelOS
# Distributed under the terms of the GNU General Public License v2

EAPI=7

inherit toolchain-funcs

XIMDKIT="xcb-imdkit-1.0.9"

DESCRIPTION="ChromeOS's input methods for X11 apps: an XIM server on sommelier's text input"
HOMEPAGE="https://nickelos.dev"
# xcb-imdkit (fcitx's XIM library), built in: it is not in the SDK's tree,
# and the one xcb-util function it needs comes with it (files/shim).
SRC_URI="https://github.com/fcitx/xcb-imdkit/archive/refs/tags/1.0.9.tar.gz -> ${XIMDKIT}.tar.gz"
RESTRICT="mirror"

# nickel-xim (GPL-2); xcb-imdkit (LGPL-2.1) with Xlib's text converters
# (MIT) and uthash (BSD); the text-input protocols (MIT, BSD).
LICENSE="GPL-2 LGPL-2.1 MIT BSD"
SLOT="0"
KEYWORDS="*"

S="${WORKDIR}/${XIMDKIT}"

COMMON_DEPEND="
	dev-libs/wayland
	x11-libs/libxcb:=
	x11-libs/libxkbcommon
"
DEPEND="${COMMON_DEPEND}"
RDEPEND="${COMMON_DEPEND}
	chromeos-base/nickel-session
	chromeos-base/sommelier
"
BDEPEND="
	dev-util/wayland-scanner
	virtual/pkgconfig
"

PATCHES=( "${FILESDIR}/${XIMDKIT}-nickel.patch" )

src_compile() {
	local p name pc
	mkdir -p gen || die
	for p in "${FILESDIR}"/protocol/*.xml; do
		name=$(basename "${p}" .xml)
		wayland-scanner client-header "${p}" "gen/${name}-client-protocol.h" || die
		wayland-scanner private-code "${p}" "gen/${name}-protocol.c" || die
	done
	pc="$(tc-getPKG_CONFIG) xcb wayland-client xkbcommon"
	# shellcheck disable=SC2046 # pkg-config output is a list of flags
	$(tc-getCC) ${CFLAGS} ${CPPFLAGS} -std=gnu11 -D_GNU_SOURCE -Wall \
		-Wno-unused-parameter -I"${FILESDIR}"/shim -Isrc -Iuthash -Igen \
		$(${pc} --cflags) -o nickel-xim "${FILESDIR}"/nickel-xim.c \
		gen/*-protocol.c \
		src/{parser,ximproto,imdkit,protocolhandler,message,common,encoding}.c \
		src/xlibi18n/{lcCT,lcUTF8,lcCharSet}.c \
		${LDFLAGS} $(${pc} --libs) || die
}

src_install() {
	dobin nickel-xim
	insinto /etc/init
	doins "${FILESDIR}"/init/nickel-xim.conf
}
