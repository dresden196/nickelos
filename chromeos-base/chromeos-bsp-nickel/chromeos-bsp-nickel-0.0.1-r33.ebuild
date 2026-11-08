# Distributed under the terms of the GNU General Public License v2

EAPI=7

inherit appid osreleased

DESCRIPTION="NickelOS board support: reven's hardware support minus Google services, plus NickelOS identity"
LICENSE="BSD-Google"
SLOT="0"
KEYWORDS="*"

S="${WORKDIR}"

# Same build deps as chromeos-bsp-reven.
DEPEND="
	sys-apps/busybox
	sys-apps/pv
"
# chromeos-bsp-reven's RDEPEND minus chromeos-base/flex_hwis (reports hardware
# details to Google), plus the firmware, audio DSP firmware and fonts that
# Google's private overlay supplies for ChromeOS Flex, and NVIDIA's driver for
# Turing and newer GPUs (nvidia-open; older ones keep nouveau) with its
# VA-API video decoding (nvidia-vaapi-driver), and the out-of-tree kernel
# modules apps expect: v4l2loopback (OBS's virtual camera), xpadneo (Xbox
# controllers over Bluetooth) and xonedo (the Xbox Wireless Adapter, its
# firmware fetched from Microsoft on first use).
RDEPEND="
	!<chromeos-base/gestures-conf-0.0.2
	chromeos-base/reven-hwdb
	chromeos-base/nickel-accounts
	chromeos-base/nickel-appd
	chromeos-base/nickel-cloud
	chromeos-base/nickel-desktop-base
	chromeos-base/nickel-portal
	chromeos-base/nickel-flatpak
	chromeos-base/nickel-guide
	chromeos-base/nickel-hostname
	chromeos-base/nickel-phone
	chromeos-base/nickel-session
	chromeos-base/nickel-ublock-origin
	chromeos-base/nickel-update
	chromeos-base/nickel-waydroid
	chromeos-base/nickel-userland
	app-i18n/nickel-mozc
	app-i18n/nickel-xim
	chromeos-base/nickel-xembed-tray
	gnome-base/gnome-keyring
	chromeos-base/nickel-x11-keys
	chromeos-base/reven-quirks
	dev-libs/libinput
	games-util/steam-devices
	games-util/xonedo
	games-util/xpadneo
	media-fonts/google-sans-code
	media-fonts/google-sans-flex
	media-sound/sound_card_init
	sys-firmware/fwupd-uefi-dbx
	sys-firmware/sof-firmware
	media-libs/nvidia-vaapi-driver
	media-video/v4l2loopback
	sys-kernel/linux-firmware-nickel
	x11-drivers/nvidia-open
	x11-themes/adwaita-cursors
"

src_install() {
	# Touchpad gesture configs, copied from chromeos-bsp-reven.
	insinto /etc/gesture
	doins "${FILESDIR}"/gesture/*

	# Setup's Full experience: Proton's apps as web apps, which Chrome
	# installs for the accounts of a device whose setup chose it (its
	# preinstalled web apps, gated on NickelFullExperience: Chrome 0089).
	# Each config carries the app's manifest (name, address, Proton's icons
	# from its web manifest, 2026-10-25): Proton's pages send a visitor to
	# the sign-in page, which has none, so Chrome installed nothing without
	# it, and with it the apps install offline, during setup. Proton's icons
	# name Proton's apps, as an OS's LibreOffice icon names LibreOffice.
	insinto /usr/share/chromium/extensions/web_apps
	doins "${FILESDIR}"/web_apps/*.json
	insinto /usr/share/chromium/extensions/web_apps/icons
	doins "${FILESDIR}"/web_apps/icons/*.png

	# The default wallpaper, the wave (branding/wallpaper/CREDITS.md,
	# make-default-jpgs.sh): the
	# session manager hands these to Chrome when they exist; ChromiumOS has
	# none, so the desktop was plain gray. Guests get it too.
	insinto /usr/share/chromeos-assets/wallpaper
	doins "${FILESDIR}"/wallpaper/default_{large,small}.jpg
	dosym default_large.jpg /usr/share/chromeos-assets/wallpaper/guest_large.jpg
	dosym default_small.jpg /usr/share/chromeos-assets/wallpaper/guest_small.jpg
	# Dark mode's twins (Dresden's darker grade of the same photo), which
	# Chrome shows in dark mode (Chrome 0112: default_X -> default_dark_X).
	doins "${FILESDIR}"/wallpaper/default_dark_{large,small}.jpg
	dosym default_dark_large.jpg /usr/share/chromeos-assets/wallpaper/guest_dark_large.jpg
	dosym default_dark_small.jpg /usr/share/chromeos-assets/wallpaper/guest_dark_small.jpg
	# NickelOS's wallpapers (branding/wallpaper/CREDITS.md): Wallpaper &
	# style's collection and the screen saver's (Chrome 0116), listed in
	# collection.json, with thumbnails for the app's grid (and preview.jpg to
	# preview-4.jpg, the default's and three more, for the screen saver's
	# card and tile, Chrome 0119).
	insinto /usr/share/chromeos-assets/wallpaper/nickel
	doins "${FILESDIR}"/wallpaper/nickel/collection.json "${FILESDIR}"/wallpaper/nickel/*.jpg
	insinto /usr/share/chromeos-assets/wallpaper/nickel/thumbs
	doins "${FILESDIR}"/wallpaper/nickel/thumbs/*.jpg

	# NickelOS's boot animation (branding/boot-splash/make-boot-splash.sh,
	# from Dresden's logo): frecon shows it while the system starts, as it
	# shows ChromiumOS's. Beside ChromiumOS's frames, which chromiumos-assets
	# owns; boot-splash.conf takes these when they're there (chromeos-init
	# 0003). splash_background is the screen's colour around them (black),
	# which boot-splash.conf reads from chromeos-assets, where nothing else
	# has one.
	insinto /usr/share/nickel/boot-splash
	doins -r "${FILESDIR}"/boot-splash/images_{100,200}_percent
	insinto /usr/share/chromeos-assets
	doins "${FILESDIR}"/boot-splash/splash_background

	# NickelOS's own Omaha app ID, so update_engine asks *our* update server
	# for *our* payloads (reven's is ChromeOS Flex's). DEVICETYPE OTHER: these
	# are generic PCs, not Chromebooks.
	doappid "{84A28531-7D33-44D5-AAA1-6CFA2BCF8652}" "OTHER"

	do_osrelease_field NAME "NickelOS"
	do_osrelease_field ID "nickelos"
	do_osrelease_field ID_LIKE "chromiumos"
	# ChromiumOS's own would send people to chromium.org and crbug. No
	# public bug tracker yet: the site.
	do_osrelease_field HOME_URL "https://nickelos.dev"
	do_osrelease_field BUG_REPORT_URL "https://nickelos.dev"
}
