# Copyright 2022-2026 Gentoo Authors
# Distributed under the terms of the GNU General Public License v2

EAPI=7

# NickelOS: from GURU's gbinder-1.1.52 (EAPI 9 -> 7): Waydroid's binder
# client (through dev-python/gbinder). Without the binder-* debugging tools.

inherit toolchain-funcs

MY_PN="lib${PN}"
MY_P="${MY_PN}-${PV}"
DESCRIPTION="GLib-style interface to binder"
HOMEPAGE="https://github.com/mer-hybris/libgbinder"
SRC_URI="https://github.com/mer-hybris/libgbinder/archive/${PV}.tar.gz -> ${P}.tar.gz"
S="${WORKDIR}/${MY_P}"

LICENSE="BSD"
SLOT="0/1.1.52"
KEYWORDS="*"

DEPEND="
	dev-libs/libglibutil
	dev-libs/glib:2
"
RDEPEND="${DEPEND}"
BDEPEND="virtual/pkgconfig"

PATCHES=(
	"${FILESDIR}"/gbinder-1.1.52-respect-env.patch
)

src_configure() {
	tc-export AR CC PKG_CONFIG RANLIB STRIP
	export KEEP_SYMBOLS=1
	export LIBDIR="${EPREFIX}"/usr/$(get_libdir)
}

src_compile() {
	emake release pkgconfig
}

src_install() {
	emake DESTDIR="${D}" INSTALL_INCLUDE_DIR="${ED}"/usr/include/gbinder install-dev
}
