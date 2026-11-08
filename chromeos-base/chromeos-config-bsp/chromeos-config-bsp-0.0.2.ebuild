# Distributed under the terms of the GNU General Public License v2

EAPI=7

inherit cros-unibuild

DESCRIPTION="NickelOS model config for generic PC hardware"
HOMEPAGE="https://chromium.googlesource.com/chromiumos/platform2/+/HEAD/chromeos-config/README.md"
LICENSE="BSD-Google"
SLOT="0/${PVR}"
KEYWORDS="*"

S="${WORKDIR}"

src_install() {
	install_model_files
}
