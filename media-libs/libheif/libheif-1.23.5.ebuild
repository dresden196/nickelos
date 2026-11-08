# Copyright 1999-2026 Gentoo Authors
# Distributed under the terms of the GNU General Public License v2

EAPI=7

# NickelOS: from Gentoo libheif-1.23.5 (EAPI 8 -> 7, cmake instead of
# cmake-multilib: R156's multilib-minimal stops at EAPI 7; 64-bit only).
# HEIC/HEIF photos (what phones take) for picture previews: heif-thumbnailer
# and its heif.thumbnailer, which nickel-appd's Thumbnail runs (Chrome can't
# decode HEIF). Decoding H.265 through libde265 only: no encoders, no AV1
# (Chrome shows AVIF itself), no plugins, no gdk-pixbuf loader. The other
# example tools (heif-dec, heif-enc, heif-info) come with the thumbnailer.

inherit cmake

DESCRIPTION="ISO/IEC 23008-12:2017 HEIF file format decoder and encoder"
HOMEPAGE="https://github.com/strukturag/libheif"
SRC_URI="https://github.com/strukturag/libheif/releases/download/v${PV}/${P}.tar.gz"

LICENSE="GPL-3 MIT"
SLOT="0/$(ver_cut 1-2)"
KEYWORDS="*"

RDEPEND="
	media-libs/libde265
	media-libs/libjpeg-turbo:=
	media-libs/libpng:=
"
DEPEND="${RDEPEND}"
BDEPEND="virtual/pkgconfig"

src_configure() {
	local mycmakeargs=(
		-DBUILD_DOCUMENTATION=OFF
		-DBUILD_TESTING=OFF
		-DENABLE_PLUGIN_LOADING=OFF
		-DWITH_LIBDE265=ON
		-DWITH_AOM_DECODER=OFF
		-DWITH_AOM_ENCODER=OFF
		-DWITH_DAV1D=OFF
		-DWITH_EXAMPLES=ON
		-DWITH_EXAMPLE_HEIF_THUMB=ON
		-DWITH_EXAMPLE_HEIF_VIEW=OFF
		-DWITH_FFMPEG_DECODER=OFF
		-DWITH_GDK_PIXBUF=OFF
		-DWITH_OpenH264_DECODER=OFF
		-DWITH_OpenH264_ENCODER=OFF
		-DWITH_RAV1E=OFF
		-DWITH_SvtEnc=OFF
		-DWITH_LIBSHARPYUV=OFF
		-DWITH_WEBCODECS=no
		-DWITH_X264=OFF
		-DWITH_X265=OFF
		-DWITH_KVAZAAR=OFF
		-DWITH_JPEG_DECODER=OFF
		-DWITH_JPEG_ENCODER=OFF
		-DWITH_OpenJPEG_DECODER=OFF
		-DWITH_OpenJPEG_ENCODER=OFF
		-DWITH_OPENJPH_ENCODER=OFF
		-DWITH_UNCOMPRESSED_CODEC=ON
		-DWITH_LIBPNG_INTERNAL=no
		-DCMAKE_DISABLE_FIND_PACKAGE_TIFF=ON
		-DCMAKE_DISABLE_FIND_PACKAGE_WEBP=ON
	)
	cmake_src_configure
}
