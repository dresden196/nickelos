# Copyright 2026 NickelOS
# Distributed under the terms of the GNU General Public License v2

EAPI=7

inherit toolchain-funcs

DESCRIPTION="NickelOS: old-style X11 tray icons (XEmbed: Wine, Java) on ChromeOS's shelf"
HOMEPAGE="https://nickelos.dev"
LICENSE="GPL-2"
SLOT="0"
KEYWORDS="*"

S="${WORKDIR}"

COMMON_DEPEND="
	dev-libs/glib:2
	x11-libs/libxcb:=
"
DEPEND="${COMMON_DEPEND}"
RDEPEND="${COMMON_DEPEND}
	chromeos-base/nickel-appd
	chromeos-base/nickel-session
"
BDEPEND="virtual/pkgconfig"

src_compile() {
	local pc
	pc="$(tc-getPKG_CONFIG) gio-2.0 gio-unix-2.0 xcb"
	# shellcheck disable=SC2046 # pkg-config output is a list of flags
	$(tc-getCC) ${CFLAGS} ${CPPFLAGS} -Wall $(${pc} --cflags) \
		-o nickel-xembed-tray "${FILESDIR}"/nickel-xembed-tray.c \
		${LDFLAGS} $(${pc} --libs) || die
}

src_install() {
	dobin nickel-xembed-tray
	insinto /etc/init
	doins "${FILESDIR}"/init/nickel-xembed-tray.conf
}
