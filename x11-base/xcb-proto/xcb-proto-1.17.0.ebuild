# Copyright 1999-2025 Gentoo Authors
# Distributed under the terms of the GNU General Public License v2

# NickelOS: 1.17 has DRI3 1.4 and Present 1.4 (DRM syncobjs), which libxcb
# 1.17 builds its bindings from; NVIDIA's GL and Vulkan look those bindings up
# to hand Xwayland explicit sync points. portage-stable has 1.16.

EAPI=7

PYTHON_COMPAT=( python3_{8..12} )
XORG_TARBALL_SUFFIX="xz"
XORG_MODULE=proto/

inherit python-r1 xorg-3

DESCRIPTION="X C-language Bindings protocol headers"
HOMEPAGE="https://xcb.freedesktop.org/ https://gitlab.freedesktop.org/xorg/proto/xcbproto"
EGIT_REPO_URI="https://gitlab.freedesktop.org/xorg/proto/xcbproto.git"

KEYWORDS="*"
REQUIRED_USE="${PYTHON_REQUIRED_USE}"

DEPEND=""
RDEPEND="
	${PYTHON_DEPS}
"
BDEPEND="
	${PYTHON_DEPS}
	dev-libs/libxml2
"

ECONF_SOURCE="${S}"

src_configure() {
	# Don't use Python to find sitedir here.
	PYTHON=true default
}

src_compile() {
	:
}

xcbgen_install() {
	# Use eclass to find sitedir instead.
	emake -C xcbgen install DESTDIR="${D}" pythondir="$(python_get_sitedir)"
	python_optimize
}

src_install() {
	# Restrict SUBDIRS to prevent xcbgen with empty sitedir.
	emake install DESTDIR="${D}" SUBDIRS=src
	python_foreach_impl xcbgen_install
}
