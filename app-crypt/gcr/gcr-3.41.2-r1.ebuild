# Copyright 1999-2026 Gentoo Authors
# Distributed under the terms of the GNU General Public License v2

EAPI=7

# NickelOS: from Gentoo gcr-3.41.2-r2 (EAPI 8 -> 7), for gnome-keyring (the
# Secret Service): the libraries and the GTK 3 prompter (gcr-prompter, which
# asks for a keyring's password should one ever need it). No introspection,
# vapi, docs, systemd or ssh-agent (gcr 4's).

inherit gnome.org gnome2-utils meson xdg

DESCRIPTION="Libraries for cryptographic UIs and accessing PKCS#11 modules"
HOMEPAGE="https://gitlab.gnome.org/GNOME/gcr"

LICENSE="GPL-2+ LGPL-2+"
SLOT="0/1" # subslot = suffix of libgcr-base-3 and co
KEYWORDS="*"
# Built with GTK (the prompter); the flag says so, as on Gentoo, for the
# packages that ask for gcr[gtk] (NickelOS: Gentoo's gnome-keyring).
IUSE="+gtk"
REQUIRED_USE="gtk"
RESTRICT="test"

DEPEND="
	>=dev-libs/glib-2.44.0:2
	>=dev-libs/libgcrypt-1.2.2:0=
	>=app-crypt/p11-kit-0.19.0
	>=app-crypt/libsecret-0.20
	>=x11-libs/gtk+-3.22:3
	>=sys-apps/dbus-1
"
RDEPEND="${DEPEND}"
BDEPEND="
	dev-libs/libxml2:2
	dev-util/glib-utils
	>=sys-devel/gettext-0.19.8
	virtual/pkgconfig
"

PATCHES=(
	"${FILESDIR}"/3.41.1-implicit-func-decl.patch
)

src_prepare() {
	default
	xdg_environment_reset
}

src_configure() {
	local emesonargs=(
		-Dintrospection=false
		$(meson_use gtk)
		-Dgtk_doc=false
		-Dgpg_path=/usr/bin/gpg
		-Dssh_agent=false
		-Dsystemd=disabled
	)
	meson_src_configure
}

pkg_postinst() {
	xdg_pkg_postinst
	gnome2_schemas_update
}

pkg_postrm() {
	xdg_pkg_postrm
	gnome2_schemas_update
}
