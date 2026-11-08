# Copyright 2026 The NickelOS Authors
# Distributed under the terms of the GNU General Public License v2

# Xbox One and Series controllers over Bluetooth with everything working:
# rumble (trigger rumble too), battery level, correct button mapping for
# games, the Share button. The kernel's own hid-microsoft does only part of
# that. The module is built here against the board kernel and signed with its
# module key, since NickelOS loads only signed modules; the kernel's :=
# dependency rebuilds it for every kernel. Its udev rule hands a paired
# controller to it.

EAPI=7

inherit toolchain-funcs

DESCRIPTION="Advanced Linux Driver for Xbox One Wireless Controller"
HOMEPAGE="https://atar-axis.github.io/xpadneo/"
SRC_URI="https://github.com/atar-axis/xpadneo/archive/v${PV}.tar.gz -> ${P}.tar.gz"

LICENSE="GPL-2 GPL-3+"
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
		emake -C "${kernel_src}" O="${kernel_out}" M="${S}/hid-xpadneo/src" \
		ARCH=x86_64 LLVM=1 CC="$(tc-getCC)" LD=ld.lld \
		VERSION="v${PV}" modules

	# Strip, then sign: a signature covers the file as shipped.
	local ko=hid-xpadneo/src/hid-xpadneo.ko
	$(tc-getSTRIP) --strip-debug "${ko}" || die
	"${kernel_out}/scripts/sign-file" sha256 \
		"${kernel_out}/certs/signing_key.pem" \
		"${kernel_out}/certs/signing_key.x509" \
		"${ko}" || die "signing hid-xpadneo.ko failed"
}

src_install() {
	local kver
	kver=$(<"${SYSROOT}/usr/src/linux/include/config/kernel.release") || die
	insinto "/lib/modules/${kver}/extra"
	doins hid-xpadneo/src/hid-xpadneo.ko

	insinto /lib/modprobe.d
	doins hid-xpadneo/etc-modprobe.d/xpadneo.conf
	insinto /lib/udev/rules.d
	doins hid-xpadneo/etc-udev-rules.d/{60-xpadneo,70-xpadneo-disable-hidraw}.rules
	dodoc NEWS.md docs/README.md
}

pkg_postinst() {
	local kver
	kver=$(<"${SYSROOT}/usr/src/linux/include/config/kernel.release") || die
	depmod -b "${ROOT:-/}" -a "${kver}" || die "depmod failed"
}
