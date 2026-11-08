# Distributed under the terms of the GNU General Public License v2

EAPI=7

DESCRIPTION="NickelOS Flatpak integration: storage, sandbox and permission plumbing"
LICENSE="GPL-2"
SLOT="0"
KEYWORDS="*"

S="${WORKDIR}"

RDEPEND="
	chromeos-base/nickel-portal
	chromeos-base/nickel-session
	dev-libs/glib
	sys-apps/accountsservice
	sys-apps/flatpak
	sys-apps/xdg-desktop-portal
	sys-auth/polkit
"

src_install() {
	insinto /etc/init
	doins "${FILESDIR}"/init/nickel-flatpak-storage.conf
	doins "${FILESDIR}"/init/nickel-flatpak-defaults.conf
	dosbin "${FILESDIR}"/nickel-flatpak-defaults
	# Tells the user while the first boot installs the default apps.
	doins "${FILESDIR}"/init/nickel-apps-progress.conf
	exeinto /usr/libexec/nickel-flatpak
	doexe "${FILESDIR}"/nickel-apps-progress

	# NVIDIA's Flatpak extensions, matched to the running driver.
	doins "${FILESDIR}"/init/nickel-flatpak-gpu.conf
	dosbin "${FILESDIR}"/nickel-flatpak-gpu

	# Flathub, added to the system installation the first time it is used.
	insinto /usr/share/flatpak/remotes.d
	doins "${FILESDIR}"/remotes/flathub.flatpakrepo

	insinto /usr/share/cros/startup/symlink_exceptions
	doins "${FILESDIR}"/startup/nickel-flatpak-symlink-exceptions.txt
	insinto /usr/share/cros/startup/fifo_exceptions
	doins "${FILESDIR}"/startup/nickel-flatpak-fifo-exceptions.txt

	# System-wide Flatpaks follow Flatpak's own polkit rules: administrators
	# (wheel) in their active local session need no password, anyone else
	# an administrator's (elogind tells polkit which session is active).
}
