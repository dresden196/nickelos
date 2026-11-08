# Distributed under the terms of the GNU General Public License v2

EAPI=7

DESCRIPTION="NickelOS: /usr/local always mounted (the machine's own services, a test image's tools)"
LICENSE="GPL-2"
SLOT="0"
KEYWORDS="*"

S="${WORKDIR}"

# patchelf gives a test image's /usr/local tools their RUNPATHs
# (nickel-local-rpath). ChromiumOS's own Portage on the device (dev_install,
# nickel-portage-*) is gone: Gentoo's Portage is the OS's (docs/unified.md).
RDEPEND="
	dev-util/patchelf
"

src_install() {
	insinto /etc/init
	doins "${FILESDIR}"/init/nickel-usr-local.conf
	doins "${FILESDIR}"/init/nickel-local-services.conf
	doins "${FILESDIR}"/init/nickel-local-service.conf
	doins "${FILESDIR}"/init/nickel-gpu-dir.conf
	doins "${FILESDIR}"/init/nickel-sound-cards.conf
	doins "${FILESDIR}"/init/nickel-grub-menu.conf
	insinto /usr/share/nickel
	doins "${FILESDIR}"/grub-hidden-menu.cfg

	dosbin "${FILESDIR}"/nickel-local-rpath
	# The machine's own boot scripts and services (/usr/local/etc/nickel).
	dosbin "${FILESDIR}"/nickel-local-services
	# A hardware report to share: what the computer is and how NickelOS
	# drives it.
	dobin "${FILESDIR}"/nickel-hwreport

	insinto /usr/share/cros/startup/symlink_exceptions
	doins "${FILESDIR}"/startup/nickel-symlink-exceptions.txt
	insinto /usr/share/cros/startup/fifo_exceptions
	doins "${FILESDIR}"/startup/nickel-fifo-exceptions.txt

	# Base URL of the NickelOS binhost (<url>/gentoo: NickelOS's Gentoo
	# packages, nickel-gentoo): NickelOS's public one, next to the update server
	# (docs/updates.md). Test images get the build host instead
	# (nickel-test-image-finish).
	insinto /etc/nickel
	doins "${FILESDIR}"/binhost
}
