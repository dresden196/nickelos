# Copyright 2026 The NickelOS Authors
# Distributed under the terms of the GNU General Public License v2

# Microsoft's Xbox Wireless Adapter (the USB dongle) and the controllers,
# headsets and chatpads that talk through it: xonedo, the maintained fork of
# xone that Bazzite uses. Wired controllers stay with the kernel's xpad, and
# Bluetooth ones with hid-microsoft or xpadneo. The modules are built here
# against the board kernel and signed with its module key, since NickelOS
# loads only signed modules; nickel-build-stock rebuilds them with every
# kernel.
#
# The dongle needs Microsoft's firmware, which may not be redistributed, and
# LoadPin takes firmware files only from the verified root filesystem. So
# nothing of Microsoft's is in the image: when a dongle first appears,
# nickel-xone-firmware downloads Microsoft's driver package from Microsoft's
# own server, checks the firmware's SHA-256, keeps it on the data partition
# and hands it to the driver through sysfs (this package's patch), as Fedora
# (lpf) and Gentoo (GURU) fetch it on the user's machine.

EAPI=7

inherit toolchain-funcs

COMMIT="982cbcb019ae4d2bee5ae69385223409ee555c88"
DESCRIPTION="Linux kernel driver for the Xbox Wireless Adapter (xonedo)"
HOMEPAGE="https://github.com/OpenGamingCollective/xonedo"
SRC_URI="https://github.com/OpenGamingCollective/xonedo/archive/${COMMIT}.tar.gz -> ${P}.tar.gz"

LICENSE="GPL-2+"
SLOT="0"
KEYWORDS="*"
# The modules are signed in src_compile; Portage's strip would break the
# signatures (the kernel refused v4l2loopback and xpadneo that way). Not on
# Google's mirrors, which the board's Portage otherwise fetches from alone.
RESTRICT="mirror strip"

KERNEL_PKG="sys-kernel/chromeos-kernel-6_12"
DEPEND="${KERNEL_PKG}:="
RDEPEND="
	${KERNEL_PKG}:=
	app-arch/libarchive
	net-misc/curl
"

S="${WORKDIR}/xonedo-${COMMIT}"

PATCHES=(
	"${FILESDIR}"/xonedo-0.5.7-dongle-the-firmware-can-be-handed-over-by-userspace.patch
)

MODULES=(
	xone_gip xone_dongle xone_gip_gamepad xone_gip_headset xone_gip_chatpad
	xone_gip_madcatz_strat xone_gip_madcatz_glam xone_gip_pdp_jaguar
)

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
	local m
	for m in "${MODULES[@]}"; do
		$(tc-getSTRIP) --strip-debug "${m}.ko" || die
		"${kernel_out}/scripts/sign-file" sha256 \
			"${kernel_out}/certs/signing_key.pem" \
			"${kernel_out}/certs/signing_key.x509" \
			"${m}.ko" || die "signing ${m}.ko failed"
	done
}

src_install() {
	local kver m
	kver=$(<"${SYSROOT}/usr/src/linux/include/config/kernel.release") || die
	insinto "/lib/modules/${kver}/extra"
	for m in "${MODULES[@]}"; do
		doins "${m}.ko"
	done

	# Upstream's: the kernel's mt76x2u must not take the dongle.
	insinto /lib/modprobe.d
	newins install/modprobe.conf xone.conf

	# The firmware, fetched from Microsoft when a dongle appears.
	exeinto /usr/libexec/nickel-xone
	doexe "${FILESDIR}"/nickel-xone-firmware
	insinto /etc/init
	doins "${FILESDIR}"/nickel-xone-firmware.conf
	insinto /lib/udev/rules.d
	doins "${FILESDIR}"/90-nickel-xone-dongle.rules

	dodoc README.md
}

pkg_postinst() {
	local kver
	kver=$(<"${SYSROOT}/usr/src/linux/include/config/kernel.release") || die
	depmod -b "${ROOT:-/}" -a "${kver}" || die "depmod failed"
}
