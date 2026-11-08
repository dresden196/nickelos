# Copyright 1999-2026 Gentoo Authors
# Distributed under the terms of the GNU General Public License v2

EAPI=7

# NickelOS: from Gentoo geoclue-2.8.1 (EAPI 8 -> 7), for xdg-desktop-portal's
# Location portal (USE geolocation) and Linux apps that ask GeoClue
# directly: the service and libgeoclue, the Wi-Fi and IP sources (beaconDB),
# and the where-am-i demo (a terminal check, hidden from the launcher); no
# modem, NMEA, compass, demo agent, introspection, vapi or docs. Our
# geoclue.conf: nickel-appd is the agent (it follows ChromeOS's location
# switch), only the Wi-Fi and IP sources. Our D-Bus policy: GeoClue reads
# what wpa_supplicant has seen but does not start scans: shill does, asked by
# nickel-appd while GeoClue runs. r1: GeoClue only asks for a position with
# the networks once a scan is done, and when its own scan was refused it
# stopped listening for anyone's, so it never used Wi-Fi (the internet
# address only, 25 km off): now it keeps taking others' scans.

PYTHON_COMPAT=( python3_{11..14} )
PYTHON_REQ_USE="xml(+)"
inherit meson python-any-r1 systemd

DESCRIPTION="Location information D-Bus service"
HOMEPAGE="https://gitlab.freedesktop.org/geoclue/geoclue/-/wikis/home"
SRC_URI="https://gitlab.freedesktop.org/geoclue/${PN}/-/archive/${PV}/${P}.tar.bz2"

LICENSE="LGPL-2.1+ GPL-2+"
SLOT="2.0"
KEYWORDS="*"
IUSE=""

DEPEND="
	>=dev-libs/glib-2.74:2
	>=dev-libs/json-glib-0.14.0
	>=net-libs/libsoup-3.0.0:3.0
"
RDEPEND="${DEPEND}
	acct-user/geoclue
	net-libs/glib-networking
	sys-apps/dbus
"
PATCHES=(
	"${FILESDIR}"/${P}-scans-of-others.patch
)

BDEPEND="${PYTHON_DEPS}
	dev-util/glib-utils
	>=sys-devel/gettext-0.19.8
	virtual/pkgconfig
"

src_configure() {
	local emesonargs=(
		-Dlibgeoclue=true
		-Dintrospection=false
		-Dvapi=false
		-Dgtk-doc=false
		-D3g-source=false
		-Dcdma-source=false
		-Dmodem-gps-source=false
		-Dnmea-source=false
		-Dcompass=false
		-Denable-backend=true
		-Ddemo-agent=false
		-Dsystemd-system-unit-dir="$(systemd_get_systemunitdir)"
		-Ddbus-srv-user=geoclue
	)
	meson_src_configure
}

src_install() {
	meson_src_install
	# sysusers.d file is provided by acct-user/geoclue, #973676
	rm -f "${ED}"/usr/lib/sysusers.d/geoclue-sysusers.conf || die
	# The GIR is made whenever g-ir-scanner is found, introspection or not.
	rm -rf "${ED}"/usr/share/gir-1.0 "${ED}"/usr/$(get_libdir)/girepository-1.0 || die
	insinto /etc/geoclue
	newins "${FILESDIR}"/geoclue.conf geoclue.conf
	local policy
	policy=$(find "${ED}" -name org.freedesktop.GeoClue2.conf -path '*dbus-1*') || die
	[[ -n ${policy} ]] || die "no D-Bus policy installed"
	cp "${FILESDIR}"/org.freedesktop.GeoClue2.conf "${policy}" || die
}
