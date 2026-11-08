# Copyright 1999-2026 Gentoo Authors
# Distributed under the terms of the GNU General Public License v2

EAPI=7

# NickelOS: ported from Gentoo dconf-0.49.0 (EAPI 8 -> 7): KEYWORDS="*", gdbus-codegen floor 2.80.5-r1 relaxed to ChromiumOS's 2.76.4, man pages (docbook), vapi and tests (virtualx) off, dropped the vapigen patch, dconf-service kill limited to ROOT=/.

inherit gnome.org gnome2-utils meson shell-completion systemd xdg

DESCRIPTION="Simple low-level configuration system"
HOMEPAGE="https://wiki.gnome.org/Projects/dconf"

LICENSE="LGPL-2.1+"
SLOT="0"
KEYWORDS="*"
IUSE="gtk-doc"
RESTRICT="test"

RDEPEND="
	>=dev-libs/glib-2.67.4.0:2
	sys-apps/dbus
"
DEPEND="${RDEPEND}"
BDEPEND="
	dev-util/gdbus-codegen
	gtk-doc? ( >=dev-util/gtk-doc-1.15 )
	>=sys-devel/gettext-0.19.8
	virtual/pkgconfig
"

PATCHES=(
	"${FILESDIR}"/0.49.0-bash-completion-dir.patch
)

src_configure() {
	local emesonargs=(
		-Dbash_completion_dir="$(get_bashcompdir)"
		-Dman=false
		$(meson_use gtk-doc gtk_doc)
		-Dvapi=false
		-Dsystemduserunitdir=$(systemd_get_userunitdir)
	)
	meson_src_configure
}

src_install() {
	meson_src_install

	# GSettings backend may be one of: memory, gconf, dconf
	# Only dconf is really considered functional by upstream
	# must have it enabled over gconf if both are installed
	# This snippet can't be removed until gconf package is
	# ensured to not install a /etc/env.d/50gconf and then
	# still consider the CONFIG_PROTECT_MASK bit.
	echo 'CONFIG_PROTECT_MASK="/etc/dconf"' >> 51dconf
	echo 'GSETTINGS_BACKEND="dconf"' >> 51dconf
	doenvd 51dconf
}

pkg_postinst() {
	xdg_pkg_postinst
	gnome2_giomodule_cache_update

	# Kill existing dconf-service processes as recommended by upstream due to
	# possible changes in the dconf private dbus API.
	# dconf-service will be dbus-activated on next use.
	if [[ ${ROOT:-/} == / ]]; then
		pids=$(pgrep -x dconf-service)
		if [[ $? == 0 ]]; then
			ebegin "Stopping dconf-service; it will automatically restart on demand"
			kill ${pids}
			eend $?
		fi
	fi
}

pkg_postrm() {
	xdg_pkg_postrm
	gnome2_giomodule_cache_update
}
