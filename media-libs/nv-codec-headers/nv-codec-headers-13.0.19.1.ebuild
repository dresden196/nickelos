# Copyright 2026 The NickelOS Authors
# Distributed under the terms of the GNU General Public License v2

# FFmpeg's headers for NVIDIA's video APIs (NVDEC, NVENC; ffnvcodec): no
# code, the libraries are loaded at run time from NVIDIA's driver. Built
# against by nvidia-vaapi-driver.

EAPI=7

inherit multilib

DESCRIPTION="FFmpeg version of headers required to interface with NVIDIA's codec APIs"
HOMEPAGE="https://github.com/FFmpeg/nv-codec-headers"
SRC_URI="https://github.com/FFmpeg/nv-codec-headers/releases/download/n${PV}/${P}.tar.gz"

LICENSE="MIT"
SLOT="0"
KEYWORDS="*"

# ChromeOS's license check needs MIT's copyright notice in a file of its own;
# the source has it only at the top of each header.
src_prepare() {
	default
	sed -n '2,/^ \*\//{/^ \*\//d;s/^ \*\{0,1\} \{0,1\}//;p}' \
		include/ffnvcodec/dynlink_cuda.h > LICENSE || die
	grep -q "Permission is hereby granted" LICENSE || die "no MIT notice"
}

src_compile() {
	emake PREFIX="${EPREFIX}/usr" LIBDIR="$(get_libdir)"
}

src_install() {
	emake PREFIX="${EPREFIX}/usr" LIBDIR="$(get_libdir)" DESTDIR="${D}" install
}
