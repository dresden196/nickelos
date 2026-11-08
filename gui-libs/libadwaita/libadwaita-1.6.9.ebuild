# Copyright 2022-2026 Gentoo Authors
# Distributed under the terms of the GNU General Public License v2

EAPI=7

# NickelOS: ported from Gentoo libadwaita-1.8.7 (EAPI 8 -> 7) at upstream 1.6.9: KEYWORDS="*". 1.6 is the newest series that builds on GLib 2.76 and GTK 4.16 (1.7 needs GLib 2.80 and GTK 4.17.5, 1.8 GTK 4.19.4); it has AdwStyleManager's accent colors (1.6). 1.6.9 is the last 1.6 release on ChromiumOS's Gentoo distfiles mirror (the build fetches with FEATURES=force-mirror; 1.6.10 only fixes a leak and accessibility roles). Gentoo's tree no longer has a 1.6 ebuild, so this is its 1.8.7 ebuild with 1.6's requirements (GLib 2.76, GTK 4.15.2); appstream is the port in this overlay.
# The release tarball has no compiled stylesheet: its SCSS is compiled at build time by sassc, which ChromiumOS's SDK does not have. The ebuild builds sassc and libsass (static) for the build machine from their release tarballs and puts sassc on PATH for the meson build; only the four CSS files of the stylesheet are made with it, nothing of it is installed. This replaces Gentoo's BDEPEND on dev-lang/sassc (two more ports, built into the board's build root) and keeps meson from falling back to its sassc subproject (a git download).
# Off: introspection (ChromiumOS's gobject-introspection is 1.72), vala, gtk-doc, examples (adwaita-1-demo), tests, profiling; so no Python BDEPEND.

inherit gnome.org meson toolchain-funcs xdg

LIBSASS_PV="3.6.6"
SASSC_PV="3.6.2"

DESCRIPTION="Building blocks for modern GNOME applications"
HOMEPAGE="https://gnome.pages.gitlab.gnome.org/libadwaita/ https://gitlab.gnome.org/GNOME/libadwaita"
SRC_URI+="
	https://github.com/sass/libsass/archive/${LIBSASS_PV}.tar.gz -> libsass-${LIBSASS_PV}.tar.gz
	https://github.com/sass/sassc/archive/${SASSC_PV}.tar.gz -> sassc-${SASSC_PV}.tar.gz
"

LICENSE="LGPL-2.1+"
SLOT="1"
KEYWORDS="*"
RESTRICT="test"

RDEPEND="
	>=dev-libs/glib-2.76.0:2
	>=gui-libs/gtk-4.15.2:4
	dev-libs/appstream:=
	dev-libs/fribidi
"
DEPEND="${RDEPEND}"
BDEPEND="
	dev-util/glib-utils
	sys-devel/gettext
	virtual/pkgconfig
"

src_prepare() {
	default
	xdg_environment_reset
}

# Builds sassc (with libsass linked in statically) for the build machine.
# The flags go in through the environment: the Makefiles append their own
# (-std=c++11, -fPIC, version defines) to it, which make arguments would
# override.
nickel_build_sassc() {
	local sassc_dir="${WORKDIR}/sassc-${SASSC_PV}"

	env \
		CC="$(tc-getBUILD_CC)" \
		CXX="$(tc-getBUILD_CXX)" \
		AR="$(tc-getBUILD_AR)" \
		CFLAGS="${BUILD_CFLAGS:--O2}" \
		CXXFLAGS="${BUILD_CXXFLAGS:--O2}" \
		LDFLAGS="${BUILD_LDFLAGS}" \
		LIBSASS_VERSION="${LIBSASS_PV}" \
		SASSC_VERSION="${SASSC_PV}" \
		SASS_LIBSASS_PATH="${WORKDIR}/libsass-${LIBSASS_PV}" \
		BUILD=static \
		emake -C "${sassc_dir}" || die

	"${sassc_dir}"/bin/sassc --version || die "sassc does not run"
	export PATH="${sassc_dir}/bin:${PATH}"
}

src_configure() {
	nickel_build_sassc

	local emesonargs=(
		-Dprofiling=false
		-Dintrospection=disabled
		-Dvapi=false
		-Dgtk_doc=false
		-Dtests=false
		-Dexamples=false
	)
	meson_src_configure
}
