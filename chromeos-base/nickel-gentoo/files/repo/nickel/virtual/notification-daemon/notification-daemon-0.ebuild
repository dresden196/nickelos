# Copyright 2026 NickelOS
# Distributed under the terms of the GNU General Public License v2

EAPI=8

DESCRIPTION="NickelOS: desktop notifications are NickelOS's own"
HOMEPAGE="https://nickelos.dev"

LICENSE="metapackage"
SLOT="0"
KEYWORDS="amd64"

# nickel-portal, part of the OS, owns org.freedesktop.Notifications and shows
# Linux apps' notifications as ChromeOS's. Gentoo's virtual (masked in
# /etc/portage/package.mask/nickel-ours) pulled in Gentoo's
# notification-daemon for VLC and libnotify's users, and that daemon's D-Bus
# service file is nickel-portal's (2026-10-20). A profile's package.provided
# said so first, but EAPI 8 profiles can't have one: Portage ignored it.
