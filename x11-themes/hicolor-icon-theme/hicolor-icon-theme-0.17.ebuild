# Copyright 1999-2025 Gentoo Authors
# Distributed under the terms of the GNU General Public License v2

EAPI=7

# NickelOS: ported from Gentoo hicolor-icon-theme-0.17: KEYWORDS="*". The icon cache update is a no-op at build time (the SDK has no gtk-update-icon-cache); GTK scans the theme directories without a cache.

inherit xdg-utils

DESCRIPTION="Fallback theme for the freedesktop icon theme specification"
HOMEPAGE="https://freedesktop.org/wiki/Software/icon-theme"
SRC_URI="https://icon-theme.freedesktop.org/releases/${P}.tar.xz"

LICENSE="GPL-2"
SLOT="0"
KEYWORDS="*"

BDEPEND="app-arch/xz-utils"

RESTRICT="binchecks strip"

pkg_postinst() { xdg_icon_cache_update; }
pkg_postrm() { xdg_icon_cache_update; }
