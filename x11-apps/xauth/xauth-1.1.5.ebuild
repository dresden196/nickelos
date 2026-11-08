# Copyright 1999-2026 Gentoo Authors
# Distributed under the terms of the GNU General Public License v2

EAPI=7

# NickelOS: ported from Gentoo xauth-1.1.5 (EAPI 8 -> 7): KEYWORDS="*", XORG_TARBALL_SUFFIX=xz (ChromiumOS xorg-3 defaults to bz2).

XORG_TARBALL_SUFFIX="xz"
inherit xorg-3

DESCRIPTION="X authority file utility"
KEYWORDS="*"

RDEPEND="
	x11-libs/libX11
	x11-libs/libXau
	x11-libs/libXext
	x11-libs/libXmu"
DEPEND="${RDEPEND}
	x11-base/xorg-proto"

XORG_CONFIGURE_OPTIONS=(
	--enable-ipv6
)
