# Copyright 1999-2026 Gentoo Authors
# Distributed under the terms of the GNU General Public License v2

# NickelOS: ported from Gentoo wireplumber-0.5.18 (EAPI 8 -> 7): the session
# manager of the session's PipeWire (nickel-session): it links Linux apps'
# streams to the sink and source that carry sound into CRAS. ChromiumOS has
# no Lua, so WirePlumber's own Lua (its meson subproject, linked in
# statically) is built from the source and wrapdb patch named here, which
# src_unpack puts in meson's package cache: nothing is downloaded. No
# elogind/systemd, no docs, introspection or tests.

EAPI=7

inherit meson

LUA_V="5.5.0"
LUA_WRAP="lua_${LUA_V}-1"

DESCRIPTION="Session / policy manager implementation for PipeWire"
HOMEPAGE="https://gitlab.freedesktop.org/pipewire/wireplumber"
SRC_URI="https://gitlab.freedesktop.org/pipewire/${PN}/-/archive/${PV}/${P}.tar.bz2
	https://www.lua.org/ftp/lua-${LUA_V}.tar.gz
	https://wrapdb.mesonbuild.com/v2/${LUA_WRAP}/get_patch -> ${LUA_WRAP}_patch.zip"

# The built-in Lua is MIT too.
LICENSE="MIT"
SLOT="0/0.5"
KEYWORDS="*"

BDEPEND="
	dev-libs/glib
	sys-devel/gettext
	virtual/pkgconfig
"
DEPEND="
	>=dev-libs/glib-2.68:2
	>=media-video/pipewire-1.6.8-r3:=
	virtual/libintl
"
RDEPEND="${DEPEND}"

src_unpack() {
	unpack "${P}.tar.bz2"
	mkdir -p "${S}/subprojects/packagecache" || die
	cp "${DISTDIR}/lua-${LUA_V}.tar.gz" "${DISTDIR}/${LUA_WRAP}_patch.zip" \
		"${S}/subprojects/packagecache/" || die
}

src_configure() {
	local emesonargs=(
		-Dsystem-lua=false
		-Dintrospection=disabled
		-Ddoc=disabled
		-Dtests=false
		-Ddbus-tests=false
		-Delogind=disabled
		-Dsystemd=disabled
		-Dsystemd-system-service=false
		-Dsystemd-user-service=false
	)
	meson_src_configure
}
