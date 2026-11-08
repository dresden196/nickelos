# Distributed under the terms of the GNU General Public License v2

EAPI=7

inherit toolchain-funcs

DESCRIPTION="NickelOS local accounts: real Linux users kept on the stateful partition"
LICENSE="GPL-2"
SLOT="0"
KEYWORDS="*"

S="${WORKDIR}"

COMMON_DEPEND="
	dev-libs/glib:2
	sys-libs/libxcrypt:=
"
DEPEND="${COMMON_DEPEND}"
RDEPEND="${COMMON_DEPEND}
	app-admin/sudo
	sys-apps/util-linux
"
BDEPEND="virtual/pkgconfig"

src_compile() {
	local pc
	pc="$(tc-getPKG_CONFIG) gio-2.0 libcrypt"
	# shellcheck disable=SC2046 # pkg-config output is a list of flags
	$(tc-getCXX) ${CXXFLAGS} ${CPPFLAGS} -std=c++17 -Wall \
		$(${pc} --cflags) -o nickel-accountsd "${FILESDIR}"/nickel-accountsd.cc \
		${LDFLAGS} $(${pc} --libs) || die
}

src_install() {
	dosbin nickel-accountsd "${FILESDIR}"/nickel-accounts-setup
	insinto /etc/init
	doins "${FILESDIR}"/init/nickel-accounts-setup.conf
	doins "${FILESDIR}"/init/nickel-accountsd.conf
	insinto /etc/dbus-1/system.d
	doins "${FILESDIR}"/dbus/org.nickelos.Accounts.conf
	insinto /etc/sudoers.d
	insopts -m 0440
	doins "${FILESDIR}"/sudoers/90-nickel-wheel
}
