# Copyright 1999-2025 Gentoo Authors
# Distributed under the terms of the GNU General Public License v2

EAPI=7

# NickelOS: ported from Gentoo graphene-1.10.8-r1 (already EAPI 7): KEYWORDS="*",
# single ABI (meson, not meson-multilib). GTK 4 needs it (graphene-gobject-1.0).
# SSE2 is always on (it is part of the x86_64 baseline; Gentoo keys it to
# CPU_FLAGS_X86, which the board does not set). Off: introspection (ChromiumOS
# has gobject-introspection 1.72, older than the 1.82 Gentoo asks for, and
# nothing on the image reads typelibs), gtk-doc, tests; so no Python BDEPEND.

inherit meson

DESCRIPTION="A thin layer of types for graphic libraries"
HOMEPAGE="https://ebassi.github.io/graphene/"
SRC_URI="https://github.com/ebassi/graphene/archive/refs/tags/${PV}.tar.gz -> ${P}.tar.gz"

LICENSE="MIT"
SLOT="0"
KEYWORDS="*"
RESTRICT="test"

RDEPEND=">=dev-libs/glib-2.30.0:2"
DEPEND="${RDEPEND}"
BDEPEND="virtual/pkgconfig"

src_configure() {
	local emesonargs=(
		-Dgtk_doc=false
		-Dgobject_types=true
		-Dintrospection=disabled
		-Dgcc_vector=true
		-Dsse2=true
		-Darm_neon=false
		-Dtests=false
		-Dinstalled_tests=false
	)
	meson_src_configure
}
