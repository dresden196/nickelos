# Distributed under the terms of the GNU General Public License v2

EAPI=7

DESCRIPTION="NickelOS per-login session for native Linux apps: runtime dir, Wayland, X11, PipeWire (sound into CRAS, screen sharing, webcams), session D-Bus"
LICENSE="GPL-2"
SLOT="0"
KEYWORDS="*"

S="${WORKDIR}"

# sommelier pulls in Xwayland. libpulse: native PulseAudio apps' client
# library (the server is pipewire-pulse).
RDEPEND="
	chromeos-base/minijail
	chromeos-base/nickel-logind
	chromeos-base/sommelier
	>=media-video/pipewire-1.6.8-r3
	media-video/wireplumber
	media-libs/libpulse
	chromeos-base/nickel-gentoo
	sys-apps/dbus
	x11-apps/xauth
"

src_install() {
	insinto /etc/init
	doins "${FILESDIR}"/init/nickel-session.conf "${FILESDIR}"/init/nickel-x11.conf \
		"${FILESDIR}"/init/nickel-audio.conf "${FILESDIR}"/init/nickel-pipewire.conf \
		"${FILESDIR}"/init/nickel-wireplumber.conf
	insinto /usr/share/pipewire/pipewire.conf.d
	doins "${FILESDIR}"/pipewire/nickel-cras.conf
	insinto /usr/share/wireplumber/wireplumber.conf.d
	doins "${FILESDIR}"/wireplumber/90-nickel.conf
	exeinto /usr/libexec/nickel-session
	doexe "${FILESDIR}"/nickel-x11 "${FILESDIR}"/nickel-user-dirs \
		"${FILESDIR}"/nickel-home-move "${FILESDIR}"/nickel-session-locale
	dosbin "${FILESDIR}"/nickel-session-setup "${FILESDIR}"/nickel-session-exec \
		"${FILESDIR}"/nickel-session-cleanup
	insinto /etc/profile.d
	doins "${FILESDIR}"/profile.d/nickel-session.sh
}
