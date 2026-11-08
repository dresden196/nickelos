# Copyright 1999-2026 Gentoo Authors
# Distributed under the terms of the GNU General Public License v2

EAPI=7

# NickelOS: ported from Gentoo pulseaudio-daemon-17.0-r2 (EAPI 8 -> 7):
# KEYWORDS="*". A per-session sound server for Flatpak apps, playing into and
# recording from CRAS through its ALSA plugin (the flags NickelOS uses are in
# profiles/base/package.use). Mapped to ChromiumOS: speexdsp comes from
# media-libs/speex (1.2rc1, which still ships libspeexdsp and speexdsp.pc);
# dev-libs/libatomic_ops is dropped (not in ChromiumOS; meson only needs it
# when the compiler lacks the __sync/__atomic builtins). Dropped: the
# system-wide flag with its OpenRC/systemd/tmpfiles files (NickelOS runs it
# per user), the client.conf.d snippet that turns autospawn back on, man pages
# (and with them the perl/XML-Parser BDEPEND), tests and the test-only patch
# (MR 812). The pipewire blocker takes the sound-server flag as off when
# missing: this overlay's pipewire is the client library only. Added patch:
# the NEON mixers are built for 32-bit ARM only (ChromiumOS clang accepts
# -mfpu=neon on x86_64, so meson's check enabled ARM assembly there).

MY_PV="${PV/_pre*}"
MY_P="pulseaudio-${MY_PV}"
inherit bash-completion-r1 gnome2-utils meson optfeature systemd udev

DESCRIPTION="Daemon component of PulseAudio (networked sound server)"
HOMEPAGE="https://www.freedesktop.org/wiki/Software/PulseAudio/"

SRC_URI="https://freedesktop.org/software/pulseaudio/releases/${MY_P}.tar.xz"
KEYWORDS="*"

S="${WORKDIR}/${MY_P}"

# libpulse-simple and libpulse link to libpulse-core; this is daemon's
# library and can link to gdbm and other GPL-only libraries. In this
# cases, we have a fully GPL-2 package. Leaving the rest of the
# GPL-forcing USE flags for those who use them.
LICENSE="!gdbm? ( LGPL-2.1 ) gdbm? ( GPL-2 )"

SLOT="0"

# +alsa-plugin as discussed in bug #519530
# TODO: Find out why webrtc-aec is + prefixed - there's already the always available speexdsp-aec
# NOTE: The current ebuild sets +X almost certainly just for the pulseaudio.desktop file
IUSE="+alsa +alsa-plugin aptx +asyncns bluetooth dbus elogind equalizer fftw +gdbm +glib gstreamer jack ldac lirc
ofono-headset +orc oss selinux sox ssl systemd tcpd +udev valgrind +webrtc-aec +X zeroconf"

RESTRICT="test"

# See "*** BLUEZ support not found (requires D-Bus)" in configure.ac
REQUIRED_USE="
	?? ( elogind systemd )
	alsa-plugin? ( alsa )
	aptx? ( bluetooth )
	bluetooth? ( dbus )
	equalizer? ( dbus )
	ldac? ( bluetooth )
	ofono-headset? ( bluetooth )
	udev? ( || ( alsa oss ) )
	zeroconf? ( dbus )
"

# NOTE:
# - libpcre needed in some cases, bug #472228
# - media-libs/speex provides libspeexdsp: echo canceller and resampler
gstreamer_deps="
	media-libs/gst-plugins-base
	>=media-libs/gstreamer-1.14
"
COMMON_DEPEND="
	~media-libs/libpulse-${PV}[dbus?,glib?,systemd?,valgrind?,X?]
	>=media-libs/libpulse-16.99.1
	>=media-libs/libsndfile-1.0.20
	media-libs/speex
	alsa? ( >=media-libs/alsa-lib-1.0.24 )
	aptx? ( ${gstreamer_deps} )
	asyncns? ( >=net-libs/libasyncns-0.1 )
	bluetooth? (
		>=net-wireless/bluez-5
		media-libs/sbc
	)
	dev-libs/libltdl
	sys-kernel/linux-headers
	>=sys-libs/libcap-2.22-r2
	dbus? ( >=sys-apps/dbus-1.4.12 )
	elibc_mingw? ( dev-libs/libpcre:3 )
	elogind? ( sys-auth/elogind )
	equalizer? (
		sci-libs/fftw:3.0=
	)
	fftw? (
		sci-libs/fftw:3.0=
	)
	gdbm? ( sys-libs/gdbm:= )
	glib? ( >=dev-libs/glib-2.28.0:2 )
	gstreamer? (
		${gstreamer_deps}
		>=dev-libs/glib-2.26.0:2
	)
	jack? ( virtual/jack )
	ldac? ( ${gstreamer_deps} )
	lirc? ( app-misc/lirc )
	ofono-headset? ( >=net-misc/ofono-1.13 )
	orc? ( >=dev-lang/orc-0.4.15 )
	selinux? ( sec-policy/selinux-pulseaudio )
	sox? ( >=media-libs/soxr-0.1.1 )
	ssl? ( dev-libs/openssl:= )
	systemd? ( sys-apps/systemd:= )
	tcpd? ( sys-apps/tcp-wrappers )
	udev? ( >=virtual/udev-143[hwdb(+)] )
	valgrind? ( dev-debug/valgrind )
	webrtc-aec? ( >=media-libs/webrtc-audio-processing-1.3:1 )
	X? (
		>=x11-libs/libxcb-1.6
		x11-libs/libICE
		x11-libs/libSM
		>=x11-libs/libX11-1.4.0
		>=x11-libs/libXtst-1.0.99.2
	)
	zeroconf? ( >=net-dns/avahi-0.6.12[dbus] )
"

DEPEND="
	${COMMON_DEPEND}
	X? ( x11-base/xorg-proto )
"

RDEPEND="
	${COMMON_DEPEND}
	bluetooth? (
		ldac? ( media-plugins/gst-plugins-ldac )
		aptx? ( media-plugins/gst-plugins-openaptx )
	)
	!media-video/pipewire[sound-server(-)]
"
unset gstreamer_deps

# This is a PDEPEND to avoid a circular dep
PDEPEND="
	alsa? ( alsa-plugin? ( >=media-plugins/alsa-plugins-1.0.27-r1[pulseaudio] ) )
"

BDEPEND="
	sys-devel/gettext
	sys-devel/m4
	virtual/libiconv
	virtual/libintl
	virtual/pkgconfig
	orc? ( >=dev-lang/orc-0.4.15 )
"

DOCS=( NEWS README )

PATCHES=(
	"${FILESDIR}"/pulseaudio-17.0-fix_libpulse_libdir.patch

	# patches merged upstream, to be removed with 17.1 or later bump
	"${FILESDIR}"/pulseaudio-17.0-pr801.patch
	"${FILESDIR}"/pulseaudio-17.0-pr802.patch

	# NickelOS: ChromiumOS clang takes -mfpu=neon on x86_64 too
	"${FILESDIR}"/pulseaudio-17.0-neon-arm-only.patch
)

src_prepare() {
	default

	gnome2_environment_reset
}

src_configure() {
	local enable_bluez5_gstreamer="disabled"
	if use aptx || use ldac ; then
		enable_bluez5_gstreamer="enabled"
	fi

	local enable_fftw="disabled"
	if use equalizer || use fftw ; then
		enable_fftw="enabled"
	fi

	local emesonargs=(
		--localstatedir="${EPREFIX}"/var

		-Ddaemon=true
		-Dclient=false
		-Ddoxygen=false
		-Dgcov=false
		-Dman=false
		-Dtests=false
		-Ddatabase=$(usex gdbm gdbm simple) # tdb is also an option but no one cares about it
		-Dstream-restore-clear-old-devices=true
		-Drunning-from-build-tree=false

		# Paths
		-Dmodlibexecdir="${EPREFIX}/usr/$(get_libdir)/pulseaudio/modules" # Was $(get_libdir)/${P}
		-Dprivlibdir_system="${ESYSROOT}/usr/$(get_libdir)/pulseaudio"
		-Dsystemduserunitdir=$(systemd_get_userunitdir)
		-Dudevrulesdir="${EPREFIX}$(get_udevdir)/rules.d"
		# Alternatively DEPEND on app-shells/bash-completion for pkg-config to provide the value
		-Dbashcompletiondir="$(get_bashcompdir)"

		# Optional features
		$(meson_feature alsa)
		$(meson_feature asyncns)
		$(meson_feature zeroconf avahi)
		$(meson_feature bluetooth bluez5)
		-Dbluez5-gstreamer=${enable_bluez5_gstreamer}
		$(meson_use bluetooth bluez5-native-headset)
		$(meson_use ofono-headset bluez5-ofono-headset)
		-Dconsolekit=disabled
		$(meson_feature dbus)
		$(meson_feature elogind)
		-Dfftw=${enable_fftw}
		$(meson_feature glib) # WARNING: toggling this likely changes ABI
		$(meson_feature glib gsettings) # Supposedly correct?
		$(meson_feature gstreamer)
		-Dgtk=disabled
		-Dhal-compat=false
		-Dipv6=true
		$(meson_feature jack)
		$(meson_feature lirc)
		$(meson_feature ssl openssl)
		$(meson_feature orc)
		$(meson_feature oss oss-output)
		-Dsamplerate=disabled # Matches upstream
		$(meson_feature sox soxr)
		-Dspeex=enabled
		$(meson_feature systemd)
		$(meson_feature tcpd tcpwrap)
		$(meson_feature udev)
		$(meson_feature valgrind)
		$(meson_feature X x11)

		# Echo cancellation
		-Dadrian-aec=false # Not packaged?
		$(meson_feature webrtc-aec)
	)

	meson_src_configure
}

src_install() {
	meson_src_install

	# qpaeq moved to media-sound/qpaeq
	if [[ -f "${ED}"/usr/bin/qpaeq ]]; then
		rm "${ED}"/usr/bin/qpaeq || die
	fi

	# Upstream installs 'pactl' if client is built, with all symlinks except for
	# 'pulseaudio', 'pacmd' and 'pasuspender' which are installed if server is built.
	# This trips QA warning, workaround:
	# - install missing aliases in media-libs/libpulse (client build)
	# - remove corresponding symlinks in media-sound/pulseaudio-daemonclient (server build)
	rm "${D}/$(get_bashcompdir)"/pulseaudio || die
	rm "${D}/$(get_bashcompdir)"/pacmd || die
	rm "${D}/$(get_bashcompdir)"/pasuspender || die

	# Daemon configuration scripts will try to load snippets from corresponding '.d' dirs.
	# Install these dirs to silence a warning if they are missing.
	keepdir /etc/pulse/default.pa.d
	keepdir /etc/pulse/system.pa.d

	# Prevent warnings when system-wide is not used, bug 447694
	if use dbus; then
		rm "${ED}"/usr/share/dbus-1/system.d/pulseaudio-system.conf || die
	fi

	if use zeroconf; then
		sed -i \
			-e '/module-zeroconf-publish/s:^#::' \
			"${ED}/etc/pulse/default.pa" \
			|| die
	fi

	find "${ED}" \( -name '*.a' -o -name '*.la' \) -delete || die
}

pkg_postinst() {
	gnome2_schemas_update

	use udev && udev_reload

	optfeature_header "PulseAudio can be enhanced by installing the following:"
	use equalizer && optfeature "qpaeq script for equalizer GUI" media-sound/qpaeq
	use dbus && optfeature "restricted realtime capabilities via D-Bus" sys-auth/rtkit
}

pkg_postrm() {
	gnome2_schemas_update
	use udev && udev_reload
}
