# Copyright 2026 NickelOS
# Distributed under the terms of the GNU General Public License v2

EAPI=7

DESCRIPTION="NickelOS: your layer of the OS's Gentoo (overlays, records after OS updates, accounts, repository)"
HOMEPAGE="https://nickelos.dev"
LICENSE="GPL-2"
SLOT="0"
KEYWORDS="*"

S="${WORKDIR}"

# docs/unified.md ("Your layer"). The OS's Gentoo is in the image
# (scripts/unified/nickel-unify); this is what you add to it. nickel-accounts
# merges Gentoo's system accounts into the account files.
RDEPEND="
	app-admin/sudo
	app-arch/zstd
	chromeos-base/nickel-accounts
	dev-lang/python
	net-misc/curl
	sys-auth/polkit
"

src_install() {
	dosbin "${FILESDIR}"/nickel-gentoo
	# Portage's useradd/groupadd/usermod (Gentoo's PATH, /etc/env.d/00-nickelos).
	exeinto /usr/libexec/nickel-gentoo/accounts
	doexe "${FILESDIR}"/accounts/*
	# Portage's settings for the device and NickelOS's repository (its
	# profile): nickel-unify puts the settings into the image's /etc, read-only
	# below your layer, so OS updates bring their changes.
	insinto /usr/share/nickel-gentoo
	doins -r "${FILESDIR}"/etc
	insinto /usr/share/nickel-gentoo/repo
	doins -r "${FILESDIR}"/repo/nickel
	# The records and overlays at boot, then the rest; Gentoo's repository at
	# the first startup.
	insinto /etc/init
	doins "${FILESDIR}"/init/nickel-gentoo-mount.conf "${FILESDIR}"/init/nickel-gentoo.conf \
		"${FILESDIR}"/init/nickel-gentoo-setup.conf
	# Your services under OpenRC (Gentoo's, installed by nickel-unify): its
	# default runlevel at startup, and what ChromeOS provides for their
	# dependencies.
	doins "${FILESDIR}"/init/nickel-openrc.conf
	# Not in /etc/init.d: ChromeOS's install mask drops that directory from
	# every board package, so services that need "net" (sshd, CUPS,
	# Tailscale) found nothing to provide it. nickel-unify links it there.
	exeinto /usr/libexec/nickel-gentoo
	doexe "${FILESDIR}"/openrc/nickel-os
	# Removing a Gentoo app from the launcher (nickel-appd, under pkexec).
	exeinto /usr/libexec
	doexe "${FILESDIR}"/nickel-gentoo-uninstall
	insinto /usr/share/polkit-1/actions
	doins "${FILESDIR}"/polkit/org.nickelos.gentoo.policy
	# Symlinks and FIFOs where ChromeOS's startup blocks them otherwise.
	insinto /usr/share/cros/startup/symlink_exceptions
	doins "${FILESDIR}"/startup/nickel-gentoo-symlink-exceptions.txt
	insinto /usr/share/cros/startup/fifo_exceptions
	doins "${FILESDIR}"/startup/nickel-gentoo-fifo-exceptions.txt
}
