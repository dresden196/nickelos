# Copyright 2026 The NickelOS Authors
# Distributed under the terms of the GNU General Public License v2

# VA-API on NVIDIA's NVDEC (through NVIDIA's CUDA and NVDEC libraries, from
# nvidia-open), so Chrome decodes video on NVIDIA GPUs as it does on Intel
# and AMD ones. Changed only for VP9: its frame header parser comes from
# GStreamer, which the system does not have, so the parser's four files are
# taken from gst-plugins-bad's source and built in, with a small stand-in
# for the bits of GStreamer they use (files/nickel-gst, logging compiled
# out). And NVDEC gets the loop filter deltas and segment features in
# effect, which the parser carries over between frames, not only what each
# frame header changes (upstream's bug: zeros on most inter frames, so the
# output differed slightly from libvpx's at edges).

EAPI=7

inherit meson

GST_P="gst-plugins-bad-1.24.12"

DESCRIPTION="A VA-API implementation using NVIDIA's NVDEC"
HOMEPAGE="https://github.com/elFarto/nvidia-vaapi-driver"
SRC_URI="https://github.com/elFarto/nvidia-vaapi-driver/archive/refs/tags/v${PV}.tar.gz -> ${P}.tar.gz
	https://gstreamer.freedesktop.org/src/gst-plugins-bad/${GST_P}.tar.xz"

# The built-in VP9 parser is LGPL-2.1+.
LICENSE="MIT LGPL-2.1+"
SLOT="0"
KEYWORDS="*"

DEPEND="
	dev-libs/glib:2
	media-libs/libglvnd
	media-libs/nv-codec-headers
	x11-libs/libdrm
	x11-libs/libva
"
RDEPEND="
	dev-libs/glib:2
	x11-drivers/nvidia-open
	x11-libs/libdrm
	x11-libs/libva
"
BDEPEND="virtual/pkgconfig"

PATCHES=( "${FILESDIR}/${P}-nickel-vp9.patch" )

src_unpack() {
	unpack "${P}.tar.gz"
	local parsers="${GST_P}/gst-libs/gst/codecparsers"
	tar -xJf "${DISTDIR}/${GST_P}.tar.xz" \
		"${parsers}"/{gstvp9parser.c,gstvp9parser.h,vp9utils.c,vp9utils.h} || die
	cp -r "${FILESDIR}/nickel-gst" "${S}/" || die
	cp "${parsers}"/{gstvp9parser.c,gstvp9parser.h,vp9utils.c,vp9utils.h} \
		"${S}/nickel-gst/" || die
	cp "${parsers}/gstvp9parser.h" "${S}/nickel-gst/gst/codecparsers/" || die
}

# Where Gentoo's libva, the one in the image, looks for drivers, as Gentoo's
# own nvidia-vaapi-driver installs it. meson takes the directory from the
# build's libva, ChromiumOS's, which looked in /usr/lib64/va/drivers: since
# the switch to Gentoo's libva Chrome found no driver on NVIDIA GPUs and
# decoded video on the CPU (16852.17.9). The Intel drivers move the same way
# (profiles/base/bashrc/nickel-va-drivers.bashrc).
src_install() {
	meson_src_install
	mkdir -p "${ED}/usr/lib64/dri" || die
	mv "${ED}"/usr/lib64/va/drivers/nvidia_drv_video.so "${ED}/usr/lib64/dri/" || die
	rmdir "${ED}/usr/lib64/va/drivers" "${ED}/usr/lib64/va" || die
}
