# Copyright 1999-2026 Gentoo Authors
# Distributed under the terms of the GNU General Public License v2

EAPI=7

# NickelOS: from Gentoo libphonenumber-8.13.47-r1 (EAPI 8 -> 7), for Valent
# (net-misc/valent), which matches phone numbers with it: the library on
# protobuf-lite with ICU's regexps and std::mutex; no geocoder (its own
# metadata, which also needs Gentoo's cmake patch), Boost, metadata
# regeneration (Java) or tests. Gentoo's patches for linking abseil and
# against -Werror.

inherit cmake

DESCRIPTION="Library for parsing, formatting, and validating international phone numbers"
HOMEPAGE="https://github.com/google/libphonenumber"
SRC_URI="https://github.com/google/${PN}/archive/v${PV}.tar.gz -> ${P}.tar.gz"
CMAKE_USE_DIR="${WORKDIR}/${P}/cpp"

LICENSE="Apache-2.0"
SLOT="0/8.13"
KEYWORDS="*"
RESTRICT="test"

DEPEND="
	dev-cpp/abseil-cpp:=
	dev-libs/nickel-icu:=
	dev-libs/protobuf:=
"
RDEPEND="${DEPEND}"
BDEPEND="dev-libs/protobuf"

PATCHES=(
	"${FILESDIR}"/${P}-protobuf-link-abseil.patch
	"${FILESDIR}"/${P}-werror.patch
)

src_prepare() {
	# Only the tests use it, and they are off.
	touch "${CMAKE_USE_DIR}"/src/phonenumbers/test_metadata.h || die
	cmake_src_prepare
}

src_configure() {
	local mycmakeargs=(
		-DBUILD_GEOCODER=OFF
		-DBUILD_STATIC_LIB=OFF
		-DBUILD_TESTING=OFF
		-DREGENERATE_METADATA=OFF
		-DUSE_BOOST=OFF
		-DUSE_ICU_REGEXP=ON
		-DUSE_PROTOBUF_LITE=ON
		-DUSE_STDMUTEX=ON
	)
	cmake_src_configure
}
