# Copyright 1999-2026 Gentoo Authors
# Distributed under the terms of the GNU General Public License v2

EAPI=7

# NickelOS: ported from Gentoo gsettings-desktop-schemas-49.1 (EAPI 8 -> 7): KEYWORDS="*", introspection off by default (no gobject-introspection floor).

inherit gnome.org gnome2-utils meson xdg

DESCRIPTION="Collection of GSettings schemas for GNOME desktop"
HOMEPAGE="https://gitlab.gnome.org/GNOME/gsettings-desktop-schemas"

LICENSE="LGPL-2.1+"
SLOT="0"
KEYWORDS="*"
IUSE="introspection"

BDEPEND="
	introspection? ( dev-libs/gobject-introspection:= )
	dev-util/glib-utils
	>=sys-devel/gettext-0.19.8
	virtual/pkgconfig
"

PATCHES=(
	# Use generic sans and monospace aliases
	"${FILESDIR}"/49.0-default-fonts.patch
)

src_configure() {
	local emesonargs=(
		$(meson_use introspection)
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
