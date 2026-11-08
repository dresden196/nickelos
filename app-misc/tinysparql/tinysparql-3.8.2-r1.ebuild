# Copyright 1999-2026 Gentoo Authors
# Distributed under the terms of the GNU General Public License v2

EAPI=7

# NickelOS: from Gentoo tinysparql-3.8.2 (EAPI 8 -> 7), for Valent
# (net-misc/valent), which keeps contacts and messages in it: the library
# (tinysparql-3.0, with tracker-sparql-3.0 for older users) and its tools,
# ICU for Unicode; no stemmer, Avahi, introspection, vapi, docs, man pages,
# systemd units, bash completion or tests.

PYTHON_COMPAT=( python3_{8..14} )
inherit gnome.org meson python-any-r1

DESCRIPTION="A low-footprint RDF triple store with SPARQL 1.1 interface"
HOMEPAGE="https://tinysparql.org/"

LICENSE="GPL-2+ LGPL-2.1+"
SLOT="3/0" # libtracker-sparql-3.0 soname version
KEYWORDS="*"
RESTRICT="test"

RDEPEND="
	>=dev-libs/glib-2.52:2
	dev-libs/nickel-icu:=
	>=dev-libs/json-glib-1.4:=
	>=dev-libs/libxml2-2.6:2
	>=dev-db/sqlite-3.20.0:3
	net-libs/libsoup:3.0
	sys-apps/dbus
"
DEPEND="${RDEPEND}"
BDEPEND="
	${PYTHON_DEPS}
	dev-util/glib-utils
	>=sys-devel/gettext-0.19.8
	virtual/pkgconfig
"

# Its configure runs two test programs, which a cross build can't: the
# answers for the board (SQLite has FTS5, glibc's 4-digit year is %4Y).
PATCHES=(
	"${FILESDIR}"/${P}-cross-build.patch
)

src_configure() {
	local emesonargs=(
		-Ddocs=false
		-Dman=false
		-Dstemmer=disabled
		-Dunicode_support=icu
		-Davahi=disabled
		-Dbash_completion=false
		-Dsystemd_user_services=false
		-Dtests=false
		-Dintrospection=disabled
		-Dvapi=disabled
	)
	meson_src_configure
}
