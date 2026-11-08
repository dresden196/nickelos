# Copyright 1999-2026 Gentoo Authors
# Distributed under the terms of the GNU General Public License v2

EAPI=7

# NickelOS: ported from Gentoo fuse-3.18.2 (EAPI 8 -> 7): KEYWORDS="*", meson-multilib -> meson (single ABI), dropped tests (pytest); slot 3 installs next to ChromiumOS's fuse:0 (2.9.8) and shares its sys-fs/fuse-common; fusermount3 stays setuid root (USE=suid).

inherit flag-o-matic meson toolchain-funcs udev

DESCRIPTION="An interface for filesystems implemented in userspace"
HOMEPAGE="https://github.com/libfuse/libfuse"
# Packaged as sys-fs/fuse3 (not Gentoo's sys-fs/fuse:3): six ChromiumOS
# ebuilds depend on a plain, unversioned sys-fs/fuse and must keep getting
# fuse 2.9; under the same name, any fresh rebuild of them would get fuse 3.
MY_P="fuse-${PV}"
SRC_URI="https://github.com/libfuse/libfuse/releases/download/${MY_P}/${MY_P}.tar.gz"
S="${WORKDIR}/${MY_P}"

LICENSE="GPL-2 LGPL-2.1"
SLOT="3/4"
KEYWORDS="*"
IUSE="io-uring +suid systemtap"
RESTRICT="test"

DEPEND="
	io-uring? (
		sys-libs/liburing:=
		sys-process/numactl
	)
"
RDEPEND="
	${DEPEND}
	>=sys-fs/fuse-common-3.3.0-r1
"
BDEPEND="
	virtual/pkgconfig
"

DOCS=( AUTHORS ChangeLog.rst README.md doc/README.NFS doc/kernel.txt )

src_configure() {
	# bug #853058
	tc-is-clang && filter-lto

	local emesonargs=(
		-Dexamples=false
		-Dtests=false
		$(meson_use systemtap enable-usdt)
		$(meson_use io-uring enable-io-uring)
		-Duseroot=false
		-Dinitscriptdir=
		-Dudevrulesdir="${EPREFIX}$(get_udevdir)/rules.d"
	)
	meson_src_configure
}

src_install() {
	meson_src_install

	# Installed via fuse-common
	rm -r "${ED}"{/etc,$(get_udevdir)} || die

	# useroot=false prevents the build system from doing this.
	use suid && fperms u+s /usr/bin/fusermount3

	# manually install man pages to respect compression
	rm -r "${ED}"/usr/share/man || die
	doman doc/{fusermount3.1,mount.fuse3.8}
}
