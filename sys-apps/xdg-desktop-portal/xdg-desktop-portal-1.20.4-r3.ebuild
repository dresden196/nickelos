# Copyright 1999-2026 Gentoo Authors
# Distributed under the terms of the GNU General Public License v2

EAPI=7

# NickelOS: ported from Gentoo xdg-desktop-portal-1.20.4-r1 (EAPI 8 -> 7): KEYWORDS="*", gdbus-codegen floor 2.80.5-r1 dropped (only needed for docs, which are off), man pages (docutils) and tests off, dropped the tests patch and the OpenRC user init script; the cross build gets bwrap's target path from an extra meson machine file (the SDK has no bwrap to find).
# r3: built with USE geolocation (profiles/base/package.use): the Location portal, through app-misc/geoclue.

PYTHON_COMPAT=( python3_{11..14} )

inherit meson python-any-r1 systemd

DESCRIPTION="Desktop integration portal"
HOMEPAGE="https://flatpak.github.io/xdg-desktop-portal/ https://github.com/flatpak/xdg-desktop-portal"
SRC_URI="https://github.com/flatpak/${PN}/releases/download/${PV}/${P}.tar.xz"

LICENSE="LGPL-2.1"
SLOT="0"
KEYWORDS="*"
IUSE="geolocation flatpak gstreamer seccomp systemd udev"
# Upstream expect flatpak to be used w/ seccomp and flatpak needs bwrap anyway
REQUIRED_USE="flatpak? ( seccomp )"
RESTRICT="test"

DEPEND="
	>=dev-libs/glib-2.72:2
	dev-libs/json-glib
	>=media-video/pipewire-0.3:=
	>=sys-fs/fuse3-3.10.0:3=[suid]
	x11-libs/gdk-pixbuf
	geolocation? ( >=app-misc/geoclue-2.5.3:2.0 )
	gstreamer? (
		media-libs/gst-plugins-base:1.0
		sys-apps/bubblewrap
	)
	flatpak? ( sys-apps/flatpak )
	seccomp? ( sys-apps/bubblewrap )
	systemd? ( sys-apps/systemd )
	udev? ( dev-libs/libgudev )
"
RDEPEND="
	${DEPEND}
	sys-apps/dbus
"
BDEPEND="
	${PYTHON_DEPS}
	sys-devel/gettext
	virtual/pkgconfig
"

PATCHES=(
	# Make gstreamer use optional
	"${FILESDIR}/${PN}-1.20.4-optional-gstreamer.patch"
	# NickelOS: trashing failed for files in the home (a bind mount) and for
	# every sandboxed app
	"${FILESDIR}/${PN}-1.20.4-trash-in-the-home.patch"
)

src_configure() {
	if use gstreamer; then
		# bug #951609, #972571
		addpredict /dev
	fi

	local emesonargs=(
		-Ddbus-service-dir="${EPREFIX}/usr/share/dbus-1/services"
		-Dsystemd-user-unit-dir="$(systemd_get_userunitdir)"
		$(meson_feature flatpak flatpak-interfaces)
		$(meson_feature geolocation geoclue)
		$(meson_feature udev gudev)
		$(meson_feature seccomp sandboxed-image-validation)
		$(meson_feature gstreamer sandboxed-sound-validation)
		$(meson_feature systemd)
		# Requires flatpak
		-Ddocumentation=disabled
		# -Dxmlto-flags=
		-Ddatarootdir="${EPREFIX}/usr/share"
		-Dman-pages=disabled
		-Dinstalled-tests=false
		-Dtests=disabled
	)

	if use seccomp || use gstreamer; then
		# NickelOS: meson looks for bwrap on the build machine, which is
		# the SDK when cross-compiling. Point it at the target's bwrap; the
		# path is only compiled into the validators, never run at build
		# time.
		cat > "${T}"/nickel-bwrap.ini <<-EOF || die
			[binaries]
			bwrap = '${EPREFIX}/usr/bin/bwrap'
		EOF
		emesonargs+=( --cross-file "${T}"/nickel-bwrap.ini )
	fi

	meson_src_configure
}

src_install() {
	meson_src_install

	# Install a default to avoid breakage: >=1.18.0 assumes that DEs/WMs
	# will install their own, but we want some fallback in case they don't
	# (so will probably keep this forever). DEs need time to catch up even
	# if they will eventually provide one anyway. See bug #915356.
	#
	# TODO: Add some docs on wiki for users to add their own preference
	# for minimalist WMs etc.
	insinto /usr/share/xdg-desktop-portal
	newins "${FILESDIR}"/default-portals.conf portals.conf
}

pkg_postinst() {
	if ! has_version gui-libs/xdg-desktop-portal-lxqt && ! has_version gui-libs/xdg-desktop-portal-wlr && \
		! has_version kde-plasma/xdg-desktop-portal-kde && ! has_version sys-apps/xdg-desktop-portal-gnome && \
		! has_version sys-apps/xdg-desktop-portal-gtk && ! has_version sys-apps/xdg-desktop-portal-xapp; then
		elog "${PN} is not usable without any of the following XDP"
		elog "implementations installed:"
		elog "  gui-libs/xdg-desktop-portal-lxqt"
		elog "  gui-libs/xdg-desktop-portal-wlr"
		elog "  kde-plasma/xdg-desktop-portal-kde"
		elog "  sys-apps/xdg-desktop-portal-gnome"
		elog "  sys-apps/xdg-desktop-portal-gtk"
		elog "  sys-apps/xdg-desktop-portal-xapp"
	fi
}
