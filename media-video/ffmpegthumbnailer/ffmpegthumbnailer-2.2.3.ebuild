# Copyright 1999-2025 Gentoo Authors
# Distributed under the terms of the GNU General Public License v2

EAPI=7

# NickelOS: from Gentoo ffmpegthumbnailer-2.2.3 (EAPI 8 -> 7). Video previews
# for what Chrome can't decode itself (H.265/HEVC from phones, and more):
# nickel-appd's Thumbnail runs it through its ffmpegthumbnailer.thumbnailer,
# sandboxed.
#
# With its own FFmpeg, built here and linked in statically: ChromiumOS's
# media-video/ffmpeg installs static libraries under /usr/local for its test
# tools only, and changing it would change them. This one decodes video and
# nothing else: the usual video formats' demuxers and decoders, the filters
# ffmpegthumbnailer uses, the file protocol; no encoders, no network, no
# hardware, no external libraries but zlib. AV1 is left out: Chrome shows
# AV1 videos itself. Only the ffmpegthumbnailer program and its .thumbnailer
# file are installed (no library, headers or GIO part).

inherit cmake toolchain-funcs

FFMPEG_PV="7.1.5"

DESCRIPTION="Lightweight video thumbnailer that can be used by file managers"
HOMEPAGE="https://github.com/dirkvdb/ffmpegthumbnailer/ https://ffmpeg.org/"
SRC_URI="
	https://github.com/dirkvdb/ffmpegthumbnailer/archive/refs/tags/${PV}.tar.gz -> ${P}.tar.gz
	https://ffmpeg.org/releases/ffmpeg-${FFMPEG_PV}.tar.xz
"

LICENSE="GPL-2+ LGPL-2.1+"
SLOT="0"
KEYWORDS="*"

RDEPEND="
	media-libs/libjpeg-turbo:=
	media-libs/libpng:=
	sys-libs/zlib:=
"
DEPEND="${RDEPEND}"
BDEPEND="
	dev-lang/nasm
	virtual/pkgconfig
"

FFMPEG_S="${WORKDIR}/ffmpeg-${FFMPEG_PV}"
FFMPEG_D="${WORKDIR}/ffmpeg-install"

ffmpeg_configure() {
	local conf=(
		--prefix="${FFMPEG_D}"
		--enable-cross-compile
		--arch="$(tc-arch-kernel)"
		--target-os=linux
		--cross-prefix="${CHOST}-"
		--cc="$(tc-getCC)"
		--cxx="$(tc-getCXX)"
		--ar="$(tc-getAR)"
		--nm="$(tc-getNM)"
		--ranlib="$(tc-getRANLIB)"
		--strip="$(tc-getSTRIP)"
		--host-cc="$(tc-getBUILD_CC)"
		--optflags="${CFLAGS}"
		--enable-static
		--disable-shared
		--enable-pic
		--disable-programs
		--disable-doc
		--disable-debug
		--disable-network
		--disable-autodetect
		--disable-everything
		--disable-avdevice
		--disable-swresample
		--disable-postproc
		--enable-zlib
		--enable-protocol=file
		# MP4/MOV/M4V/3GP, MKV/WebM, AVI, MPEG-TS (.ts, .m2ts, .mts), MPEG-PS
		# (.mpg, .vob), FLV, Ogg, ASF (.wmv), raw streams.
		--enable-demuxer=mov,matroska,avi,mpegts,mpegps,flv,ogg,asf,h264,hevc,m4v,mpegvideo
		--enable-decoder=h264,hevc,vp8,vp9,mpeg4,mpeg1video,mpeg2video,h263,msmpeg4v1,msmpeg4v2,msmpeg4v3,wmv1,wmv2,wmv3,vc1,theora,prores,dnxhd,mjpeg,flv
		--enable-parser=h264,hevc,vp8,vp9,mpeg4video,mpegvideo,vc1,mjpeg,h263
		--enable-filter=buffer,buffersink,format,rotate,scale,transpose,yadif
	)
	cd "${FFMPEG_S}" || die
	echo ./configure "${conf[@]}"
	./configure "${conf[@]}" || die "FFmpeg's configure failed"
}

src_configure() {
	ffmpeg_configure
}

src_compile() {
	emake -C "${FFMPEG_S}"
	emake -C "${FFMPEG_S}" install

	# FFmpeg's .pc files with the build host's pkg-config (the cross one
	# would put the board's sysroot in front of the work dir), and its
	# archives by path (find_library would look for them under the sysroot).
	# JPEG and PNG come from the board through CMake's own modules. The
	# archives depend on each other: one group.
	local lib="${FFMPEG_D}/lib" l
	local mycmakeargs=(
		-DPKG_CONFIG_EXECUTABLE="${BROOT}/usr/bin/pkg-config"
		-DCMAKE_CXX_STANDARD_LIBRARIES="-Wl,--start-group ${lib}/libavfilter.a ${lib}/libswscale.a ${lib}/libavformat.a ${lib}/libavcodec.a ${lib}/libavutil.a -Wl,--end-group -lz -lm -lpthread"
		-DCMAKE_DISABLE_FIND_PACKAGE_JPEG=OFF
		-DCMAKE_DISABLE_FIND_PACKAGE_PNG=OFF
		-DENABLE_SHARED=OFF
		-DENABLE_STATIC=ON
		-DENABLE_GIO=OFF
		-DENABLE_TESTS=OFF
		-DENABLE_THUMBNAILER=ON
	)
	for l in avcodec avformat avfilter avutil; do
		mycmakeargs+=( -D${l^^}_LIBRARY_PATH="${lib}/lib${l}.a" )
	done
	local -x PKG_CONFIG_PATH="${lib}/pkgconfig" PKG_CONFIG_LIBDIR="${lib}/pkgconfig"
	local -x PKG_CONFIG_SYSROOT_DIR=
	cmake_src_configure
	cmake_src_compile
}

src_install() {
	cmake_src_install
	# The program and its .thumbnailer file only.
	rm -rf "${ED}"/usr/include "${ED}"/usr/$(get_libdir) || die
	# A plain frame, as Chrome's own video previews are: no film strip (-f).
	sed -i -e 's/ -f\b//' "${ED}"/usr/share/thumbnailers/ffmpegthumbnailer.thumbnailer || die
}
