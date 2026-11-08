# Copyright 2021-2026 Gentoo Authors
# Distributed under the terms of the GNU General Public License v2

EAPI=7

# NickelOS: ported from Gentoo xwayland-24.1.13 (EAPI 8 -> 7): KEYWORDS="*".
# Supersedes chromiumos-overlay's 1.20.14 (an xorg-server build for Crostini
# guests). Glamor (X11 drawn on the GPU, buffers shared with apps through
# DRI3 and with Exo through linux-dmabuf) runs on nickel-mesa, the Mesa for
# Linux apps: its GBM, linked here and found at run time by RPATH ahead of
# ChromeOS's minigbm (whose gbm.h numbers the import types differently, so
# an Xwayland built against it hands Mesa's GBM requests it rejects), and
# its EGL through libglvnd (the session's __EGL_VENDOR_LIBRARY_FILENAMES).
# sommelier decides whether glamor is used (nickel-x11: --glamor and a
# render node); otherwise Xwayland renders in software (-shm).
# Off: GLX unless USE=glx (needs gl.pc, i.e. libglvnd[X], which reven builds
# without), libei, libdecor, Xvfb, secure-rpc, SELinux, systemd notify,
# libunwind, docs, tests. The tarball's top-level dependency('x11') is only
# used by the tests and is disabled as in Gentoo's !test path. protocol.txt and
# Xserver.1 are kept: there is no xorg-server here for them to collide with.

inherit flag-o-matic meson

SRC_URI="https://xorg.freedesktop.org/archive/individual/xserver/${P}.tar.xz"
KEYWORDS="*"

DESCRIPTION="Standalone X server running under Wayland"
HOMEPAGE="https://www.x.org/ https://gitlab.freedesktop.org/xorg/xserver/xorg-server"

LICENSE="MIT"
SLOT="0"
IUSE="glx"
RESTRICT="test"

COMMON_DEPEND="
	dev-libs/libbsd
	dev-libs/openssl:=
	>=dev-libs/wayland-1.21.0
	>=media-libs/libepoxy-1.5.4
	virtual/opengles
	media-libs/nickel-mesa
	>=x11-libs/libdrm-2.4.116
	>=x11-libs/libXau-1.0.4
	x11-libs/libxcvt
	>=x11-libs/libXdmcp-1.0.2
	>=x11-libs/libXfont2-2.0.1
	x11-libs/libxkbfile
	>=x11-libs/libxshmfence-1.1
	>=x11-libs/pixman-0.27.2
	>=x11-misc/xkeyboard-config-2.4.1-r3
	glx? ( media-libs/libglvnd[X] )
"
DEPEND="
	${COMMON_DEPEND}
	>=dev-libs/wayland-protocols-1.34
	media-fonts/font-util
	>=x11-base/xorg-proto-2024.1
	>=x11-libs/xtrans-1.3.5
	x11-apps/xkbcomp
"
RDEPEND="
	${COMMON_DEPEND}
	x11-apps/xkbcomp
"
BDEPEND="
	dev-util/wayland-scanner
	virtual/pkgconfig
"

PATCHES=(
	# No unsynchronized flips on NVIDIA when explicit sync is on.
	"${FILESDIR}/${P}-no-unsynced-flips.patch"
)

src_prepare() {
	default

	sed -i -e "s/dependency('x11')/disabler()/" meson.build || die
}

src_configure() {
	# nickel-mesa's gbm.pc (and with it gbm.h and libgbm) ahead of the
	# system's. The board's pkg-config wrapper unsets PKG_CONFIG_PATH, so
	# this is that wrapper with nickel-mesa's directory first.
	local pc="${T}/nickel-pkg-config"
	cat > "${pc}" <<-EOF || die
		#!/bin/sh
		PKG_CONFIG_LIBDIR="${SYSROOT}/usr/lib64/nickel-mesa/pkgconfig:\$(printf '%s:' "${SYSROOT}"/usr/*/pkgconfig)"
		export PKG_CONFIG_LIBDIR
		export PKG_CONFIG_SYSROOT_DIR="${SYSROOT}"
		unset PKG_CONFIG_PATH
		export PKG_CONFIG_SYSTEM_INCLUDE_PATH="/usr/include:${SYSROOT}/usr/include"
		export PKG_CONFIG_FDO_SYSROOT_RULES=1
		exec /usr/bin/pkg-config "\$@"
	EOF
	chmod +x "${pc}" || die
	export PKG_CONFIG="${pc}"
	"${pc}" --cflags gbm | grep -q nickel-mesa || die "gbm is not nickel-mesa's"
	append-ldflags "-Wl,-rpath,${EPREFIX}/usr/lib64/nickel-mesa" "-Wl,--disable-new-dtags"
	# Explicit sync (linux-drm-syncobj-v1, which sommelier offers on NVIDIA)
	# needs the dma-buf sync-file ioctls, which ChromiumOS's kernel headers
	# (5.4) predate; the kernel (6.12) has them. Without them Xwayland turns
	# explicit sync off at build time.
	cat > "${T}"/nickel-dma-buf-sync.h <<-EOF || die
		#include <linux/dma-buf.h>
		#ifndef DMA_BUF_IOCTL_EXPORT_SYNC_FILE
		struct dma_buf_export_sync_file { __u32 flags; __s32 fd; };
		struct dma_buf_import_sync_file { __u32 flags; __s32 fd; };
		#define DMA_BUF_IOCTL_EXPORT_SYNC_FILE _IOWR(DMA_BUF_BASE, 2, struct dma_buf_export_sync_file)
		#define DMA_BUF_IOCTL_IMPORT_SYNC_FILE _IOW(DMA_BUF_BASE, 3, struct dma_buf_import_sync_file)
		#endif
	EOF
	append-cppflags -include "${T}"/nickel-dma-buf-sync.h

	local emesonargs=(
		$(meson_use glx)
		-Dxselinux=false
		-Dsystemd_notify=false
		-Dlibunwind=false
		-Dxcsecurity=false
		-Ddpms=true
		-Ddri3=true
		-Ddrm=true
		-Ddtrace=false
		-Dglamor=true
		-Dipv6=true
		-Dsecure-rpc=false
		-Dscreensaver=true
		-Dsha1=libcrypto
		-Dxace=true
		-Dxdmcp=true
		-Dxinerama=true
		-Dxvfb=false
		-Dxv=true
		-Dxwayland-path="${EPREFIX}"/usr/bin
		-Dxwayland_ei=false
		-Dlibdecor=false
		-Ddocs=false
		-Ddevel-docs=false
		-Ddocs-pdf=false
	)

	meson_src_configure
}

src_install() {
	dosym ../bin/Xwayland /usr/libexec/Xwayland

	meson_src_install
}
