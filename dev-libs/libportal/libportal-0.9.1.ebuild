# Copyright 1999-2026 Gentoo Authors
# Distributed under the terms of the GNU General Public License v2

EAPI=7

# NickelOS: from Gentoo libportal-0.9.1 (EAPI 8 -> 7): the library and its
# GTK 4 backend (Valent asks the portals for files and input through them);
# no GTK 3 or Qt backends, introspection, vapi, docs or tests.

inherit meson

DESCRIPTION="Flatpak portal library"
HOMEPAGE="https://github.com/flatpak/libportal"
SRC_URI="https://github.com/flatpak/libportal/releases/download/${PV}/${P}.tar.xz"

LICENSE="LGPL-3"
SLOT="0/1-1-1-1" # soname of libportal{,-gtk3,-gtk4,-qt6}.so
KEYWORDS="*"
RESTRICT="test"

RDEPEND="
	>=dev-libs/glib-2.72:2
	>=gui-libs/gtk-4.12:4
"
DEPEND="${RDEPEND}"
BDEPEND="
	dev-util/glib-utils
	virtual/pkgconfig
"

src_configure() {
	local emesonargs=(
		-Dbackend-gtk3=disabled
		-Dbackend-gtk4=enabled
		-Dbackend-qt5=disabled
		-Dbackend-qt6=disabled
		-Dportal-tests=false
		-Dintrospection=false
		-Dvapi=false
		-Ddocs=false
		-Dtests=false
	)
	meson_src_configure
}
