# Copyright 1999-2025 Gentoo Authors
# Distributed under the terms of the GNU General Public License v2

EAPI=7

# NickelOS: ported from Gentoo appstream-glib-0.8.3 (EAPI 8 -> 7): KEYWORDS="*", introspection off by default, man pages (docbook) off. Built for the board only: the SDK cannot install packages from this overlay, and nothing in the Flatpak stack needs it at build time.

inherit xdg meson

DESCRIPTION="Provides GObjects and helper methods to read and write AppStream metadata"
HOMEPAGE="https://people.freedesktop.org/~hughsient/appstream-glib/ https://github.com/hughsie/appstream-glib"
SRC_URI="https://people.freedesktop.org/~hughsient/${PN}/releases/${P}.tar.xz"

LICENSE="LGPL-2.1+"
SLOT="0/8" # soname version
KEYWORDS="*"
IUSE="gtk-doc fonts introspection"

RDEPEND="
	>=dev-libs/glib-2.58.0:2
	sys-apps/util-linux
	app-arch/libarchive:=
	>=net-misc/curl-7.56.0
	>=dev-libs/json-glib-1.1.2
	>=x11-libs/gdk-pixbuf-2.31.5:2[introspection?]

	fonts? (
		x11-libs/gtk+:3
		>=media-libs/freetype-2.4:2
	)
	>=media-libs/fontconfig-2.11:1.0
	dev-libs/libyaml
	x11-libs/pango
	introspection? ( dev-libs/gobject-introspection:= )
"
DEPEND="${RDEPEND}"
# libxml2 required for glib-compile-resources
BDEPEND="
	dev-util/gperf

	dev-libs/libxml2:2
	gtk-doc? (
		>=dev-util/gtk-doc-1.9
		app-text/docbook-xml-dtd:4.3
	)
	>=sys-devel/gettext-0.19.8
"

src_configure() {
	local emesonargs=(
		-Ddep11=true
		-Dbuilder=true
		-Drpm=false
		-Dalpm=false
		$(meson_use fonts)
		-Dman=false
		$(meson_use gtk-doc)
		$(meson_use introspection)
	)
	meson_src_configure
}
