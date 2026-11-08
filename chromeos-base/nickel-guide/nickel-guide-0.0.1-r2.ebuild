# Copyright 2026 The NickelOS Authors
# Distributed under the terms of the GNU General Public License v2

EAPI=7

DESCRIPTION="NickelOS: the guide (nickelos.dev/help) on the device, for Settings > Help"
# The guide's pages, and the fonts the site is set in (SIL Open Font License).
LICENSE="GPL-2 OFL-1.1"
SLOT="0"
KEYWORDS="*"

S="${WORKDIR}"

# files/guide is made by site/build.sh (build-help.py --offline) from
# site/help-src: plain pages with relative links, the stylesheet and fonts
# beside them. Bump the revision when it changes. Chrome opens local files
# only from a few places, /usr/share/chromeos-assets among them, so the
# browser can show these from disk, offline.
src_install() {
	insinto /usr/share/chromeos-assets/nickel-guide
	doins -r "${FILESDIR}"/guide/.
}
