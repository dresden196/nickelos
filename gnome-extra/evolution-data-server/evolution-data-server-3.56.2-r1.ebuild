# Copyright 1999-2026 Gentoo Authors
# Distributed under the terms of the GNU General Public License v2

EAPI=7

# NickelOS: from Gentoo evolution-data-server-3.56.2-r1 (EAPI 8 -> 7), for
# Valent (net-misc/valent), whose contacts are vCards (libebook-contacts):
# EDS has to be built whole for that library, so it is built bare: no GTK
# (3 or 4) or OAuth web views, GNOME Online Accounts, weather, Berkeley DB,
# Kerberos, LDAP, libphonenumber, Canberra, introspection, vala, docs,
# examples, systemd units or tests. S/MIME with NSS, as Gentoo builds it.

inherit cmake gnome.org gnome2-utils

DESCRIPTION="Evolution groupware backend"
HOMEPAGE="https://gitlab.gnome.org/GNOME/evolution-data-server"

# Note: explicitly "|| ( LGPL-2 LGPL-3 )", not "LGPL-2+".
LICENSE="|| ( LGPL-2 LGPL-3 ) BSD Sleepycat"
SLOT="0/64-11-21-4-3-27-2-27-4-0" # subslot = libcamel-1.2/libebackend-1.2/libebook-1.2/libebook-contacts-1.2/libecal-2.0/libedata-book-1.2/libedata-cal-2.0/libedataserver-1.2/libedataserverui-1.2/libedataserverui4-1.0 soname version
KEYWORDS="*"
RESTRICT="test"

RDEPEND="
	>=app-crypt/libsecret-0.5
	>=dev-db/sqlite-3.7.17:3
	>=dev-libs/glib-2.70:2
	>=dev-libs/libical-3.0.8:=
	>=dev-libs/libxml2-2:=
	>=dev-libs/nspr-4.4
	>=dev-libs/nss-3.9
	>=net-libs/libsoup-3.1.1:3.0
	>=dev-libs/json-glib-1.0.4
	dev-libs/nickel-icu:=
	sys-libs/zlib:=
"
DEPEND="${RDEPEND}"
BDEPEND="
	dev-util/gdbus-codegen
	dev-util/glib-utils
	dev-util/gperf
	>=dev-util/intltool-0.35.5
	>=sys-devel/gettext-0.18.3
	virtual/pkgconfig
"

src_configure() {
	# Its configure runs test programs: iconv's quirks (written to a header
	# camel uses), strftime's %l and %k, the OAuth client ids. The board is
	# x86_64 like the build machine, so run them with the board's own
	# loader and libraries.
	cat > "${T}"/run-on-board <<-EOF || die
	#!/bin/sh
	exec "${ESYSROOT}"/lib64/ld-linux-x86-64.so.2 \\
		--library-path "${ESYSROOT}/lib64:${ESYSROOT}/usr/lib64" "\$@"
	EOF
	chmod +x "${T}"/run-on-board || die

	local mycmakeargs=(
		-DCMAKE_CROSSCOMPILING_EMULATOR="${T}"/run-on-board
		-DSYSCONF_INSTALL_DIR="${EPREFIX}"/etc
		-DENABLE_GTK_DOC=OFF
		-DWITH_PRIVATE_DOCS=OFF
		-DENABLE_SCHEMAS_COMPILE=OFF
		-DENABLE_INTROSPECTION=OFF
		-DWITH_KRB5=OFF
		-DWITH_OPENLDAP=OFF
		-DWITH_PHONENUMBER=OFF
		-DENABLE_SMIME=ON
		-DENABLE_GTK=OFF
		-DENABLE_GTK4=OFF
		-DENABLE_OAUTH2_WEBKITGTK=OFF
		-DENABLE_OAUTH2_WEBKITGTK4=OFF
		-DENABLE_EXAMPLES=OFF
		-DENABLE_GOA=OFF
		-DWITH_LIBDB=OFF
		-DENABLE_IPV6=ON
		-DENABLE_WEATHER=OFF
		-DENABLE_CANBERRA=OFF
		-DENABLE_LARGEFILE=ON
		-DENABLE_VALA_BINDINGS=OFF
		-DENABLE_TESTS=OFF
		-DWITH_SYSTEMDUSERUNITDIR=OFF
	)
	cmake_src_configure
}

pkg_postinst() {
	gnome2_schemas_update
}

pkg_postrm() {
	gnome2_schemas_update
}
