# Distributed under the terms of the GNU General Public License v2

EAPI=7

inherit toolchain-funcs

DESCRIPTION="NickelOS: user sessions known to elogind, polkit for the signed-in user"
LICENSE="GPL-2"
SLOT="0"
KEYWORDS="*"

S="${WORKDIR}"

DEPEND="sys-libs/pam
	dev-libs/glib:2
	sys-auth/polkit
"
RDEPEND="${DEPEND}
	chromeos-base/nickel-desktop-base
	sys-auth/elogind[pam]
	sys-auth/polkit[elogind]
"

src_compile() {
	$(tc-getCC) ${CFLAGS} ${CPPFLAGS} -Wall -o nickel-logind-session \
		"${FILESDIR}"/nickel-logind-session.c ${LDFLAGS} -lpam || die
	# The polkit agent: ChromeOS's own password prompt (Chrome 0103).
	$(tc-getCC) ${CFLAGS} ${CPPFLAGS} -Wall -o nickel-polkit-agent \
		"${FILESDIR}"/nickel-polkit-agent.c ${LDFLAGS} \
		$($(tc-getPKG_CONFIG) --cflags --libs polkit-agent-1) || die
}

src_install() {
	dosbin nickel-logind-session
	exeinto /usr/libexec
	doexe nickel-polkit-agent
	insinto /etc/dbus-1/system.d
	doins "${FILESDIR}"/dbus/org.nickelos.PolkitAgent.conf
	insinto /etc/init
	doins "${FILESDIR}"/init/elogind.conf "${FILESDIR}"/init/nickel-logind-session.conf \
		"${FILESDIR}"/init/nickel-polkit-agent.conf
	insinto /etc/pam.d
	doins "${FILESDIR}"/pam.d/nickel-session
	insinto /etc/polkit-1/rules.d
	doins "${FILESDIR}"/polkit/49-nickel-admin.rules "${FILESDIR}"/polkit/49-nickel-power.rules
}
