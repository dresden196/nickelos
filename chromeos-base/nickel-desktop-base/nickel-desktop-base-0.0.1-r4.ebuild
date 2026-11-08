# Copyright 2026 NickelOS Authors
# Distributed under the terms of the GNU General Public License v2

EAPI=7

# The library set AppImages expect from a desktop distribution. AppImages
# bundle their application but not the desktop base underneath it: Electron
# AppImages (Obsidian, balenaEtcher...) link GTK 3 (libgtk-3, libgdk-3), the
# accessibility stack (libatk-1.0, libatk-bridge-2.0, libatspi) and the X11
# extension libraries Chromium uses (Xcomposite, Xdamage, Xfixes, Xrandr) from
# the host, and fail to start without them. ChromeOS ships none of these;
# NSS, ALSA, GBM, DRM and the other libraries they link are already on the
# image. GTK 3's runtime data comes along: its GSettings schemas
# (org.gtk.Settings.*, compiled at install), the hicolor fallback icon theme
# and the SVG pixbuf loader (librsvg). The polkit agent (pkexec's password
# prompt) is no longer polkit-gnome's GTK 3 window but ChromeOS's own prompt
# (chromeos-base/nickel-logind's agent and Chrome 0103).
# GTK 4 and libadwaita are for native GTK 4 apps (Portage packages, AppImages
# that leave them to the host); Flatpak apps bring their own from the GNOME
# runtime. They come with their GSettings schemas (org.gtk.gtk4.Settings.*)
# and use the same pixbuf loaders and hicolor theme as GTK 3.
# Mesa, the GPU for them all (OpenGL: EGL on Wayland and X11, GLX; Vulkan),
# is Gentoo's, the OS's one (NickelOS, vision decision 5): the image
# build installs it (scripts/unified/gentoo/install.txt).

DESCRIPTION="Desktop base libraries Linux apps expect from the host (GTK 3, GTK 4, libadwaita, AT-SPI, X11 extensions)"
HOMEPAGE="https://github.com/dresden196/overlay-nickel"

LICENSE="metapackage"
SLOT="0"
KEYWORDS="*"

S="${WORKDIR}"

RDEPEND="
	app-accessibility/at-spi2-core
	gnome-base/librsvg
	gui-libs/gtk:4[wayland,X]
	gui-libs/libadwaita:1
	x11-libs/gdk-pixbuf:2
	x11-libs/gtk+:3[wayland,X]
	x11-libs/libXcomposite
	x11-libs/libXdamage
	x11-libs/libXfixes
	x11-libs/libXrandr
	x11-themes/hicolor-icon-theme
"

pkg_postinst() {
	# gdk-pixbuf's own postinst leaves loaders.cache empty when installing
	# into a sysroot or image root, so no external pixbuf loader is
	# registered: GTK cannot load SVG icons (librsvg's loader), nor GIF, ICO,
	# BMP... (PNG and JPEG are built in). Regenerate the cache with the
	# target's query tool, run through the target's dynamic loader against
	# the target's libraries (NickelOS is x86_64 only, like the SDK), and
	# strip the root prefix from the loader paths.
	local root="${EROOT%/}"
	local libdir=$(get_libdir)
	local dir="${root}/usr/${libdir}/gdk-pixbuf-2.0/2.10.0"
	local query="${root}/usr/bin/${CHOST}-gdk-pixbuf-query-loaders"
	[[ -x ${query} ]] || query="${root}/usr/bin/gdk-pixbuf-query-loaders"
	local ldso=( "${root}"/${libdir}/ld-linux*.so.* )
	local loaders=( "${dir}"/loaders/*.so )

	if [[ ! -x ${query} || ! -x ${ldso[0]} || ! -e ${loaders[0]} ]]; then
		ewarn "gdk-pixbuf loader cache not regenerated: query tool, dynamic loader or loaders missing"
		return
	fi

	ebegin "Regenerating gdk-pixbuf loader cache"
	"${ldso[0]}" --inhibit-cache \
		--library-path "${root}/usr/${libdir}:${root}/${libdir}" \
		"${query}" "${loaders[@]}" > "${T}"/loaders.cache &&
	grep -q '^"/' "${T}"/loaders.cache &&
	sed -e "s|^\"${root}/|\"/|" "${T}"/loaders.cache > "${dir}"/loaders.cache &&
	chmod 0644 "${dir}"/loaders.cache
	eend $? || ewarn "gdk-pixbuf loader cache not regenerated"
}
