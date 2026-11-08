# Copyright 1999-2026 Gentoo Authors
# Distributed under the terms of the GNU General Public License v2

EAPI=7

# NickelOS: ported from Gentoo xdg-utils-1.2.1-r11 (EAPI 8 -> 7):
# KEYWORDS="*", none of the optional features (X tools, Perl, Plasma, D-Bus
# helpers), no env.d files (nickel-session sets the XDG directories). The
# scripts treat the NickelOS desktop as GNOME 3, so xdg-open, xdg-email and
# xdg-mime go through gio and follow the same defaults as GIO and the OpenURI
# portal (nickelos-mimeapps.list: links and files open through ChromeOS).

DESCRIPTION="Portland utils for cross-platform/cross-toolkit/cross-desktop interoperability"
HOMEPAGE="https://www.freedesktop.org/wiki/Software/xdg-utils/"
SRC_URI="https://gitlab.freedesktop.org/xdg/xdg-utils/-/archive/v${PV}/${PN}-v${PV}.tar.bz2"
S="${WORKDIR}"/${PN}-v${PV}

LICENSE="MIT"
SLOT="0"
KEYWORDS="*"

RDEPEND="
	dev-libs/glib:2
	dev-util/desktop-file-utils
	x11-misc/shared-mime-info
"

# Tests run random system programs, including interactive programs
# that block forever
RESTRICT="test"

PATCHES=(
	"${FILESDIR}"/${P}-xdg-mime-default.patch
	"${FILESDIR}"/${PN}-1.2.1-qtpaths.patch
	"${FILESDIR}"/${PN}-1.2.1-fix-CVE-2020-27748.patch
	"${FILESDIR}"/${PN}-1.2.1-fix-attach-with-spaces.patch
)

XDG_SCRIPTS=(
	xdg-desktop-icon xdg-desktop-menu xdg-email xdg-icon-resource xdg-mime
	xdg-open xdg-screensaver xdg-settings
)

src_prepare() {
	default
	# NickelOS: its desktop opens with gio, as GNOME 3 does.
	sed -i -e '/^         GNOME\*)$/i\         NickelOS)\n           DE=gnome3;\n           ;;' \
		scripts/xdg-utils-common.in || die
	grep -q '^         NickelOS)$' scripts/xdg-utils-common.in ||
		die "NickelOS desktop not added"
}

src_configure() {
	# The scripts are assembled below; nothing needs xmlto.
	export ac_cv_path_XMLTO=true
	default
}

src_compile() {
	cd scripts || die
	local s
	for s in "${XDG_SCRIPTS[@]}"; do
		# Their --help and --manual text, which upstream renders with xmlto
		# (and a text browser): the description without its markup.
		sed -e 's/<[^>]*>//g' -e 's/^[[:space:]]*//' "desc/${s}.xml" |
			cat -s > "${s}.txt" || die
		awk -f generate-help-script.awk "${s}.in" |
			sed -e "s/@NAME@/${s}/g" > "${s}" || die
	done
}

src_install() {
	local s
	for s in "${XDG_SCRIPTS[@]}"; do
		dobin "scripts/${s}"
	done
	dodoc RELEASE_NOTES
}
