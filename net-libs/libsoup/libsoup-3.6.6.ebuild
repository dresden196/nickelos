# Copyright 1999-2026 Gentoo Authors
# Distributed under the terms of the GNU General Public License v2

EAPI=7

# NickelOS: ported from Gentoo libsoup-3.6.6 (EAPI 8 -> 7): KEYWORDS="*", meson-multilib -> meson (single ABI, meson_native_use_feature -> meson_feature), dropped vala/gtk-doc/sysprof/tests.

PYTHON_COMPAT=( python3_{11..14} )
inherit gnome.org meson python-any-r1 xdg

DESCRIPTION="HTTP client/server library for GNOME"
HOMEPAGE="https://libsoup.gnome.org"

LICENSE="LGPL-2.1+"
SLOT="3.0"

KEYWORDS="*"
IUSE="+brotli gssapi introspection samba ssl"
RESTRICT="test"

DEPEND="
	>=dev-libs/glib-2.70.0:2
	net-libs/nghttp2:=
	>=dev-db/sqlite-3.8.2:3
	brotli? ( >=app-arch/brotli-1.0.6-r1:= )
	>=net-libs/libpsl-0.20
	virtual/zlib:=
	gssapi? ( virtual/krb5 )
	introspection? ( dev-libs/gobject-introspection:= )
	samba? ( net-fs/samba )
"
RDEPEND="${DEPEND}
	>=net-libs/glib-networking-2.70_alpha[ssl?]
"
BDEPEND="
	${PYTHON_DEPS}
	dev-libs/glib
	dev-util/glib-utils
	>=sys-devel/gettext-0.19.8
	virtual/pkgconfig
"

PATCHES=(
	# Disable apache tests until they are usable on Gentoo, bug #326957
	"${FILESDIR}"/disable-apache-tests.patch
)

src_prepare() {
	default
	xdg_environment_reset
	# https://gitlab.gnome.org/GNOME/libsoup/issues/159 - could work with libnss-myhostname
	sed -e '/hsts/d' -i tests/meson.build || die
}

src_configure() {
	local emesonargs=(
		# Avoid auto-magic, built-in feature of meson
		-Dauto_features=enabled

		$(meson_feature gssapi)
		$(meson_feature samba ntlm)
		$(meson_feature brotli)
		-Dntlm_auth="${EPREFIX}/usr/bin/ntlm_auth"
		-Dtls_check=false # disables check, we still rdep on glib-networking
		$(meson_feature introspection)
		-Dvapi=disabled
		-Ddocs=disabled
		-Ddoc_tests=false
		-Dtests=false
		-Dautobahn=disabled
		-Dinstalled_tests=false
		-Dsysprof=disabled
		-Dpkcs11_tests=disabled
	)
	meson_src_configure
}
