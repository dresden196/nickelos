# Copyright 1999-2026 Gentoo Authors
# Distributed under the terms of the GNU General Public License v2

EAPI=7

# NickelOS: Mesa for Linux apps. ChromeOS's Mesa (media-libs/mesa-reven) is
# built for Chrome alone: no window systems (surfaceless only), no GBM, no
# Vulkan, so Linux apps rendered in software. Changing it is unsafe: with
# GBM, Mesa's EGL needs its own libgbm.so.1, the name of ChromeOS's minigbm,
# which Chrome already has loaded. So this is a second build of the same Mesa
# (mesa-reven's source, chromeos-reven branch, same commit) for Linux apps
# only, kept apart in /usr/lib64/nickel-mesa:
#   - EGL with the Wayland and X11 platforms and its own GBM; GLX; GLES;
#     Vulkan for Intel (anv, hasvk) and AMD (radv); the same Gallium drivers.
#   - Every library carries an RPATH to that directory, so its own libgbm and
#     libglapi are the ones loaded, never the system's.
#   - libglvnd picks it through its own EGL vendor file, 60_nickel_mesa.json,
#     kept out of libglvnd's directory (which every process reads, Chrome's
#     too): nickel-session points Linux apps at it
#     (__EGL_VENDOR_LIBRARY_FILENAMES); Chrome keeps mesa-reven. Vulkan
#     drivers register system-wide (the loader's icd.d), under names of
#     their own (nickel-mesa_*.json): Gentoo's Mesa has the standard ones
#     there, and Gentoo's apps need them. The loaders find each library by a
#     name of its own, a link in /usr/lib64 (see src_install), so Gentoo's
#     apps keep theirs (docs/gentoo-at-root.md).
#   - Headers, drirc and pkg-config files stay out of /usr. Xwayland builds
#     against this GBM (its glamor allocates and imports buffers through
#     it): gbm.pc and gbm.h are kept in the directory, apart from ChromeOS's
#     own gbm.h (minigbm's, which numbers the import types differently).

CROS_WORKON_COMMIT="bc8501b41626696b521a85bebb5db2f66a51dbc3"
CROS_WORKON_TREE="f587444ccb0eea49682fb4dac5a4631377893a56"
CROS_WORKON_PROJECT="chromiumos/third_party/mesa"
CROS_WORKON_EGIT_BRANCH="chromeos-reven"
CROS_WORKON_LOCALNAME="mesa-reven"

inherit flag-o-matic meson cros-toolchain-funcs cros-workon

DESCRIPTION="Mesa for NickelOS's Linux apps: Wayland/X11 EGL, GBM, GLX, Vulkan"
HOMEPAGE="https://mesa3d.org/"

LICENSE="MIT LGPL-3 SGI-B-2.0"
SLOT="0"
KEYWORDS="*"

COMMON_DEPEND="
	dev-libs/expat:=
	dev-libs/wayland
	media-libs/libglvnd
	sys-devel/llvm:=
	sys-libs/zlib:=
	virtual/libelf:=
	x11-libs/libX11
	x11-libs/libXext
	x11-libs/libXfixes
	x11-libs/libXrandr
	x11-libs/libXxf86vm
	x11-libs/libxcb
	x11-libs/libxshmfence
	>=x11-libs/libdrm-2.4.60:=
"
RDEPEND="${COMMON_DEPEND}
	media-libs/vulkan-loader
"
DEPEND="${COMMON_DEPEND}
	dev-libs/libxml2:=
	>=dev-libs/wayland-protocols-1.8:=
	x11-base/xorg-proto:=
"
BDEPEND="
	dev-util/wayland-scanner
	sys-devel/bison
	sys-devel/flex
	virtual/pkgconfig
"

LIBDIR_NICKEL="lib64/nickel-mesa"

src_configure() {
	tc-getPROG PKG_CONFIG pkg-config
	cros_optimize_package_for_speed
	# The libraries find each other in their own directory first.
	append-ldflags "-Wl,-rpath,${EPREFIX}/usr/${LIBDIR_NICKEL}" "-Wl,--disable-new-dtags"

	export LLVM_CONFIG=${SYSROOT}/usr/lib/llvm/bin/llvm-config-host

	local emesonargs=(
		--libdir="${LIBDIR_NICKEL}"
		-Ddri-search-path="${EPREFIX}/usr/${LIBDIR_NICKEL}/dri"
		-Dplatforms=x11,wayland
		-Dgallium-drivers=iris,crocus,radeonsi,r600,nouveau,swrast,zink
		-Dvulkan-drivers=intel,intel_hasvk,amd
		-Dvulkan-layers=device-select
		-Degl=enabled
		-Dgbm=enabled
		-Dglx=dri
		-Dglvnd=true
		-Dgles1=disabled
		-Dgles2=enabled
		-Dshared-glapi=enabled
		-Dllvm=enabled
		-Dshared-llvm=disabled
		-Dgallium-va=disabled
		-Dgallium-vdpau=disabled
		-Dgallium-xa=disabled
		-Dgallium-nine=false
		-Dgallium-opencl=disabled
		-Dvalgrind=disabled
		-Dlibunwind=disabled
		-Dlmsensors=disabled
		-Dselinux=false
		-Dzstd=disabled
		-Dbuild-tests=false
		-Dexecmem=false
		-Dshader-cache-default=true
		--buildtype release
	)
	meson_src_configure
}

src_install() {
	meson_src_install

	local lib="${ED}/usr/${LIBDIR_NICKEL}"
	# GBM's header beside the library, for Xwayland (see above).
	insinto /usr/${LIBDIR_NICKEL}/include
	doins "${ED}"/usr/include/gbm.h
	sed -i "s|^includedir=.*|includedir=\${prefix}/${LIBDIR_NICKEL}/include|" \
		"${lib}"/pkgconfig/gbm.pc || die
	grep -q "^includedir=.*/${LIBDIR_NICKEL}/include" "${lib}"/pkgconfig/gbm.pc ||
		die "gbm.pc: includedir"
	# Apart from ChromeOS's Mesa: no headers or driver config in /usr, and
	# the libglvnd EGL vendor file only where the session's variable names it.
	rm -rf "${ED}"/usr/include "${ED}"/usr/share/drirc.d || die
	mkdir -p "${ED}"/usr/share/nickel-mesa || die
	mv "${ED}"/usr/share/glvnd/egl_vendor.d/50_mesa.json \
		"${ED}"/usr/share/nickel-mesa/60_nickel_mesa.json || die
	rm -r "${ED}"/usr/share/glvnd || die
	# The loaders (libglvnd's EGL, Vulkan's) open these libraries by the
	# names in their JSON files, which are the whole system's: Gentoo's apps
	# read the same EGL vendor list and Vulkan directories with their own
	# glibc and loaders. So each is known by a name only the OS's library
	# path has, a link into this directory: the OS's apps load this Mesa,
	# and in a Gentoo app the name is not found and the loader moves on to
	# Gentoo's own Mesa. The names do not start
	# with "lib": ldconfig takes every lib* file in /usr/lib64 for a library
	# and links its soname to it, so a libEGL_nickel_mesa.so.0 there became
	# libEGL_mesa.so.0, ChromeOS's own EGL, for Chrome and everything else.
	# The dynamic loader finds any name in /usr/lib64.
	nickel_name() {
		local json=$1 from=$2 to=$3
		sed -i -E "s|\"library_path\" *: *\"[^\"]*${from}\"|\"library_path\": \"${to}\"|" \
			"${json}" || die
		grep -q "\"${to}\"" "${json}" || die "${json}: no ${from}"
		[[ -e ${lib}/${from} ]] || die "no ${from}"
		dosym "${LIBDIR_NICKEL##*/}/${from}" "/usr/lib64/${to}"
	}
	nickel_name "${ED}"/usr/share/nickel-mesa/60_nickel_mesa.json \
		libEGL_mesa.so.0 nickel-mesa-libEGL_mesa.so.0
	local icd
	for icd in intel intel_hasvk radeon; do
		nickel_name "${ED}"/usr/share/vulkan/icd.d/${icd}_icd.x86_64.json \
			libvulkan_${icd}.so nickel-mesa-libvulkan_${icd}.so
		# Its own file name: Gentoo's Mesa has the standard one, and with the
		# same name the OS's file won and Gentoo's apps had no Vulkan driver.
		mv "${ED}"/usr/share/vulkan/icd.d/{,nickel-mesa_}${icd}_icd.x86_64.json || die
	done
	nickel_name "${ED}"/usr/share/vulkan/implicit_layer.d/VkLayer_MESA_device_select.json \
		libVkLayer_MESA_device_select.so nickel-mesa-libVkLayer_MESA_device_select.so
	# The same for the layer, and a layer name of its own: the loader takes
	# one layer per name.
	sed -i 's/"VK_LAYER_MESA_device_select"/"VK_LAYER_NICKEL_MESA_device_select"/' \
		"${ED}"/usr/share/vulkan/implicit_layer.d/VkLayer_MESA_device_select.json || die
	grep -q VK_LAYER_NICKEL_MESA_device_select \
		"${ED}"/usr/share/vulkan/implicit_layer.d/VkLayer_MESA_device_select.json || die
	mv "${ED}"/usr/share/vulkan/implicit_layer.d/{,nickel-mesa_}VkLayer_MESA_device_select.json || die
	# GLX: libglvnd opens libGLX_<vendor>.so.0 by name, and ChromeOS's Mesa
	# ships none: link it into the default path (a Gentoo app finds its own).
	dosym "${LIBDIR_NICKEL##*/}/libGLX_mesa.so.0" /usr/lib64/libGLX_mesa.so.0
	[[ -e ${lib}/libGLX_mesa.so.0 ]] || die "no libGLX_mesa"
}
