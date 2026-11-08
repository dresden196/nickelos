# Copyright 1999-2026 Gentoo Authors
# Distributed under the terms of the GNU General Public License v2

EAPI=7

# NickelOS: ported from Gentoo libXcomposite-0.4.7 (EAPI 8 -> 7): KEYWORDS="*", built with xorg-3 (the tarball ships autotools next to meson; ChromiumOS's xorg-meson is EAPI 8 only) and XORG_TARBALL_SUFFIX=xz (ChromiumOS xorg-3 defaults to bz2).

XORG_DOC=doc
XORG_MULTILIB=yes
XORG_TARBALL_SUFFIX="xz"
inherit xorg-3

DESCRIPTION="X.Org Xcomposite library"
KEYWORDS="*"

RDEPEND=">=x11-libs/libX11-1.6.2[${MULTILIB_USEDEP}]"
DEPEND="${RDEPEND}
	>=x11-libs/libXfixes-5.0.1[${MULTILIB_USEDEP}]
	x11-base/xorg-proto
"

src_configure() {
	local XORG_CONFIGURE_OPTIONS=(
		$(use_with doc xmlto)
	)
	xorg-3_src_configure
}
