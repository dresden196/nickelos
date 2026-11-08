# Copyright 1999-2026 Gentoo Authors
# Distributed under the terms of the GNU General Public License v2

EAPI=7

# NickelOS: ported from Gentoo pipewire-1.6.8 (EAPI 8 -> 7): KEYWORDS="*", cut down to the client library (libpipewire-0.3 and what clients load) for xdg-desktop-portal, the daemon (pipewire, pw-cli, pw-dump, pipewire.conf and its modules) and its PulseAudio server (pipewire-pulse, pipewire-pulse.conf) for the session: screen sharing, and Linux apps' sound, which an ALSA sink and source carry into CRAS through its ALSA plugin (nickel-session); and webcams for Linux apps (V4L2, found through udev; the Camera portal). The session manager is media-video/wireplumber. No JACK/Bluetooth/libcamera, no ALSA device monitoring (CRAS owns the sound cards; WirePlumber does not monitor them), no dbus/systemd/elogind; meson-multilib -> meson, all USE flags and docs dropped, -Dauto_features=disabled so nothing is automagic.

inherit meson

SRC_URI="https://gitlab.freedesktop.org/${PN}/${PN}/-/archive/${PV}/${P}.tar.bz2"
KEYWORDS="*"

DESCRIPTION="Multimedia processing graphs"
HOMEPAGE="https://pipewire.org/"

LICENSE="MIT LGPL-2.1+ GPL-2"
# ABI was broken in 0.3.42 for https://gitlab.freedesktop.org/pipewire/wireplumber/-/issues/49
SLOT="0/0.4"
RESTRICT="test"

BDEPEND="
	>=dev-build/meson-0.59
	virtual/pkgconfig
"
# The ALSA plugin: the sink and source nickel-session gives CRAS. udev: how
# the V4L2 plugin finds webcams.
DEPEND="
	media-libs/alsa-lib
	virtual/libudev
"
RDEPEND="${DEPEND}"

src_configure() {
	local emesonargs=(
		-Ddocdir="${EPREFIX}"/usr/share/doc/${PF}

		# Everything not named below is off.
		-Dauto_features=disabled

		-Ddocs=disabled
		-Dman=disabled
		-Dexamples=disabled
		-Dtests=disabled
		-Dinstalled_tests=disabled
		-Dsession-managers="[]"

		# Client side only: the SPA support plugin (log, loop, system,
		# cpu) is what pw_context needs; no device plugins. audioconvert
		# has no external deps and the always-built filter-graph plugin
		# needs it at build time.
		-Dspa-plugins=enabled
		-Dsupport=enabled
		-Daudioconvert=enabled
		# Sound into CRAS: ALSA nodes on its ALSA plugin (its udev device
		# monitor stays unused: WirePlumber's hardware.audio is off), and
		# what audio graphs need.
		-Dalsa=enabled
		-Daudiomixer=enabled
		-Dcontrol=enabled
		-Daudiotestsrc=disabled
		# The adapter of every video stream loads it (screen sharing).
		-Dvideoconvert=enabled
		-Dvideotestsrc=disabled
		# Webcams for Linux apps: V4L2 nodes, which WirePlumber creates for
		# the cameras udev lists (nickel-session's wireplumber.conf.d) and
		# the Camera portal hands apps.
		-Dudev=enabled
		-Dv4l2=enabled
		-Dvolume=enabled
		-Dtest=disabled
		-Dbluez5-backend-hsp-native=disabled
		-Dbluez5-backend-hfp-native=disabled
		-Dbluez5-backend-ofono=disabled
		-Dbluez5-backend-hsphfpd=disabled

		-Dpipewire-jack=disabled
		-Dpipewire-v4l2=disabled
		-Djack-devel=false
		-Ddbus=disabled
		-Dflatpak=disabled
		-Dlogind=disabled
		-Dlegacy-rtkit=false
		-Dpam-defaults-install=false
		-Drlimits-install=false
	)

	meson_src_configure
}

src_install() {
	meson_src_install
	# The library, and the daemon and its PulseAudio server for the session
	# (nickel-session's nickel-pipewire and nickel-audio jobs): screen
	# sharing, and Linux apps' sound into CRAS. Keep them and two tools to
	# look at the graph; drop the other tools and every configuration but
	# the daemon's, the pulse server's and the clients'.
	local b
	for b in "${ED}"/usr/bin/*; do
		has "${b##*/}" pipewire pipewire-pulse pw-cli pw-dump || rm "${b}" || die
	done
	find "${ED}"/usr/share/pipewire -mindepth 1 -maxdepth 1 \
		! -name 'client.conf' ! -name 'client.conf.avail' \
		! -name 'pipewire.conf' ! -name 'pipewire.conf.avail' \
		! -name 'pipewire-pulse.conf' ! -name 'pipewire-pulse.conf.avail' \
		-exec rm -r {} + || die
	# Keep only the modules client.conf, pipewire.conf and
	# pipewire-pulse.conf load; the rest (RTP, filter-chain, echo-cancel,
	# ...) are not used.
	local keep=( rt protocol-native client-node client-device adapter metadata
		session-manager profiler spa-device-factory spa-node-factory access
		link-factory protocol-pulse )
	local m n
	for m in "${ED}"/usr/$(get_libdir)/pipewire-0.3/libpipewire-module-*.so; do
		n=${m##*/libpipewire-module-}
		n=${n%.so}
		has "${n}" "${keep[@]}" || rm "${m}" || die
	done
	# Only used by the filter-chain and echo-cancel modules removed above.
	rm -r "${ED}"/usr/$(get_libdir)/spa-0.2/{aec,filter-graph} || die
	# Properties for ALSA cards' profiles; CRAS owns the cards.
	rm -rf "${ED}"/lib/udev "${ED}"/usr/lib/udev || die
}
