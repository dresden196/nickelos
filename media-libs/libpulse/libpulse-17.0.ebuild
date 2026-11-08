# Copyright 1999-2025 Gentoo Authors
# Distributed under the terms of the GNU General Public License v2

EAPI=7

# NickelOS: ported from Gentoo libpulse-17.0 (already EAPI 7): KEYWORDS="*";
# meson-multilib -> meson (one ABI). Here only to give pulseaudio-daemon its
# libpulse/libpulsecommon (Flatpak apps bring their own libpulse). Doxygen,
# man pages (and with them the perl/XML-Parser BDEPEND) and tests are off, so
# the doc and test flags and the test-only patch (MR 807) are dropped.
# dev-libs/libatomic_ops is dropped: it is not in ChromiumOS, and meson only
# needs it when the compiler lacks the __sync/__atomic builtins. The PDEPEND
# takes pipewire's sound-server flag as off when missing, because this
# overlay's pipewire is the client library only and has no such flag.

MY_PV="${PV/_pre*}"
MY_P="pulseaudio-${MY_PV}"
inherit bash-completion-r1 flag-o-matic gnome2-utils meson optfeature systemd udev

DESCRIPTION="Libraries for PulseAudio clients"
HOMEPAGE="https://www.freedesktop.org/wiki/Software/PulseAudio/"

SRC_URI="https://freedesktop.org/software/pulseaudio/releases/${MY_P}.tar.xz"
KEYWORDS="*"

S="${WORKDIR}/${MY_P}"

LICENSE="LGPL-2.1+"

SLOT="0"
IUSE="+asyncns dbus +glib gtk selinux systemd valgrind X"
RESTRICT="test"

# NOTE: libpcre needed in some cases, bug #472228
RDEPEND="
	>=media-libs/libsndfile-1.0.20
	asyncns? ( >=net-libs/libasyncns-0.1 )
	dbus? ( >=sys-apps/dbus-1.4.12 )
	elibc_mingw? ( dev-libs/libpcre:3 )
	glib? ( >=dev-libs/glib-2.28.0:2 )
	gtk? ( x11-libs/gtk+:3 )
	selinux? ( sec-policy/selinux-pulseaudio )
	systemd? ( sys-apps/systemd:= )
	valgrind? ( dev-debug/valgrind )
	X? (
		x11-libs/libX11
		>=x11-libs/libxcb-1.6
	)
	!<media-sound/pulseaudio-16.1
	!<media-sound/pulseaudio-daemon-16.99.1
"

DEPEND="${RDEPEND}
	X? ( x11-base/xorg-proto )
"

BDEPEND="
	sys-devel/gettext
	sys-devel/m4
	virtual/libiconv
	virtual/libintl
	virtual/pkgconfig
"
PDEPEND="
	|| (
		media-video/pipewire[sound-server(-)]
		media-sound/pulseaudio-daemon
	)
"

DOCS=( NEWS README )

src_prepare() {
	default

	# disable autospawn by client
	sed -i -e 's:; autospawn = yes:autospawn = no:g' src/pulse/client.conf.in || die

	gnome2_environment_reset
}

src_configure() {
	append-ldflags $(test-flags-CCLD -Wl,--undefined-version)

	local emesonargs=(
		--localstatedir="${EPREFIX}"/var

		-Ddaemon=false
		-Dclient=true
		-Ddoxygen=false
		-Dgcov=false
		-Dman=false
		-Dtests=false
		-Ddatabase=simple # Not used for non-daemon, simple database avoids external dep checks
		-Dstream-restore-clear-old-devices=true
		-Drunning-from-build-tree=false

		# Paths
		-Dmodlibexecdir="${EPREFIX}/usr/$(get_libdir)/pulseaudio/modules" # Was $(get_libdir)/${P}
		-Dsystemduserunitdir=$(systemd_get_userunitdir)
		-Dudevrulesdir="${EPREFIX}$(get_udevdir)/rules.d"
		-Dbashcompletiondir="$(get_bashcompdir)" # Alternatively DEPEND on app-shells/bash-completion for pkg-config to provide the value

		# Optional features
		-Dalsa=disabled
		$(meson_feature asyncns)
		-Davahi=disabled
		-Dbluez5=disabled
		-Dbluez5-gstreamer=disabled
		-Dbluez5-native-headset=false
		-Dbluez5-ofono-headset=false
		-Dconsolekit=disabled
		$(meson_feature dbus)
		-Delogind=disabled
		-Dfftw=disabled
		$(meson_feature glib) # WARNING: toggling this likely changes ABI
		-Dgsettings=disabled
		-Dgstreamer=disabled
		$(meson_feature gtk)
		-Dhal-compat=false
		-Dipv6=true
		-Djack=disabled
		-Dlirc=disabled
		-Dopenssl=disabled
		-Dorc=disabled
		-Doss-output=disabled
		-Dsamplerate=disabled # Matches upstream
		-Dsoxr=disabled
		-Dspeex=disabled
		$(meson_feature systemd)
		-Dtcpwrap=disabled
		-Dudev=disabled
		$(meson_feature valgrind)
		$(meson_feature X x11)

		# Echo cancellation
		-Dadrian-aec=false
		-Dwebrtc-aec=disabled
	)

	meson_src_configure
}

src_install() {
	meson_src_install

	# Upstream installs 'pactl' if client is built, with all symlinks except for
	# 'pulseaudio', 'pacmd' and 'pasuspender' which are installed if server is built.
	# This trips QA warning, workaround:
	# - install missing aliases in media-libs/libpulse (client build)
	# - remove corresponding symlinks in media-sound/pulseaudio-daemonclient (server build)
	bashcomp_alias pactl pulseaudio
	bashcomp_alias pactl pacmd
	bashcomp_alias pactl pasuspender

	find "${ED}" \( -name '*.a' -o -name '*.la' \) -delete || die
}

pkg_postinst() {
	optfeature_header "PulseAudio can be enhanced by installing the following:"
	use dbus && optfeature "restricted realtime capabilities via D-Bus" sys-auth/rtkit
}
