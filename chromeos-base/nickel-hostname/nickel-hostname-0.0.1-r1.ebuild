# Copyright 2026 The NickelOS Authors
# Distributed under the terms of the GNU General Public License v2

EAPI=7

DESCRIPTION="NickelOS: the computer's name (org.freedesktop.hostname1)"
LICENSE="GPL-2"
SLOT="0"
KEYWORDS="*"

S="${WORKDIR}"

RDEPEND="
	dev-lang/python
	dev-python/pygobject
"

src_install() {
	exeinto /usr/libexec/nickel-hostname
	doexe "${FILESDIR}"/nickel-hostnamed
	insinto /etc/init
	doins "${FILESDIR}"/nickel-hostnamed.conf
	insinto /etc/dbus-1/system.d
	doins "${FILESDIR}"/org.freedesktop.hostname1.conf
}
