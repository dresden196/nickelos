# Distributed under the terms of the GNU General Public License v2

EAPI=7

# NickelOS's Secure Boot path (scripts/secureboot/README.md): shim signed by
# Microsoft, as a distribution ships it, loads NickelOS's GRUB once the
# person has enrolled NickelOS's key in shim's MOK list (MokManager, on the
# first boot with Secure Boot on). Ubuntu's: shim 15.8, the same binaries
# as Ubuntu 24.04's shim-signed. nickel-image-grub puts them on the EFI
# system partition when the image is signed for Secure Boot.
DESCRIPTION="Microsoft-signed shim and MokManager (Ubuntu's) for NickelOS's Secure Boot path"
HOMEPAGE="https://github.com/rhboot/shim"
MY_DEB="shim-signed_1.58+15.8-0ubuntu1_amd64.deb"
SRC_URI="http://archive.ubuntu.com/ubuntu/pool/main/s/shim-signed/${MY_DEB}"

LICENSE="BSD-2"
SLOT="0"
KEYWORDS="*"
# Signed binaries: nothing may change them.
RESTRICT="mirror strip binchecks"

S="${WORKDIR}"

src_unpack() {
	unpack "${MY_DEB}"
	unpack ./data.tar.xz
}

src_install() {
	insinto /usr/share/nickel-shim
	newins usr/lib/shim/shimx64.efi.signed.latest shimx64.efi
	doins usr/lib/shim/mmx64.efi
}
