# Copyright 2026 NickelOS Authors
# Distributed under the terms of the GNU General Public License v2

EAPI=7

# NickelOS: Valent, a GTK 4 implementation of KDE Connect, as NickelOS's
# connection to phones (Settings > Connected devices): pair with the KDE
# Connect app on Android or iOS, phone notifications on ChromeOS, files
# both ways, clipboard, battery, find my phone, media control, the phone
# as remote input, SMS.
#
# 1.0.0.alpha.47 is the newest release that builds on R156's GLib 2.76
# (alpha.48 needs GLib 2.80 throughout); it asks for 2.80 too but uses only
# three of its functions, which a patch supplies. Two later LAN fixes are
# backported: a protocol v8 handshake
# must keep its deviceId (upstream 85f77312), and identity packets name
# their target, against amplification through spoofed UDP broadcasts
# (df82168b).
#
# Plugins: everything but Bluetooth (the LAN is how KDE Connect usually
# connects), EDS address books (no Evolution), the lock screen (no
# org.freedesktop.ScreenSaver), SFTP (no GVfs) and PulseAudio (its
# libgnome-volume-control is a git submodule; the PipeWire plugin does the
# volume instead). The "gnome" plugin is Valent's own window (devices,
# messages, contacts, settings), in GTK 4 and libadwaita. Find My Device
# rings with paplay rather than GStreamer, which ChromiumOS doesn't have.
# GLib's TLS (glib-networking) carries KDE Connect's encrypted connections.
# No introspection, vapi, docs or tests.

inherit gnome2-utils meson xdg

MY_PV="${PV/_alpha/.alpha.}"

DESCRIPTION="Connect, control and sync devices (KDE Connect protocol)"
HOMEPAGE="https://valent.andyholmes.ca https://github.com/andyholmes/valent"
SRC_URI="https://github.com/andyholmes/valent/archive/refs/tags/v${MY_PV}.tar.gz -> ${P}.tar.gz"
S="${WORKDIR}/${PN}-${MY_PV}"

LICENSE="GPL-3+ CC0-1.0"
SLOT="0"
KEYWORDS="*"
RESTRICT="test"

RDEPEND="
	>=dev-libs/glib-2.76:2
	>=dev-libs/json-glib-1.6
	dev-libs/libpeas:2
	>=dev-libs/libportal-0.7.1
	dev-libs/libphonenumber:=
	>=gnome-extra/evolution-data-server-3.48:=
	>=app-misc/tinysparql-3.0:3
	>=gui-libs/gtk-4.10:4
	>=gui-libs/libadwaita-1.5:1
	media-libs/libpulse
	media-video/pipewire:=
	>=net-libs/gnutls-3.1.3:=
	net-libs/glib-networking
	x11-libs/gdk-pixbuf:2
"
DEPEND="${RDEPEND}"
BDEPEND="
	dev-util/glib-utils
	>=sys-devel/gettext-0.19.8
	virtual/pkgconfig
"

PATCHES=(
	"${FILESDIR}"/${P}-lan-backports.patch
	"${FILESDIR}"/${P}-findmyphone-paplay.patch
	"${FILESDIR}"/${P}-glib-2.76.patch
)

src_prepare() {
	default
	# Schemas, icon cache and desktop database: pkg_postinst's, not meson's.
	sed -i '/^gnome.post_install(/,/^)/d' meson.build || die
}

src_configure() {
	local emesonargs=(
		-Ddocumentation=false
		-Dintrospection=false
		-Dvapi=false
		-Dprofile=default
		-Dtests=false
		-Dfuzz_tests=false
		-Dinstalled_tests=false
		-Dtracing=false
		-Dplugin_bluez=false
		-Dplugin_eds=false
		-Dplugin_lock=false
		-Dplugin_pulseaudio=false
		-Dplugin_sftp=false
	)
	meson_src_configure
}

src_install() {
	meson_src_install
	# Valent makes its windows outside a GtkApplication, so GTK gives them
	# the program's name as their Wayland app id ("valent"), not the desktop
	# file's: ChromeOS matches the window to the app (its shelf icon) by
	# StartupWMClass.
	sed -i '/^\[Desktop Entry\]/a StartupWMClass=valent' \
		"${ED}"/usr/share/applications/ca.andyholmes.Valent.desktop || die
}

pkg_postinst() {
	xdg_pkg_postinst
	gnome2_schemas_update
}

pkg_postrm() {
	xdg_pkg_postrm
	gnome2_schemas_update
}
