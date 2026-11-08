# Copyright 2021-2025 Gentoo Authors
# Distributed under the terms of the GNU General Public License v2

EAPI=7

# NickelOS: ported from Gentoo libxcvt-0.1.3 (EAPI 8 -> 7): KEYWORDS="*"; plain meson with an explicit SRC_URI instead of xorg-3 (ChromiumOS's xorg-3 defaults to .tar.bz2 and Gentoo only used it for SRC_URI/HOMEPAGE). For xwayland.

inherit meson

DESCRIPTION="X.Org xcvt library and cvt program"
HOMEPAGE="https://gitlab.freedesktop.org/xorg/lib/libxcvt"
SRC_URI="https://www.x.org/releases/individual/lib/${P}.tar.xz"
KEYWORDS="*"

LICENSE="MIT"
SLOT="0"
