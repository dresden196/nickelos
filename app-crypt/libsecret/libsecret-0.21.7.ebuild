# Copyright 1999-2026 Gentoo Authors
# Distributed under the terms of the GNU General Public License v2

EAPI=7

# NickelOS: from Gentoo libsecret-0.21.7 (EAPI 8 -> 7), single ABI, for
# evolution-data-server (Valent's contacts): the library with GnuTLS for its
# crypto; no introspection, vapi, docs, man page, PAM, TPM or tests. No
# secret service ships with NickelOS yet (Gentoo's PDEPEND
# virtual/secret-service is left out).

inherit gnome.org meson

DESCRIPTION="GObject library for accessing the freedesktop.org Secret Service API"
HOMEPAGE="https://gnome.pages.gitlab.gnome.org/libsecret"

LICENSE="LGPL-2.1+"
SLOT="0"
KEYWORDS="*"
RESTRICT="test"

DEPEND="
	>=dev-libs/glib-2.44:2
	>=net-libs/gnutls-3.8.2:0=
"
RDEPEND="${DEPEND}"
BDEPEND="
	dev-util/glib-utils
	>=sys-devel/gettext-0.19.8
	virtual/pkgconfig
"

src_configure() {
	local emesonargs=(
		-Dcrypto=gnutls
		-Dintrospection=false
		-Dvapi=false
		-Dgtk_doc=false
		-Dmanpage=false
		-Dbash_completion=disabled
		-Dtpm2=false
		-Dpam=false
	)
	meson_src_configure
}
