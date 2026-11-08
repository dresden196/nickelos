# Distributed under the terms of the GNU General Public License v2

EAPI=7

inherit nickel-font

DESCRIPTION="Google Sans Code monospace font, for the terminal"
HOMEPAGE="https://github.com/googlefonts/googlesans-code"
SRC_URI="https://github.com/googlefonts/googlesans-code/releases/download/v${PV}/GoogleSansCode-v${PV}.zip"

LICENSE="OFL-1.1"
SLOT="0"
KEYWORDS="*"
RESTRICT="mirror"

BDEPEND="app-arch/unzip"

S="${WORKDIR}"

NICKEL_FONT_FILES=(
	"GoogleSansCode[MONO,wght].ttf"
	"GoogleSansCode-Italic[MONO,wght].ttf"
)
