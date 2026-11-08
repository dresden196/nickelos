# Distributed under the terms of the GNU General Public License v2

EAPI=7

inherit nickel-font

DESCRIPTION="Google Sans Flex variable font (weight, width, optical size, slant, grade, roundness)"
HOMEPAGE="https://github.com/googlefonts/googlesans-flex"
SRC_URI="https://github.com/googlefonts/googlesans-flex/releases/download/v${PV}/GoogleSansFlex-v${PV}.zip"

# OFL without Reserved Font Names. "Google Sans" is a Google trademark: ship the
# font unmodified, never use the name in NickelOS branding (see TRADEMARKS.txt).
LICENSE="OFL-1.1"
SLOT="0"
KEYWORDS="*"
RESTRICT="mirror"

BDEPEND="app-arch/unzip"

S="${WORKDIR}"

NICKEL_FONT_FILES=( "GoogleSansFlex[GRAD,ROND,opsz,slnt,wdth,wght].ttf" )
NICKEL_FONT_CONF=( "${FILESDIR}/60-nickel-google-sans.conf" )
