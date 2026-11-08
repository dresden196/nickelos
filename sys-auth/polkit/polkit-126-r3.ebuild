# Copyright 1999-2026 Gentoo Authors
# Distributed under the terms of the GNU General Public License v2

EAPI=7

# NickelOS: ported from Gentoo polkit-126-r3 (EAPI 8 -> 7): KEYWORDS="*", dropped 9999/snapshot branches, test support (dbusmock) and man pages (docbook); acct-user/polkitd is no longer a BDEPEND (the SDK cannot see this overlay): pkg_setup creates the account through user.eclass instead; PAM file goes to /etc/pam.d (ChromiumOS PAM has no vendordir).

inherit meson pam pax-utils systemd tmpfiles user xdg-utils

DESCRIPTION="Policy framework for controlling privileges for system-wide services"
HOMEPAGE="https://www.freedesktop.org/wiki/Software/polkit https://github.com/polkit-org/polkit"
SRC_URI="https://github.com/polkit-org/polkit/archive/refs/tags/${PV}.tar.gz -> ${P}.tar.gz"

LICENSE="LGPL-2"
SLOT="0"
KEYWORDS="*"
IUSE="elogind examples gtk introspection kde pam nls selinux systemd"
REQUIRED_USE="?? ( elogind systemd )"

BDEPEND="
	>=dev-libs/glib-2.32
	dev-util/glib-utils
	virtual/pkgconfig
	introspection? ( dev-libs/gobject-introspection )
	nls? ( sys-devel/gettext )
"
DEPEND="
	>=dev-libs/glib-2.32:2
	dev-libs/expat
	dev-lang/duktape:=
	elogind? ( sys-auth/elogind )
	pam? (
		sys-auth/pambase
		sys-libs/pam
	)
	!pam? ( virtual/libcrypt:= )
	systemd? ( sys-apps/systemd:0=[policykit] )
"
RDEPEND="
	${DEPEND}
	acct-group/polkitd
	acct-user/polkitd
	selinux? ( sec-policy/selinux-policykit )
"
PDEPEND="
	gtk? ( || (
		>=gnome-extra/polkit-gnome-0.105
		>=lxde-base/lxsession-0.5.2
	) )
	kde? ( kde-plasma/polkit-kde-agent )
"

DOCS=( docs/TODO HACKING.md NEWS.md README.md )

QA_MULTILIB_PATHS="
	usr/lib/polkit-1/polkit-agent-helper-1
	usr/lib/polkit-1/polkitd
"

PATCHES=(
	"${FILESDIR}"/${P}-elogind.patch
)

pkg_setup() {
	# ChromiumOS: creating the account in pkg_setup also adds it to the build
	# root, so the rules.d ownership below resolves at install time.
	enewgroup polkitd
	enewuser polkitd
}

src_prepare() {
	default

	# bug #401513
	sed -i -e 's|unix-group:@PRIVILEGED_GROUP@|unix-user:@PRIVILEGED_GROUP@|' src/polkitbackend/*-default.rules.in || die
}

src_configure() {
	xdg_environment_reset

	# Use ConsoleKit as a fallback with no build-time dependencies, bug 973339
	local session_tracking=ConsoleKit
	use elogind && session_tracking=elogind
	use systemd && session_tracking=logind

	local emesonargs=(
		--localstatedir="${EPREFIX}"/var
		-Dauthfw="$(usex pam pam shadow)"
		-Dexamples=false
		-Dgtk_doc=false
		-Dman=false
		-Dos_type=gentoo
		-Dpam_module_dir=$(getpam_mod_dir)
		-Dpam_prefix="${EPREFIX}"/etc/pam.d
		-Dprivileged_group=0
		-Dsession_tracking="${session_tracking}"
		-Dsystemdsystemunitdir="$(systemd_get_systemunitdir)"
		-Dlibs-only=false
		$(meson_use introspection)
		$(meson_use nls gettext)
		-Dtests=false
	)
	meson_src_configure
}

src_compile() {
	meson_src_compile

	# Required for polkitd on hardened/PaX due to spidermonkey's JIT
	pax-mark mr src/polkitbackend/.libs/polkitd test/polkitbackend/.libs/polkitbackendjsauthoritytest
}

src_install() {
	meson_src_install

	# acct-user/polkitd installs its own (albeit with a different filename)
	rm -rf "${ED}"/usr/lib/sysusers.d || die

	if use examples ; then
		docinto examples
		dodoc src/examples/{*.c,*.policy*}
	fi

	if [[ ${EUID} == 0 ]]; then
		diropts -m 0700 -o polkitd
	fi
	keepdir /etc/polkit-1/rules.d
}

pkg_postinst() {
	tmpfiles_process polkit-tmpfiles.conf

	if [[ ${EUID} == 0 ]]; then
		chmod 0700 "${EROOT}"/{etc,usr/share}/polkit-1/rules.d
		chown polkitd "${EROOT}"/{etc,usr/share}/polkit-1/rules.d
	fi
}
