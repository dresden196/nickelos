# Copyright 1999-2026 Gentoo Authors
# Distributed under the terms of the GNU General Public License v2

EAPI=7

# NickelOS: from Gentoo libde265-1.1.3 (EAPI 8 -> 7, cmake instead of
# cmake-multilib: R156's multilib-minimal stops at EAPI 7; 64-bit only). The
# H.265 (HEVC) decoder libheif uses for HEIC photos (picture previews,
# nickel-appd's Thumbnail). The library only: no dec265 player, no SDL.

inherit cmake

DESCRIPTION="Open h.265 video codec implementation"
HOMEPAGE="https://github.com/strukturag/libde265"
SRC_URI="https://github.com/strukturag/libde265/releases/download/v${PV}/${P}.tar.gz"

LICENSE="LGPL-3"
SLOT="0"
KEYWORDS="*"
IUSE="cpu_flags_x86_avx2 cpu_flags_x86_sse"

src_configure() {
	local mycmakeargs=(
		-DDE265_LOG_LEVEL=error
		-DENABLE_AVX2=$(usex cpu_flags_x86_avx2)
		-DENABLE_AVX512=OFF
		-DENABLE_DECODER=OFF
		-DENABLE_ENCODER=OFF
		-DENABLE_SDL=OFF
		-DENABLE_SHERLOCK265=OFF
		-DENABLE_SIMD=$(usex cpu_flags_x86_sse)
	)
	cmake_src_configure
}
