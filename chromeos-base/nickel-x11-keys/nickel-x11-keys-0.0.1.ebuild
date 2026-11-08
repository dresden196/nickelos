# Copyright 2026 NickelOS
# Distributed under the terms of the GNU General Public License v2

EAPI=7

inherit toolchain-funcs

DESCRIPTION="NickelOS: shortcut keys for X11 apps while other windows have the focus (push-to-talk)"
HOMEPAGE="https://nickelos.dev"
LICENSE="GPL-2"
SLOT="0"
KEYWORDS="*"

S="${WORKDIR}"

# Chrome (patch 0047) sends the keys; XTest presses them in Xwayland.
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
	pc="$(tc-getPKG_CONFIG) gio-2.0 gio-unix-2.0 xcb xcb-xtest"
	# shellcheck disable=SC2046 # pkg-config output is a list of flags
	$(tc-getCC) ${CFLAGS} ${CPPFLAGS} -Wall $(${pc} --cflags) \
		-o nickel-x11-keys "${FILESDIR}"/nickel-x11-keys.c \
		${LDFLAGS} $(${pc} --libs) || die
}

src_install() {
	dobin nickel-x11-keys
	insinto /etc/init
	doins "${FILESDIR}"/init/nickel-x11-keys.conf
}
