# Copyright 2026 The NickelOS Authors
# Distributed under the terms of the GNU General Public License v2

# Virtual cameras: OBS Studio's "Start Virtual Camera" makes one that other
# apps (video calls in Chrome too) can pick. The module is built here against
# the board kernel and signed with its module key, since NickelOS loads only
# signed modules; the kernel's := dependency rebuilds it for every kernel.
# Not loaded at boot: OBS loads it (pkexec modprobe) when its virtual camera
# starts, or `sudo modprobe v4l2loopback` by hand.

EAPI=7

inherit toolchain-funcs

DESCRIPTION="v4l2 loopback device whose output is its own input"
HOMEPAGE="https://github.com/v4l2loopback/v4l2loopback"
SRC_URI="https://github.com/v4l2loopback/${PN}/archive/v${PV}.tar.gz -> ${P}.tar.gz"

LICENSE="GPL-2"
SLOT="0"
KEYWORDS="*"
# The module is signed in src_compile; Portage's strip would break the
# signature (the kernel refused it: "Key was rejected by service").
RESTRICT="strip"

KERNEL_PKG="sys-kernel/chromeos-kernel-6_12"
DEPEND="${KERNEL_PKG}:="
RDEPEND="${KERNEL_PKG}:="

src_compile() {
	local kernel_out="${SYSROOT}/usr/src/linux"
	# The kernel's build directory points at its long-gone portage work dir
	# for sources; the cros-workon checkout is the same commit.
	local kernel_src="${CHROOT_SOURCE_ROOT:-/mnt/host/source}/src/third_party/kernel/v6.12"
	[[ -f ${kernel_src}/Makefile ]] || die "no kernel sources at ${kernel_src}"

	# kbuild reads ARCH, CFLAGS and LDFLAGS from the environment; portage's
	# values (ARCH=amd64, user flags) are not the kernel's.
	env -u CFLAGS -u CXXFLAGS -u LDFLAGS -u CPPFLAGS \
		emake -C "${kernel_src}" O="${kernel_out}" M="${S}" \
		ARCH=x86_64 LLVM=1 CC="$(tc-getCC)" LD=ld.lld \
		modules

	# Strip, then sign: a signature covers the file as shipped.
	$(tc-getSTRIP) --strip-debug v4l2loopback.ko || die
	"${kernel_out}/scripts/sign-file" sha256 \
		"${kernel_out}/certs/signing_key.pem" \
		"${kernel_out}/certs/signing_key.x509" \
		v4l2loopback.ko || die "signing v4l2loopback.ko failed"

	emake -C utils CC="$(tc-getCC)"
}

src_install() {
	local kver
	kver=$(<"${SYSROOT}/usr/src/linux/include/config/kernel.release") || die
	insinto "/lib/modules/${kver}/extra"
	doins v4l2loopback.ko

	dobin utils/v4l2loopback-ctl
	# The video group may make and remove virtual cameras; stable names
	# under /dev/v4l/by-id.
	insinto /lib/udev/rules.d
	doins udev/60-persistent-v4l2loopback.rules
	dodoc README.md
}

pkg_postinst() {
	local kver
	kver=$(<"${SYSROOT}/usr/src/linux/include/config/kernel.release") || die
	depmod -b "${ROOT:-/}" -a "${kver}" || die "depmod failed"
}
