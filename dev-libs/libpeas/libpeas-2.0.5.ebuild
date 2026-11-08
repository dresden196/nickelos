# Copyright 1999-2026 Gentoo Authors
# Distributed under the terms of the GNU General Public License v2

EAPI=7

# NickelOS: from Gentoo libpeas-2.2.1 (EAPI 8 -> 7) at upstream 2.0.5, the
# newest series on GLib 2.76 (2.2 needs GLib 2.80's girepository-2.0). For
# Valent (net-misc/valent), whose plugins are C: no plugin loaders (Python,
# Lua, GJS), no introspection (ChromiumOS's gobject-introspection is 1.72),
# vapi or docs.

inherit gnome.org meson

DESCRIPTION="A GObject plugins library"
HOMEPAGE="https://gitlab.gnome.org/GNOME/libpeas"

LICENSE="LGPL-2.1+"
SLOT="2"
KEYWORDS="*"
RESTRICT="test"

RDEPEND=">=dev-libs/glib-2.74:2"
DEPEND="${RDEPEND}"
BDEPEND="
	dev-util/glib-utils
	>=sys-devel/gettext-0.19.8
	virtual/pkgconfig
"

src_configure() {
	local emesonargs=(
		-Dgjs=false
		-Dlua51=false
		-Dpython3=false
		-Dintrospection=false
		-Dvapi=false
		-Dgtk_doc=false
	)
	meson_src_configure
}
