# Copyright 2026 NickelOS
# Distributed under the terms of the GNU General Public License v2

EAPI=8

DESCRIPTION="NickelOS: the Gentoo a desktop distribution's programs expect"
HOMEPAGE="https://nickelos.dev"

LICENSE="metapackage"
SLOT="0"
KEYWORDS="amd64"

# What programs built elsewhere (AppImages, Electron, Steam, -bin packages)
# and Linux apps take from a desktop distribution, on top of Gentoo's
# @system; Portage's tools; tab completion.
RDEPEND="
	app-accessibility/at-spi2-core
	app-crypt/libsecret
	app-portage/eix
	app-portage/gentoolkit
	app-portage/portage-utils
	app-shells/bash-completion
	app-shells/gentoo-bashcomp
	dev-libs/nss
	dev-vcs/git
	gui-libs/gtk:4
	gui-libs/libadwaita
	media-libs/alsa-lib
	media-libs/fontconfig
	media-libs/libglvnd
	media-libs/libpulse
	media-libs/libsdl2
	media-libs/mesa
	media-libs/vulkan-loader
	media-video/pipewire
	net-misc/curl
	net-print/cups
	sys-fs/fuse:0
	sys-fs/fuse:3
	x11-libs/gtk+:3
	x11-libs/libnotify
	x11-libs/libXScrnSaver
	x11-libs/libXtst
	x11-libs/libxkbcommon
	x11-misc/xdg-utils
"
