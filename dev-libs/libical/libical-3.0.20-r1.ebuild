# Copyright 1999-2026 Gentoo Authors
# Distributed under the terms of the GNU General Public License v2

EAPI=7

# NickelOS: from Gentoo libical-3.0.20-r2 (EAPI 8 -> 7), for
# evolution-data-server (Valent's contacts): the C library and libical-glib,
# with ICU; no C++ bindings, introspection, vapi, docs, examples or tests.
# Time zones from the system's tzdata.

inherit cmake toolchain-funcs

DESCRIPTION="Implementation of basic iCAL protocols"
HOMEPAGE="https://github.com/libical/libical"
SRC_URI="https://github.com/libical/libical/releases/download/v${PV}/${P}.tar.gz"

LICENSE="|| ( MPL-2.0 LGPL-2.1 )"
SLOT="0/3"
KEYWORDS="*"
RESTRICT="test"

RDEPEND="
	dev-libs/nickel-icu:=
	>=dev-libs/glib-2.32:2
	dev-libs/libxml2:2
	sys-libs/timezone-data
"
DEPEND="${RDEPEND}"
BDEPEND="
	dev-lang/perl
	dev-libs/glib:2
	dev-libs/libxml2:2
	dev-util/glib-utils
	virtual/pkgconfig
"

src_configure() {
	# libical-glib's sources come from a generator that runs on the build
	# machine; a cross build imports it from a native libical. Build just
	# that tool here and hand CMake an imported target for it.
	local gen="${WORKDIR}/native-generator"
	mkdir -p "${gen}" || die
	$(tc-getBUILD_CC) -O2 -DG_LOG_DOMAIN=\"src-generator\" \
		-o "${gen}"/ical-glib-src-generator \
		src/libical-glib/tools/{generator,xml-parser}.c \
		$($(tc-getBUILD_PKG_CONFIG) --cflags --libs glib-2.0 libxml-2.0) || die
	cat > "${gen}"/import.cmake <<-EOF || die
	add_executable(native-ical-glib-src-generator IMPORTED)
	set_target_properties(native-ical-glib-src-generator PROPERTIES
		IMPORTED_LOCATION "${gen}/ical-glib-src-generator")
	EOF

	local mycmakeargs=(
		-DCMAKE_DISABLE_FIND_PACKAGE_BerkeleyDB=ON
		-DICAL_BUILD_DOCS=OFF
		-DICAL_GLIB=ON
		-DICAL_GLIB_VAPI=OFF
		-DGOBJECT_INTROSPECTION=OFF
		-DLIBICAL_BUILD_TESTING=OFF
		-DSHARED_ONLY=ON
		-DUSE_BUILTIN_TZDATA=OFF
		-DWITH_CXX_BINDINGS=OFF
		# Where ICU's tools are, only for the tests, but CMake insists; a
		# cross build finds none on the build host.
		-DICU_UCONV_EXECUTABLE="${ESYSROOT}"/usr/bin/uconv
		-DIMPORT_ICAL_GLIB_SRC_GENERATOR="${gen}"/import.cmake
	)
	cmake_src_configure
}
