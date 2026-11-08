# Copyright 2026 NickelOS Authors
# Distributed under the terms of the GNU General Public License v2

EAPI=7

# NickelOS's connection to phones: Valent (net-misc/valent), a KDE Connect
# implementation, running in every session with its ports open while it
# runs. Phones pair through the KDE Connect app (Android: Play Store or
# F-Droid; iPhone: App Store) on the same network.

DESCRIPTION="NickelOS phone connection: Valent (KDE Connect) in the session"
HOMEPAGE="https://github.com/dresden196/overlay-nickel"

LICENSE="GPL-2"
SLOT="0"
KEYWORDS="*"

S="${WORKDIR}"

RDEPEND="
	chromeos-base/nickel-session
	net-firewall/iptables
	net-misc/valent
"

src_install() {
	insinto /etc/init
	doins "${FILESDIR}"/init/nickel-phone.conf
	exeinto /usr/libexec/nickel-phone
	doexe "${FILESDIR}"/nickel-valent
}
