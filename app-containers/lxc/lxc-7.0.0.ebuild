# Copyright 1999-2026 Gentoo Authors
# Distributed under the terms of the GNU General Public License v2

EAPI=7

# NickelOS: from Gentoo's lxc-7.0.0-r1 (EAPI 8 -> 7), for Waydroid's Android
# container: the library and the lxc-* commands, with capabilities, seccomp
# and OpenSSL; no AppArmor, SELinux labels, PAM, systemd or OpenRC files (the
# container's upstart job is nickel-waydroid's), docs, examples or tests.
# memfd-rexec off: LXC re-executes lxc-attach (and the like) from a memfd
# against CVE-2019-5736, and ChromiumOS refuses to run code from memfds, so
# `waydroid shell` failed ("Failed to rexec as memfd"). Waydroid's container
# runs Android as root on the host, without user namespaces: the attack it
# guards against (a container overwriting the host's lxc binary) would need
# that root already.

inherit meson

DESCRIPTION="A userspace interface for the Linux kernel containment features"
HOMEPAGE="https://linuxcontainers.org/ https://github.com/lxc/lxc"
SRC_URI="https://linuxcontainers.org/downloads/lxc/${P}.tar.gz"

LICENSE="GPL-2 LGPL-2.1 LGPL-3"
SLOT="0/1.700"
KEYWORDS="*"

RDEPEND="
	sys-libs/libcap
	sys-libs/libseccomp
	dev-libs/openssl:0=
"
DEPEND="${RDEPEND}
	sys-kernel/linux-headers"
BDEPEND="virtual/pkgconfig"

PATCHES=(
	"${FILESDIR}"/lxc-7.0.0-glibc-2.43-const-fix.patch
)

src_configure() {
	local emesonargs=(
		--localstatedir "${EPREFIX}/var"

		-Dcoverity-build=false
		-Dinstall-state-dirs=false
		-Doss-fuzz=false
		-Dspecfile=false
		-Dtools-multicall=false

		-Dcommands=true
		-Dinstall-init-files=false
		-Dinit-script=openrc
		-Dmemfd-rexec=false
		-Dthread-safety=true

		-Dapparmor=false
		-Dcapabilities=true
		-Dapi-docs=false
		-Dexamples=false
		-Dio-uring-event-loop=false
		-Dlandlock-monitor=false
		-Dman=false
		-Dpam-cgroup=false
		-Dseccomp=true
		-Dselinux=false
		-Dopenssl=true
		-Dtests=false
		-Dtools=true
		-Ddbus=false

		-Ddata-path=/var/lib/lxc
		-Ddoc-path=/usr/share/doc/${PF}
		-Dlog-path=/var/log/lxc
		-Drootfs-mount-path=/var/lib/lxc/rootfs
		-Druntime-path=/run
	)
	meson_src_configure
}

src_install() {
	meson_src_install
	find "${ED}" -name '*.la' -delete -o -name '*.a' -delete || die
	rm -rf "${ED}"/usr/share/bash-completion "${ED}"/usr/share/doc || die
}
