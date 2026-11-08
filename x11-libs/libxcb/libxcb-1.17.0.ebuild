# Copyright 1999-2026 Gentoo Authors
# Distributed under the terms of the GNU General Public License v2

# NickelOS: 1.17 adds the DRI3 1.4 and Present 1.4 requests
# (xcb_dri3_import_syncobj, xcb_present_pixmap_synced). NVIDIA's GL and Vulkan
# look them up at run time to give Xwayland explicit sync points for each
# frame; with 1.16 (portage-stable) they found none and presented without.
# Same soname and subslot as 1.16: nothing needs a rebuild.

EAPI=7

PYTHON_COMPAT=( python3_11 )
PYTHON_REQ_USE="xml(+)"

XORG_TARBALL_SUFFIX="xz"
XORG_MULTILIB=yes
XORG_DOC=doc

inherit python-any-r1 xorg-3

DESCRIPTION="X C-language Bindings library"
HOMEPAGE="https://xcb.freedesktop.org/ https://gitlab.freedesktop.org/xorg/lib/libxcb"

KEYWORDS="*"
IUSE="doc selinux test +xkb"
RESTRICT="!test? ( test )"
SLOT="0/1.12"

RDEPEND="
	>=x11-libs/libXau-1.0.7-r1[${MULTILIB_USEDEP}]
	>=x11-libs/libXdmcp-1.1.1-r1[${MULTILIB_USEDEP}]
"
DEPEND="${RDEPEND}
	x11-base/xorg-proto
	>=x11-base/xcb-proto-${PV}
	elibc_Darwin? ( dev-libs/libpthread-stubs )
	test? ( dev-libs/check[${MULTILIB_USEDEP}] )
"
# The bindings are generated with the board's xcbgen (below), so the SDK's
# xcb-proto only has to provide one at all.
# Note: ${PYTHON_USEDEP} needs to go verbatim
BDEPEND="${PYTHON_DEPS}
	$(python_gen_any_dep '>=x11-base/xcb-proto-1.16.0[${PYTHON_USEDEP}]')
	doc? ( app-doc/doxygen[dot] )
	test? ( dev-libs/libxslt )
	>=x11-misc/util-macros-1.18
"

# Large file support (1.16's patch) is upstream in 1.17.
PATCHES=(
	"${FILESDIR}/${PN}-1.17.0-unquoted-var.patch"
)

python_check_deps() {
	python_has_version ">=x11-base/xcb-proto-1.16.0[${PYTHON_USEDEP}]"
}

pkg_setup() {
	python-any-r1_pkg_setup
	xorg-3_pkg_setup
}

src_prepare() {
	default

	eautoreconf
}

src_configure() {
	local XORG_CONFIGURE_OPTIONS=(
		$(use_enable doc devel-docs)
		$(use_enable selinux)
	)
	xorg-3_src_configure
}

multilib_src_compile() {
	# The protocol XML comes from the board's xcb-proto (pkg-config gives its
	# sysroot path), but xcb-proto.pc's pythondir is empty, so c_client.py
	# would generate it with the SDK's older xcbgen. Use the board's, the
	# same version as the XML.
	local sitedir
	sitedir=$(ls -d "${ESYSROOT}"/usr/lib*/"${EPYTHON}"/site-packages 2>/dev/null |
		head -n1)
	[[ -d ${sitedir}/xcbgen ]] ||
		die "no xcbgen for ${EPYTHON} in ${ESYSROOT}"
	emake XCBPROTO_XCBPYTHONDIR="${sitedir}"
}
