# Copyright 2011-2026 Gentoo Authors
# Distributed under the terms of the GNU General Public License v2

EAPI=7

# NickelOS: ported from Gentoo accountsservice-26.27.3 (EAPI 8 -> 7): KEYWORDS="*", gdbus-codegen floor 2.80.5-r1 relaxed to ChromiumOS's 2.76.4; logind is now optional (REQUIRED_USE ^^ -> ??): a new patch builds only accounts-daemon when neither systemd nor elogind is enabled (only the libaccountsservice client library uses sd-login); dropped tests (python-dbusmock), vala and the OpenRC init script; no systemd unit installed without USE=systemd; msgfmt pointed at the sysroot for polkit ITS rules.

inherit meson systemd

DESCRIPTION="D-Bus interfaces for querying and manipulating user account information"
HOMEPAGE="https://gitlab.freedesktop.org/accountsservice/accountsservice"
SRC_URI="https://gitlab.freedesktop.org/accountsservice/accountsservice/-/archive/${PV}/${P}.tar.bz2"

LICENSE="GPL-3+"
SLOT="0/1"
KEYWORDS="*"

IUSE="doc elogind gtk-doc introspection selinux systemd xcrypt"
RESTRICT="test"
REQUIRED_USE="
	?? ( elogind systemd )
	gtk-doc? ( || ( elogind systemd ) )
	introspection? ( || ( elogind systemd ) )
"

CDEPEND="
	>=dev-libs/glib-2.70:2
	>=dev-libs/json-c-0.15
	sys-auth/polkit
	virtual/libcrypt:=
	elogind? ( >=sys-auth/elogind-229.4 )
	introspection? ( dev-libs/gobject-introspection:= )
	systemd? ( >=sys-apps/systemd-186:0= )
	xcrypt? ( sys-libs/libxcrypt )
"
DEPEND="${CDEPEND}
	sys-apps/dbus
"
BDEPEND="
	dev-libs/libxslt
	dev-util/gdbus-codegen
	dev-util/glib-utils
	sys-devel/gettext
	virtual/pkgconfig
	doc? (
		app-text/docbook-xml-dtd:4.1.2
		app-text/xmlto
	)
	gtk-doc? (
		dev-util/gtk-doc
		app-text/docbook-xml-dtd:4.3
	)
"
RDEPEND="${CDEPEND}
	selinux? ( sec-policy/selinux-accountsd )
"

PATCHES=(
	"${FILESDIR}"/${PN}-22.04.62-gentoo-system-users.patch
	"${FILESDIR}"/${PN}-26.27.3-optional-libaccountsservice.patch
)

src_configure() {
	local emesonargs=(
		--localstatedir="${EPREFIX}/var"
		-Dsystemdsystemunitdir="$(usex systemd "$(systemd_get_systemunitdir)" no)"
		-Dwtmpfile=/var/log/wtmp
		-Dadmin_group="wheel"
		-Dcreate_homed=false
		$(meson_use elogind)
		-Dlibaccountsservice=$(usex elogind true "$(usex systemd true false)")
		$(meson_use introspection)
		$(meson_use doc docbook)
		$(meson_use gtk-doc gtk_doc)
		-Dtests=false
		-Dvapi=false
		$(meson_use xcrypt libxcrypt)
	)
	meson_src_configure
}

src_compile() {
	# NickelOS: msgfmt needs polkit's ITS rules to merge the .policy
	# translations; they are in the sysroot, not in the SDK.
	local -x GETTEXTDATADIRS="${ESYSROOT}/usr/share/gettext"
	meson_src_compile
}

src_install() {
	meson_src_install

	# https://gitlab.freedesktop.org/accountsservice/accountsservice/-/issues/90
	if use doc; then
		mv "${ED}/usr/share/doc/${PN}" "${ED}/usr/share/doc/${PF}" || die
	fi

	# This directories are created at runtime when needed
	# (NickelOS: all of /var; ChromiumOS forbids installing into it)
	rm -r "${ED}"/var || die
}
