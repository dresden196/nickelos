# Copyright 1999-2026 Gentoo Authors
# Distributed under the terms of the GNU General Public License v2

EAPI=7

# NickelOS: ported from Gentoo libXinerama-1.1.6 (EAPI 8 -> 7): KEYWORDS="*", built with xorg-3 (the tarball ships autotools next to meson; ChromiumOS's xorg-meson is EAPI 8 only) and XORG_TARBALL_SUFFIX=xz (ChromiumOS xorg-3 defaults to bz2). GTK 4's X11 backend requires it.

XORG_DOC=doc
XORG_MULTILIB=yes
XORG_TARBALL_SUFFIX="xz"
inherit xorg-3

DESCRIPTION="X.Org Xinerama library"
KEYWORDS="*"

RDEPEND="
	>=x11-libs/libX11-1.6.2[${MULTILIB_USEDEP}]
	>=x11-libs/libXext-1.3.2[${MULTILIB_USEDEP}]
"
DEPEND="${RDEPEND}
	x11-base/xorg-proto
"
