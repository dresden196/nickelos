# Copyright 2026 The NickelOS Authors
# Distributed under the terms of the GNU General Public License v2

# Cloud storage in Files through rclone (files/nickel-cloud.cc): a session
# service that fetches rclone on first use, runs its daemon, mounts the
# user's accounts and answers Settings and Files over the session bus.

EAPI=7

inherit toolchain-funcs

DESCRIPTION="NickelOS: cloud storage in Files, through rclone"
HOMEPAGE="https://github.com/dresden196/overlay-nickel"

LICENSE="GPL-2"
SLOT="0"
KEYWORDS="*"

S="${WORKDIR}"

COMMON_DEPEND="
	dev-libs/glib:2
	dev-libs/json-glib
"
DEPEND="${COMMON_DEPEND}"
RDEPEND="${COMMON_DEPEND}
	app-arch/libarchive
	chromeos-base/nickel-session
	net-misc/curl
	sys-fs/fuse3
	x11-misc/xdg-utils
"
BDEPEND="virtual/pkgconfig"

src_compile() {
	local pc
	pc="$(tc-getPKG_CONFIG) gio-unix-2.0 json-glib-1.0"
	# shellcheck disable=SC2046 # pkg-config output is a list of flags
	$(tc-getCXX) ${CXXFLAGS} ${CPPFLAGS} -std=c++17 -Wall \
		$(${pc} --cflags) -o nickel-cloud "${FILESDIR}"/nickel-cloud.cc \
		${LDFLAGS} $(${pc} --libs) || die
}

src_install() {
	dobin nickel-cloud
	insinto /etc/init
	doins "${FILESDIR}"/init/nickel-cloud.conf
}
