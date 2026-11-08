# Copyright 1999-2026 Gentoo Authors
# Distributed under the terms of the GNU General Public License v2

EAPI=7

# NickelOS: ported from Gentoo bubblewrap-0.12.0 (EAPI 8 -> 7): KEYWORDS="*", dropped tests (python libseccomp) and man pages (docbook), zsh completion off.

inherit linux-info meson shell-completion

DESCRIPTION="Unprivileged sandboxing tool, namespaces-powered chroot-like solution"
HOMEPAGE="https://github.com/containers/bubblewrap/"
SRC_URI="https://github.com/containers/${PN}/releases/download/v${PV}/${P}.tar.xz"

LICENSE="LGPL-2.1+"
SLOT="0"
KEYWORDS="*"
IUSE="selinux"
RESTRICT="test"

RDEPEND="
	sys-libs/libcap
	selinux? ( >=sys-libs/libselinux-2.3 )
"
DEPEND="${RDEPEND}"
BDEPEND="
	virtual/pkgconfig
"
RDEPEND+=" selinux? ( sec-policy/selinux-bubblewrap )"

PATCHES=(
	"${FILESDIR}"/${PN}-0.11.2-no-werror.patch
	"${FILESDIR}"/${PN}-0.12.0-musl.patch
)

pkg_setup() {
	if [[ ${MERGE_TYPE} != buildonly ]]; then
		CONFIG_CHECK="~UTS_NS ~IPC_NS ~USER_NS ~PID_NS ~NET_NS"
		linux-info_pkg_setup
	fi
}

src_configure() {
	local emesonargs=(
		# We could write up -Dassume_kernel like in glibc if
		# someone is interested
		-Dbash_completion=enabled
		-Dbash_completion_dir="$(get_bashcompdir)"
		-Dzsh_completion=disabled
		-Dman=disabled
		$(meson_feature selinux)
		-Dtests=false
	)

	meson_src_configure
}
