# Copyright 1999-2026 Gentoo Authors
# Distributed under the terms of the GNU General Public License v2

EAPI=7

# NickelOS: ported from Gentoo snowball-stemmer-3.1.1 (EAPI 8 -> 7): KEYWORDS="*", dropped static-libs (dot-a.eclass is not in ChromiumOS) and tests (35 MB test data).

inherit edo toolchain-funcs

DESCRIPTION="Snowball compiler and stemming algorithms"
HOMEPAGE="https://snowballstem.org/ https://github.com/snowballstem/snowball/"
SRC_URI="
	https://github.com/snowballstem/snowball/archive/v${PV}.tar.gz
		-> ${P}.tar.gz
"
S="${WORKDIR}/snowball-${PV}"

LICENSE="BSD"
SONAME="$(ver_cut 1-2)"
SLOT="0/${SONAME}"
KEYWORDS="*"
RESTRICT="test"

BDEPEND="
	dev-lang/perl
"

PATCHES=(
	# Shared-library: export only public sb_stemmer_* symbols (matching libstemmer.h)
	"${FILESDIR}"/${PN}-3.1.0-public_symbols.patch
)

src_compile() {
	# 947412
	tc-is-cross-compiler && tc-env_build emake snowball

	tc-export CC AR
	emake CFLAGS="${CFLAGS} -fPIC" all

	# Created shared library contains a lot of relocations, which slow down loading.
	# It is known issue and probably the main reason why upstream does not support shared library yet.
	# see https://github.com/snowballstem/snowball/issues/34
	edo "${CC}" \
		${CFLAGS} \
		${LDFLAGS} \
		-Wl,-version-script,libstemmer/symbol.map \
		-Wl,--whole-archive \
		libstemmer.a \
		-Wl,--no-whole-archive \
		-Wl,-soname,libstemmer.so.${SONAME} \
		-shared \
		-o libstemmer.so.${PV}
}

src_install() {
	dodoc README.rst NEWS

	dobin stemwords

	doheader include/libstemmer.h

	dolib.so libstemmer.so.${PV}
	dosym libstemmer.so.${PV} /usr/$(get_libdir)/libstemmer.so.${SONAME}
	dosym libstemmer.so.${PV} /usr/$(get_libdir)/libstemmer.so
}
