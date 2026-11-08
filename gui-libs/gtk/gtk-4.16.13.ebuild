# Copyright 2023-2026 Gentoo Authors
# Distributed under the terms of the GNU General Public License v2

EAPI=7

# NickelOS: ported from Gentoo gtk-4.18.6-r1 (EAPI 8 -> 7) at upstream 4.16.13: KEYWORDS="*", single ABI. 4.16 is the newest GTK 4 series that builds on ChromiumOS R156's GLib 2.76.4: 4.18 needs GLib 2.80, pango 1.56, wayland 1.23 and wayland-protocols 1.41 (R156: pango 1.54, wayland 1.22, wayland-protocols 1.36), 4.20 and later GLib 2.82; Gentoo's tree no longer has a 4.16 ebuild, so this is its 4.18.6 ebuild with 4.16's requirements (GLib 2.76, pango 1.52, cairo 1.18.0, harfbuzz 2.6, wayland 1.21, wayland-protocols 1.36), all met by portage-stable/chromiumos-overlay. graphene and libXinerama (X11 backend) are ported in this overlay.
# Backends: Wayland and X11, as for GTK 3 (profiles/base/package.use). Wayland EGL comes from dev-libs/wayland's libwayland-egl, not mesa[wayland]. mesa-reven is built without EGL platforms and without Vulkan drivers, so GTK's GL renderer cannot start on Exo and GSK uses its Cairo (software) renderer, as GTK 3 does; Vulkan is off (it would also need glslc/shaderc in the SDK). f16c is on: GSK picks the F16C code at run time (__builtin_cpu_supports), so it is safe on CPUs without it.
# Off: broadway, GStreamer media backend, CPDB printing, cloudproviders, colord, sysprof, tracker, introspection (ChromiumOS's gobject-introspection is 1.72, GTK 4.16 needs 1.76), docs, man pages (docutils), demos, examples, tests. CUPS stays optional: libcups is already on the image (ChromeOS printing). iso-codes is not a dependency (only language names in the font chooser's language filter use it; ChromiumOS does not package it).
# gtk4-update-icon-cache is not built (as in Gentoo; GTK 3 already installs gtk-update-icon-cache). gtk4-encode-symbolic-svg is not installed: it makes symbolic icons when apps are built, nothing on the device runs it, and it links its own static copy of GTK (7.3 MiB, a third of the package on the image). The Gentoo-only GDK_WINDOWING poison-macro patch is not applied (as for GTK 3).
# PDEPEND adwaita-icon-theme dropped: GTK 4 carries the symbolic icons its own widgets use in its resources, libadwaita its own; the hicolor fallback theme comes from chromeos-base/nickel-desktop-base. librsvg (SVG pixbuf loader, for symbolic icons) is an RDEPEND instead of a PDEPEND (no cycle here).

inherit gnome.org gnome2-utils meson xdg

DESCRIPTION="GTK is a multi-platform toolkit for creating graphical user interfaces"
HOMEPAGE="https://www.gtk.org/ https://gitlab.gnome.org/GNOME/gtk/"

LICENSE="LGPL-2+"
SLOT="4"
KEYWORDS="*"
IUSE="cups wayland X"
REQUIRED_USE="|| ( wayland X )"
RESTRICT="test"

COMMON_DEPEND="
	>=dev-libs/glib-2.76.0:2
	>=x11-libs/cairo-1.18.0[glib,X?]
	>=x11-libs/pango-1.52.0
	>=dev-libs/fribidi-1.0.6
	>=media-libs/harfbuzz-2.6.0:=
	>=x11-libs/gdk-pixbuf-2.30:2
	media-libs/libpng:=
	media-libs/tiff:=
	media-libs/libjpeg-turbo:=
	>=media-libs/libepoxy-1.4
	>=media-libs/graphene-1.10.0
	x11-misc/shared-mime-info

	cups? ( >=net-print/cups-2.0 )
	wayland? (
		>=dev-libs/wayland-1.21.0
		>=x11-libs/libxkbcommon-0.2
	)
	X? (
		media-libs/fontconfig
		media-libs/libglvnd[X]
		x11-libs/libX11
		>=x11-libs/libXi-1.8
		x11-libs/libXext
		>=x11-libs/libXrandr-1.5
		x11-libs/libXcursor
		x11-libs/libXfixes
		x11-libs/libXdamage
		x11-libs/libXinerama
		x11-libs/libXrender
	)
"
DEPEND="${COMMON_DEPEND}
	x11-libs/libdrm
	sys-kernel/linux-headers
	wayland? ( >=dev-libs/wayland-protocols-1.36 )
	X? ( x11-base/xorg-proto )
"
RDEPEND="${COMMON_DEPEND}
	gnome-base/librsvg
"
BDEPEND="
	dev-util/gdbus-codegen
	dev-util/glib-utils
	>=sys-devel/gettext-0.19.7
	virtual/pkgconfig
	wayland? ( dev-util/wayland-scanner )
"

src_prepare() {
	default
	xdg_environment_reset

	# Nothing should use gtk4-update-icon-cache; GTK 3 ships gtk-update-icon-cache.
	sed -i \
		-e '/gtk4-update-icon-cache/d' \
		docs/reference/gtk/meson.build \
		tools/meson.build \
		|| die
}

src_configure() {
	local emesonargs=(
		# GDK backends
		$(meson_use X x11-backend)
		$(meson_use wayland wayland-backend)
		-Dbroadway-backend=false
		-Dwin32-backend=false
		-Dmacos-backend=false

		# Media backends
		-Dmedia-gstreamer=disabled

		# Print backends
		-Dprint-cpdb=disabled
		$(meson_feature cups print-cups)

		# Optional dependencies
		-Dvulkan=disabled
		-Dcloudproviders=disabled
		-Dsysprof=disabled
		-Dtracker=disabled
		-Dcolord=disabled
		-Df16c=enabled

		# Introspection
		-Dintrospection=disabled

		# Documentation
		-Ddocumentation=false
		-Dscreenshots=false
		-Dman-pages=false

		# Demos, examples, and tests
		-Dprofile=default
		-Dbuild-demos=false
		-Dbuild-testsuite=false
		-Dbuild-examples=false
		-Dbuild-tests=false
	)
	meson_src_configure
}

src_install() {
	meson_src_install

	rm "${ED}"/usr/bin/gtk4-encode-symbolic-svg || die
}

pkg_preinst() {
	xdg_pkg_preinst
	gnome2_schemas_savelist
}

pkg_postinst() {
	xdg_pkg_postinst
	gnome2_schemas_update
}

pkg_postrm() {
	xdg_pkg_postrm
	gnome2_schemas_update
}
