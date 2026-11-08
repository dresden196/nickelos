# Copyright 1999-2026 Gentoo Authors
# Distributed under the terms of the GNU General Public License v2

EAPI=7

# NickelOS: ported from Gentoo at-spi2-core-2.60.7 (EAPI 8 -> 7): KEYWORDS="*", single ABI (meson, not meson-multilib), built against ChromiumOS's GLib 2.76. Provides ATK (libatk-1.0), atk-bridge (libatk-bridge-2.0), libatspi and the accessibility bus launcher; supersedes portage-stable's dev-libs/atk-2.24 (blocked, as in Gentoo; nothing in the image uses it).
# python-any-r1 instead of python-r1: Python is only needed at build time; the one Python file the build installs (a PyGObject override, gi/overrides/Atspi.py) is removed, there is no PyGObject on the image.
# Off: introspection, dbus-glib, docs, systemd user unit, dbus-broker, the GTK 2 module. X is optional and off in the NickelOS profile: libatspi then finds the accessibility bus through the session bus (org.a11y.Bus, D-Bus activated), which works for X11 and Wayland apps alike; with X it would also publish/read the AT_SPI_BUS root-window property and need libXtst and libXi.
# dbus-daemon's path is given explicitly instead of being looked up on the build machine.

PYTHON_COMPAT=( python3_{11..14} )

inherit gnome.org meson python-any-r1 xdg

DESCRIPTION="D-Bus accessibility specifications and registration daemon"
HOMEPAGE="https://gitlab.gnome.org/GNOME/at-spi2-core"

LICENSE="LGPL-2.1+"
SLOT="2"
KEYWORDS="*"
IUSE="X"

DEPEND="
	>=sys-apps/dbus-1.5
	>=dev-libs/glib-2.67.4:2
	>=dev-libs/libxml2-2.9.1:2=
	X? (
		x11-libs/libX11
		x11-libs/libXtst
		x11-libs/libXi
	)

	!<dev-libs/atk-2.46.0
"
RDEPEND="${DEPEND}"
BDEPEND="
	dev-util/glib-utils
	${PYTHON_DEPS}
	>=sys-devel/gettext-0.19.8
	virtual/pkgconfig
"

src_configure() {
	local emesonargs=(
		-Ddefault_bus=dbus-daemon
		-Ddbus_daemon="${EPREFIX}"/usr/bin/dbus-daemon
		-Duse_systemd=false
		-Dgtk2_atk_adaptor=false
		-Ddocs=false
		-Dintrospection=disabled
		$(meson_feature X x11)
		-Datk_only=false
		-Dpython="${EPYTHON}"
		-Ddbus_glib=disabled
	)
	meson_src_configure
}

src_install() {
	meson_src_install
	einstalldocs

	# PyGObject override, useless without PyGObject.
	rm -r "${ED}"/usr/lib/python* || die
}
