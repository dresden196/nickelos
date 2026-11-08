# Copyright 1999-2025 Gentoo Authors
# Distributed under the terms of the GNU General Public License v2

EAPI=7

# NickelOS: ported from Gentoo polkit-gnome-0.105-r2: KEYWORDS="*". The XDG autostart entry is not installed: ChromeOS sessions do not run XDG autostart, so the NickelOS session starts the agent (/usr/libexec/polkit-gnome-authentication-agent-1) itself.

inherit gnome.org

DESCRIPTION="A dbus session bus service that is used to bring up authentication dialogs"
HOMEPAGE="https://gitlab.freedesktop.org/polkit/polkit/"

LICENSE="LGPL-2"
SLOT="0"
KEYWORDS="*"

DEPEND=">=dev-libs/glib-2.30
	>=sys-auth/polkit-0.102
	x11-libs/gtk+:3"
RDEPEND="${DEPEND}"
BDEPEND="dev-util/intltool
	virtual/pkgconfig
	sys-devel/gettext"

DOCS=( AUTHORS HACKING NEWS README TODO )
