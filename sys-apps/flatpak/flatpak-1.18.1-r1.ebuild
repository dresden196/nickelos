# Copyright 2020-2026 Gentoo Authors
# Distributed under the terms of the GNU General Public License v2

EAPI=7

# NickelOS: ported from Gentoo flatpak-1.18.1 (EAPI 8 -> 7): KEYWORDS="*", gdbus-codegen floor 2.80.5-r1 relaxed to ChromiumOS's 2.76.4, dropped tests (and their patch and socat/polkit deps; socat is only used by the test suite), docs, gtk-doc and the leftover automake BDEPEND; app-alternatives/yacc -> sys-devel/bison; fusermount3 path passed explicitly (cross build cannot find it on the SDK); new wayland flag for the wayland security-context support (was automagic); man pages and malcontent explicitly off; msgfmt pointed at the sysroot for polkit ITS rules.

PYTHON_COMPAT=( python3_{11..14} )

inherit linux-info meson python-any-r1 systemd tmpfiles

DESCRIPTION="Linux application sandboxing and distribution framework"
HOMEPAGE="https://flatpak.org/"
SRC_URI="https://github.com/${PN}/${PN}/releases/download/${PV}/${P}.tar.xz"

LICENSE="LGPL-2.1+"
SLOT="0"
KEYWORDS="*"
IUSE="introspection policykit seccomp systemd wayland X"
RESTRICT="test"

RDEPEND="
	acct-group/flatpak
	acct-user/flatpak
	>=app-arch/libarchive-2.8:=
	app-arch/zstd:=
	>=app-crypt/gpgme-1.1.8:=
	>=dev-libs/appstream-0.12:=
	>=dev-libs/glib-2.56:2
	>=dev-libs/libxml2-2.4:=
	dev-libs/json-glib
	dev-libs/libassuan:=
	>=dev-util/ostree-2020.8:=[gpg(+)]
	dev-util/ostree[curl]
	>=gnome-base/dconf-0.26
	gnome-base/gsettings-desktop-schemas
	net-misc/curl
	>=sys-apps/bubblewrap-0.10.0
	sys-apps/dbus
	>=sys-fs/fuse3-3.1.1:3=[suid]
	sys-apps/xdg-dbus-proxy
	sys-libs/libcap
	policykit? ( sys-auth/polkit )
	X? (
		x11-apps/xauth
		x11-libs/libXau:=
	)
	x11-libs/gdk-pixbuf:2=
	seccomp? ( sys-libs/libseccomp )
	systemd? ( sys-apps/systemd )
	wayland? (
		dev-libs/wayland
		>=dev-libs/wayland-protocols-1.32
	)
"

DEPEND="${RDEPEND}"
BDEPEND="
	>=sys-devel/gettext-0.18.2
	virtual/pkgconfig
	dev-util/gdbus-codegen
	dev-util/glib-utils
	sys-devel/bison
	$(python_gen_any_dep 'dev-python/pyparsing[${PYTHON_USEDEP}]')
	introspection? ( dev-libs/gobject-introspection )
	wayland? ( dev-util/wayland-scanner )
"

PDEPEND="sys-apps/xdg-desktop-portal"

python_check_deps() {
	python_has_version "dev-python/pyparsing[${PYTHON_USEDEP}]"
}

pkg_setup() {
	local CONFIG_CHECK="~USER_NS"
	linux-info_pkg_setup
	python-any-r1_pkg_setup
}

PATCHES=(
	# NickelOS: bubblewrap 0.12 does not follow the sandbox's /var/run symlink.
	"${FILESDIR}"/${P}-cups-socket-in-run.patch
)

src_prepare() {
	default

	# QA Notice: The ebuild is installing to one or more unexpected directories:
	# /usr/share/doc/flatpak
	sed -i "s|docdir = get_option('datadir') / 'doc' / 'flatpak'|docdir = get_option('datadir') / 'flatpak'|g" \
		meson.build || die
}

src_configure() {
	local emesonargs=(
		--localstatedir="${EPREFIX}"/var
		-Ddbus_config_dir=/usr/share/dbus-1/system.d
		-Dsystem_bubblewrap=bwrap
		-Dsystem_dbus_proxy=xdg-dbus-proxy
		-Dsystem_fusermount=/usr/bin/fusermount3
		-Dtmpfilesdir=/usr/lib/tmpfiles.d
		-Dtests=false
		-Dinstalled_tests=false
		$(meson_feature policykit system_helper)
		$(meson_feature introspection gir)
		$(meson_feature X xauth)
		-Ddocbook_docs=disabled
		-Dgtkdoc=disabled
		-Dman=disabled
		-Dmalcontent=disabled
		-Dselinux_module=disabled
		-Ddconf=enabled
		-Dlibzstd=enabled
		$(meson_feature seccomp seccomp)
		$(meson_feature systemd systemd)
		$(meson_feature wayland wayland_security_context)
	)

	meson_src_configure
}

src_compile() {
	# NickelOS: msgfmt needs polkit's ITS rules to merge the .policy
	# translations; they are in the sysroot, not in the SDK.
	local -x GETTEXTDATADIRS="${ESYSROOT}/usr/share/gettext"
	meson_src_compile
}

src_install() {
	meson_src_install
	# https://projects.gentoo.org/qa/policy-guide/installed-files.html#pg0303
	find "${ED}" -name '*.la' -delete || die
	# resolve conflict with acct-user/flatpak for #856706
	rm -rf "${ED}/usr/lib/sysusers.d"
}

pkg_postinst() {
	tmpfiles_process flatpak.conf
}
