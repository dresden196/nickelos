# Copyright 1999-2026 Gentoo Authors
# Distributed under the terms of the GNU General Public License v2

EAPI=7

# NickelOS: ported from Gentoo appstream-1.0.6 (EAPI 8 -> 7): KEYWORDS="*", dropped 9999 branch, vala, qt6 and the compose-only fontconfig patch; dropped the dev-libs/appstream-glib BDEPEND (unused by the meson build); new patch lets the cross build go ahead without a native appstreamcli or itstool in the SDK, hardcoded -I/usr/include removed.

inherit meson xdg-utils

MY_PN="AppStream"
SRC_URI="https://www.freedesktop.org/software/appstream/releases/${MY_PN}-${PV}.tar.xz"
KEYWORDS="*"
S="${WORKDIR}/${MY_PN}-${PV}"

DESCRIPTION="Cross-distro effort for providing metadata for software in the Linux ecosystem"
HOMEPAGE="https://www.freedesktop.org/wiki/Distributions/AppStream/"

LICENSE="LGPL-2.1+ GPL-2+"
# check as_api_level
SLOT="0/5"
IUSE="apt doc introspection systemd test"
RESTRICT="test" # bug 691962

RDEPEND="
	app-arch/zstd:=
	>=dev-libs/glib-2.62:2
	dev-libs/libxml2:2=
	>=dev-libs/libxmlb-0.3.14:=
	dev-libs/libyaml
	dev-libs/snowball-stemmer:=
	>=net-misc/curl-7.62
	introspection? ( dev-libs/gobject-introspection:= )
	systemd? ( sys-apps/systemd:= )
"
DEPEND="${RDEPEND}"
BDEPEND="
	app-text/docbook-xsl-stylesheets
	dev-libs/libxslt
	dev-util/glib-utils
	dev-util/gperf
	>=sys-devel/gettext-0.19.8
	doc? ( app-text/docbook-xml-dtd:4.5 )
"

PATCHES=(
	"${FILESDIR}"/${PN}-1.0.0-disable-Werror-flags.patch # bug 733774
	"${FILESDIR}"/${P}-cross-no-native-appstreamcli.patch
)

src_prepare() {
	default
	sed -e "/^as_doc_target_dir/s/appstream/${PF}/" -i docs/meson.build || die
	# NickelOS: a hardcoded -I/usr/include breaks the cross build
	# (-Wpoison-system-directories); the sysroot already has libstemmer.h.
	sed -e "s|include_directories(\['/usr/include'\])|include_directories()|" \
		-i meson.build || die
	if ! use test; then
		sed -e "/^subdir.*tests/s/^/#DONT /" -i {,qt/}meson.build || die # bug 675944
	fi
}

src_configure() {
	xdg_environment_reset

	local emesonargs=(
		-Dapidocs=false
		-Ddocs=false
		-Dcompose=false
		-Dmaintainer=false
		-Dstatic-analysis=false
		-Dstemming=true
		-Dvapi=false
		-Dapt-support=$(usex apt true false)
		-Dinstall-docs=$(usex doc true false)
		-Dgir=$(usex introspection true false)
		-Dqt=false
		-Dsystemd=$(usex systemd true false)
	)

	meson_src_configure
}
