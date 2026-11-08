# Copyright 2026 NickelOS
# Distributed under the terms of the GNU General Public License v2

EAPI=7

DESCRIPTION="ChromeOS's Japanese input method on Mozc in WebAssembly"
HOMEPAGE="https://nickelos.dev"
# NickelOS: Chrome lists ChromeOS's Japanese input method (nacl_mozc, its
# manifest compiled in) but its engine was a Native Client module, which
# Chrome no longer runs, and the extension was never in ChromiumOS images.
# This is Mozc's own NaCl extension JavaScript (google/mozc, its last
# release with it) on Mozc's session handler in WebAssembly, built by
# scripts/mozc/build.sh (not in the SDK: it needs emscripten).
SRC_URI="https://nickelos.dev/distfiles/${P}.tar.xz"
RESTRICT="mirror"

# Mozc and its extension (BSD), protobuf (BSD), abseil (Apache-2.0); Mozc's
# dictionary data (BSD, ipadic, public-domain, unicode).
LICENSE="BSD Apache-2.0 ipadic public-domain unicode"
SLOT="0"
KEYWORDS="*"

src_install() {
	insinto /usr/share/chromeos-assets/input_methods/nacl_mozc
	doins -r ./*
}
