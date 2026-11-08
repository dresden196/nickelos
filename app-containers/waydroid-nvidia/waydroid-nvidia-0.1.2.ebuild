# Distributed under the terms of the GNU General Public License v2

EAPI=7

# NickelOS: Android in Waydroid on NVIDIA's own driver, from waydroid-nvidia
# (github.com/Shiro836/waydroid-nvidia). Android renders Vulkan through Mesa
# Venus; the calls go over a socket to a render server on the host, which runs
# them on NVIDIA's driver; buffers are NVIDIA's (block-linear dma-bufs), so
# Exo can show them.
#   - The host render server, built here from source: virglrenderer at the
#     commit waydroid-nvidia pins, with its patch series and its vtest
#     allocator (virgl_test_server --venus, virgl_render_server), in
#     /usr/lib/waydroid-nvidia, apart from ChromiumOS's own virglrenderer.
#   - Android's side, from the project's CI-built and attested release
#     tarballs of the same version (Venus, ANGLE, the gralloc backend,
#     Waydroid's hwcomposer, SurfaceFlinger; ANGLE needs a ~16 GB tree and
#     SurfaceFlinger a LineageOS one, which we don't build). Kept as the
#     tarballs in /usr/share/waydroid-nvidia: they are Android (bionic)
#     binaries, unpacked into Waydroid's directory by nickel-waydroid when
#     the GPU is NVIDIA.
# The host server and Android's side speak one protocol: keep them at the
# same release.

inherit flag-o-matic meson

VIRGL_COMMIT="dc35e4db03144f81637c5ad061f61d3334b078fe"
GUEST="waydroid-nvidia-guest-android-x86_64-v${PV}.tar.zst"
PREBUILTS="waydroid-nvidia-guest-prebuilts-v${PV}.tar.zst"

DESCRIPTION="Waydroid on NVIDIA's driver: the Venus render server and Android's side"
HOMEPAGE="https://github.com/Shiro836/waydroid-nvidia"
SRC_URI="
	https://gitlab.freedesktop.org/virgl/virglrenderer/-/archive/${VIRGL_COMMIT}/virglrenderer-${VIRGL_COMMIT}.tar.gz
	https://github.com/Shiro836/waydroid-nvidia/archive/v${PV}.tar.gz -> ${P}.tar.gz
	https://github.com/Shiro836/waydroid-nvidia/releases/download/v${PV}/${GUEST}
	https://github.com/Shiro836/waydroid-nvidia/releases/download/v${PV}/${PREBUILTS}
"
S="${WORKDIR}/virglrenderer-${VIRGL_COMMIT}"

LICENSE="MIT BSD Apache-2.0"
SLOT="0"
KEYWORDS="*"

RDEPEND="
	media-libs/libepoxy
	media-libs/vulkan-loader
	x11-libs/libdrm
	x11-libs/libX11
	app-containers/waydroid
"
DEPEND="${RDEPEND}
	dev-util/vulkan-headers
"
BDEPEND="virtual/pkgconfig"

P_DIR="/usr/lib/waydroid-nvidia"

src_unpack() {
	# The Android tarballs stay packed (installed as they are).
	unpack virglrenderer-${VIRGL_COMMIT}.tar.gz ${P}.tar.gz
}

src_prepare() {
	local wn="${WORKDIR}/${P}"
	eapply "${wn}"/patches/virglrenderer/000[1234]-*.patch
	cp "${wn}"/src/virglrenderer-vtest/vtest_gpu_alloc.{c,h} vtest/ || die
	default
}

src_configure() {
	# Its own libvirglrenderer, not ChromiumOS's (/usr/lib64).
	append-ldflags "-Wl,-rpath,${P_DIR}/$(get_libdir)"
	local emesonargs=(
		--prefix "${P_DIR}"
		-Dvenus=true
		-Drender-server-worker=auto
	)
	meson_src_configure
}

src_install() {
	meson_src_install
	# Headers and pkg-config: nothing builds against this copy.
	rm -rf "${ED}${P_DIR}"/include "${ED}${P_DIR}"/$(get_libdir)/pkgconfig || die
	insinto /usr/share/waydroid-nvidia
	doins "${DISTDIR}/${GUEST}" "${DISTDIR}/${PREBUILTS}"
	echo "${PV}" > "${T}"/version || die
	doins "${T}"/version
}
