# Distributed under the terms of the GNU General Public License v2

EAPI=7

inherit toolchain-funcs

DESCRIPTION="NickelOS backend for xdg-desktop-portal: ChromeOS's appearance, file picker, screen sharing, notifications, global shortcuts, camera permission, keeping the screen on and printing for Linux apps"
LICENSE="GPL-2"
SLOT="0"
KEYWORDS="*"

S="${WORKDIR}"

# Chrome reports the appearance to nickel-appd, which this asks. File dialogs
# are Chrome's, requested with Google's vm_tools.apps.SelectFileRequest
# (system_api), the message Crostini's apps use. Notifications (also
# org.freedesktop.Notifications for native apps) go to Chrome with their
# icons as PNG (gdk-pixbuf).
COMMON_DEPEND="
	chromeos-base/system_api:=
	dev-libs/glib:2
	x11-libs/gdk-pixbuf:2
	dev-libs/protobuf:=
	media-video/pipewire:=
	x11-libs/libxkbcommon
"
DEPEND="${COMMON_DEPEND}"
RDEPEND="${COMMON_DEPEND}
	chromeos-base/nickel-appd
	sys-apps/xdg-desktop-portal
"
BDEPEND="virtual/pkgconfig"

src_compile() {
	local pc
	pc="$(tc-getPKG_CONFIG) gio-unix-2.0 gdk-pixbuf-2.0 protobuf-lite libpipewire-0.3 xkbcommon"
	# shellcheck disable=SC2046 # pkg-config output is a list of flags
	$(tc-getCXX) ${CXXFLAGS} ${CPPFLAGS} -std=c++17 -Wall \
		$(${pc} --cflags) -I"${FILESDIR}" -o xdg-desktop-portal-nickel \
		"${FILESDIR}"/xdg-desktop-portal-nickel.cc "${FILESDIR}"/screencast.cc \
		"${FILESDIR}"/notifications.cc "${FILESDIR}"/globalshortcuts.cc \
		"${FILESDIR}"/access.cc "${FILESDIR}"/inhibit.cc "${FILESDIR}"/print.cc \
		"${FILESDIR}"/screenshot.cc \
		"${FILESDIR}"/background.cc "${FILESDIR}"/account.cc \
		"${FILESDIR}"/email.cc "${FILESDIR}"/dynamic_launcher.cc \
		"${FILESDIR}"/wallpaper.cc \
		${LDFLAGS} -lsystem_api-vm_applications-protos $(${pc} --libs) || die
}

src_install() {
	exeinto /usr/libexec
	doexe xdg-desktop-portal-nickel
	insinto /usr/share/xdg-desktop-portal/portals
	doins "${FILESDIR}"/portal/nickel.portal
	insinto /usr/share/xdg-desktop-portal
	doins "${FILESDIR}"/portal/nickelos-portals.conf
	insinto /usr/share/dbus-1/services
	doins "${FILESDIR}"/portal/org.freedesktop.impl.portal.desktop.nickel.service
	# Native apps' notifications start it too.
	doins "${FILESDIR}"/portal/org.freedesktop.Notifications.service
	# Native apps keeping the screen on start it too.
	doins "${FILESDIR}"/portal/org.freedesktop.ScreenSaver.service
}
