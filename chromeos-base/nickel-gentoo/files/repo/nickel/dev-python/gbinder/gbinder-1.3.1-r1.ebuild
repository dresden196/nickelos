# Copyright 2022-2026 Gentoo Authors
# Distributed under the terms of the GNU General Public License v2

EAPI=8

# NickelOS: from GURU's gbinder-1.3.1 (EAPI 8 -> 7): the Python bindings
# Waydroid uses for binder (gbinder-python). setup.py runs the board's
# pkg-config ($PKG_CONFIG): cross builds refuse a bare pkg-config.

PYTHON_COMPAT=( python3_{11..14} )
DISTUTILS_USE_PEP517="setuptools"
DISTUTILS_EXT=1

inherit distutils-r1 toolchain-funcs

MY_PN="${PN}-python"
MY_P="${MY_PN}-${PV}"
DESCRIPTION="Python bindings for dev-libs/gbinder"
HOMEPAGE="https://github.com/waydroid/gbinder-python"
SRC_URI="https://github.com/waydroid/gbinder-python/archive/${PV}.tar.gz -> ${P}.gh.tar.gz"
S="${WORKDIR}/${MY_P}"

LICENSE="GPL-3"
SLOT="0"
KEYWORDS="*"

DEPEND="
	>=dev-libs/gbinder-1.1.40
	dev-libs/libglibutil
"
RDEPEND="${DEPEND}"
BDEPEND="
	virtual/pkgconfig
	dev-python/cython[${PYTHON_USEDEP}]
"

src_prepare() {
	sed -i -e 's/^import sys, subprocess$/import os, sys, subprocess/' \
		-e "s/'pkg-config --cflags --libs {}'/os.environ.get('PKG_CONFIG', 'pkg-config') + ' --cflags --libs {}'/" \
		setup.py || die
	grep -q "os.environ.get('PKG_CONFIG'" setup.py || die "setup.py: pkg-config call not found"
	distutils-r1_src_prepare
}

src_configure() {
	tc-export PKG_CONFIG
	distutils-r1_src_configure
}
