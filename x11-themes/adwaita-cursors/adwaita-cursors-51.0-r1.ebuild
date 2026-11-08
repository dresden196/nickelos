# Copyright 2026 The NickelOS Authors
# Distributed under the terms of the GNU General Public License v2
# The cursor theme Linux apps fall back to: "default" inherits Adwaita.
# Apps that ask the compositor for a named cursor shape get ChromeOS's own;
# apps that draw their own cursor (X11 apps, GTK 3, Flatpak apps through
# /run/host/share/icons) found no theme at all and showed X's bare default
# arrow. Adwaita itself, cursors and their X core names included, is Gentoo's
# x11-themes/adwaita-icon-theme in NickelOS Unified (gentoo/install.txt):
# this package had installed its cursors too, and Gentoo's package, pulled in
# by firefox-bin, refused to overwrite them (2026-10-20).
EAPI=7
DESCRIPTION="NickelOS's default cursor theme for Linux apps: Adwaita's"
HOMEPAGE="https://nickelos.dev"
LICENSE="GPL-2"
SLOT="0"
KEYWORDS="*"
S="${WORKDIR}"
src_compile() { :; }
src_install() {
	insinto /usr/share/icons/default
	newins - index.theme <<-THEME
		[Icon Theme]
		Inherits=Adwaita
	THEME
}
