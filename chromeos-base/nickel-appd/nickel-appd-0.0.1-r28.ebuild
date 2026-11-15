# Distributed under the terms of the GNU General Public License v2

EAPI=7

inherit toolchain-funcs

DESCRIPTION="NickelOS: native Linux apps in Chrome's launcher and on its shelf (with their tray icons), launched on the host, and the Terminal app's shell"
LICENSE="GPL-2"
SLOT="0"
KEYWORDS="*"

S="${WORKDIR}"

# The app list is Google's vm_tools.apps.ApplicationList (system_api), the same
# message vm_cicerone sends Chrome for apps in a VM.
COMMON_DEPEND="
	chromeos-base/system_api:=
	dev-libs/glib:2
	dev-libs/protobuf:=
	sys-libs/zlib:=
"
DEPEND="${COMMON_DEPEND}"
# The thumbnailers Thumbnail runs (thumbnail.cc), inside bubblewrap.
RDEPEND="${COMMON_DEPEND}
	chromeos-base/nickel-session
	media-libs/libheif
	media-video/ffmpegthumbnailer
	sys-apps/bubblewrap
	x11-misc/xdg-utils
"
BDEPEND="virtual/pkgconfig"

src_compile() {
	local pc
	pc="$(tc-getPKG_CONFIG) gio-unix-2.0 protobuf-lite zlib"
	# shellcheck disable=SC2046 # pkg-config output is a list of flags
	$(tc-getCXX) ${CXXFLAGS} ${CPPFLAGS} -std=c++17 -Wall \
		$(${pc} --cflags) -o nickel-appd "${FILESDIR}"/nickel-appd.cc \
		"${FILESDIR}"/tray.cc "${FILESDIR}"/thumbnail.cc "${FILESDIR}"/autostart.cc \
		"${FILESDIR}"/location.cc "${FILESDIR}"/steam.cc \
		${LDFLAGS} -lsystem_api-vm_applications-protos $(${pc} --libs) || die
	pc="$(tc-getPKG_CONFIG) gio-unix-2.0"
	# shellcheck disable=SC2046 # pkg-config output is a list of flags
	$(tc-getCXX) ${CXXFLAGS} ${CPPFLAGS} -std=c++17 -Wall \
		$(${pc} --cflags) -o nickel-terminal-shell \
		"${FILESDIR}"/nickel-terminal-shell.cc ${LDFLAGS} $(${pc} --libs) || die
}

src_install() {
	dobin nickel-appd nickel-terminal-shell
	insinto /etc/init
	doins "${FILESDIR}"/init/nickel-appd.conf
	insinto /etc/dbus-1/system.d
	doins "${FILESDIR}"/dbus/org.nickelos.Apps.conf
	# Links and files from Linux apps open through ChromeOS.
	dobin "${FILESDIR}"/open/nickel-open
	insinto /usr/share/applications
	doins "${FILESDIR}"/open/org.nickelos.ChromeOS.desktop
	doins "${FILESDIR}"/open/nickelos-mimeapps.list
}
