# Copyright 1999-2026 Gentoo Authors
# Distributed under the terms of the GNU General Public License v2

EAPI=7

# NickelOS: ported from Gentoo elogind-257.16 (EAPI 8 -> 7): KEYWORDS="*",
# dropped the 9999 branch, docs (man/html, with their docbook/lxml BDEPEND and
# the lxml man-page patch), tests, translations, the cgroup-hybrid flag (its
# meson option is a deprecated no-op in 257), the linux-info kernel check, and
# the OpenRC init script, conf.d file and runlevel messages (eapi9-ver): under
# upstart, elogind is started by a job (see the D-Bus service below).
# Mapped to ChromiumOS: udev is sys-fs/udev (virtual/udev), not
# systemd-utils[udev]. Added patch: builds against the SDK's Linux 5.4 UAPI
# headers (libelogind without its internal sd-netlink; one newer UAPI header
# bundled), and a link fix for -Dnss-elogind=false; see the patches.
# cgroups: -Dcgroup-controller=elogind (Gentoo: openrc). Nothing on ChromiumOS
# names a cgroup hierarchy for elogind to follow, so elogind mounts its own
# named v1 hierarchy (name=elogind, at /sys/fs/cgroup/elogind) in the tmpfs
# upstart mounts on /sys/fs/cgroup, next to ChromeOS's v1 controllers, which
# it leaves alone. Each session is a cgroup there (/sys/fs/cgroup/elogind/<id>).
# Off: EFI (no efivarfs mount, no reboot-to-firmware), the NSS module and
# userdbd (meson booleans default to on), SMACK.
# The D-Bus activation file runs "/sbin/start elogind" (the ChromiumOS
# convention for activatable services) rather than the daemon itself, so
# elogind only ever runs as an upstart job.
# NickelOS defaults (files/elogind-nickelos-*.conf, installed as
# /etc/elogind/{logind,sleep}.conf.d/20-nickelos.conf): powerd keeps the power
# and suspend keys, the lid switch, idle handling and suspend; elogind ignores
# them and cannot suspend.

PYTHON_COMPAT=( python3_{11..14} )

inherit meson pam python-any-r1 udev xdg-utils

DESCRIPTION="The systemd project's logind, extracted to a standalone package"
HOMEPAGE="https://github.com/elogind/elogind"
SRC_URI="https://github.com/${PN}/${PN}/archive/refs/tags/v${PV}.tar.gz -> ${P}.tar.gz"

LICENSE="CC0-1.0 LGPL-2.1+ public-domain"
SLOT="0"
KEYWORDS="*"
IUSE="+acl audit debug +pam +policykit selinux"
RESTRICT="test"

BDEPEND="
	dev-util/gperf
	virtual/pkgconfig
	$(python_gen_any_dep 'dev-python/jinja2[${PYTHON_USEDEP}]')
"
DEPEND="
	audit? ( sys-process/audit )
	sys-apps/util-linux
	sys-libs/libcap
	virtual/libudev:=
	acl? ( sys-apps/acl )
	pam? ( sys-libs/pam )
	selinux? ( sys-libs/libselinux )
"
RDEPEND="${DEPEND}
	!sys-apps/systemd
	virtual/udev
"
PDEPEND="
	sys-apps/dbus
	policykit? ( sys-auth/polkit )
"

DOCS=( README.md )

PATCHES=(
	"${FILESDIR}"/${P}-linux-5.4-uapi.patch
	"${FILESDIR}"/${P}-nss-off.patch
)

python_check_deps() {
	python_has_version "dev-python/jinja2[${PYTHON_USEDEP}]"
}

src_prepare() {
	default
	xdg_environment_reset

	# don't cleanup /dev/shm/ on logout on logout
	# https://bugs.debian.org/cgi-bin/bugreport.cgi?bug=949698
	sed -e "s/#RemoveIPC=yes/RemoveIPC=no/" \
		-i src/login/logind.conf.in || die
}

src_configure() {
	python_setup

	EMESON_BUILDTYPE="$(usex debug debug release)"

	local emesonargs=(
		-Ddocdir="${EPREFIX}/usr/share/doc/${PF}"
		-Dhtmldir="${EPREFIX}/usr/share/doc/${PF}/html"
		-Dudevrulesdir="${EPREFIX}$(get_udevdir)"/rules.d
		--libexecdir="lib/elogind"
		--localstatedir="${EPREFIX}"/var
		-Dbashcompletiondir="${EPREFIX}/usr/share/bash-completion/completions"
		-Dman=disabled
		-Dhtml=disabled
		-Dtranslations=false
		-Dsmack=false
		-Defi=false
		-Dnss-elogind=false
		-Duserdb=false
		-Dcgroup-controller=elogind
		-Ddefault-kill-user-processes=false
		-Dacl=$(usex acl enabled disabled)
		-Daudit=$(usex audit enabled disabled)
		-Dpam=$(usex pam enabled disabled)
		-Dpamlibdir="$(getpam_mod_dir)"
		-Dpolkit=$(usex policykit enabled disabled)
		-Dselinux=$(usex selinux enabled disabled)
		-Dtests=false
		-Dutmp=$(usex elibc_musl false true)
		-Dmode=release

		# Ensure consistency between merged-usr and split-usr (bug 945965)
		-Dhalt-path="${EPREFIX}/sbin/halt"
		-Dkexec-path="${EPREFIX}/usr/sbin/kexec"
		-Dnologin-path="${EPREFIX}/sbin/nologin"
		-Dpoweroff-path="${EPREFIX}/sbin/poweroff"
		-Dreboot-path="${EPREFIX}/sbin/reboot"
	)

	meson_src_configure
}

src_install() {
	meson_src_install
	# ChromiumOS images carry nothing in /var: drop the empty /var/lib/elogind
	# meson installs (Gentoo keeps it). Only lingering uses it, and elogind
	# creates it when needed.
	rmdir "${ED}"/var/lib/elogind "${ED}"/var/lib "${ED}"/var || die

	rm -f "${ED}"/usr/share/doc/${PF}/LICENSE* || die
	rm -f "${ED}"/usr/share/doc/${PF}/CODING_STYLE.md || die

	# D-Bus activation goes through upstart (see the header).
	local svc="${ED}"/usr/share/dbus-1/system-services/org.freedesktop.login1.service
	[[ -f ${svc} ]] || die "missing ${svc}"
	sed -i -e 's|^Exec=.*|Exec=/sbin/start elogind|' "${svc}" || die

	insinto /etc/elogind/logind.conf.d
	newins "${FILESDIR}"/elogind-nickelos-logind.conf 20-nickelos.conf
	insinto /etc/elogind/sleep.conf.d
	newins "${FILESDIR}"/elogind-nickelos-sleep.conf 20-nickelos.conf
}

pkg_postinst() {
	udev_reload
	if ! use pam; then
		ewarn "${PN} will not be managing user logins/seats without USE=\"pam\"!"
		ewarn "In other words, it will be useless for most applications."
		ewarn
	fi
	if ! use policykit; then
		ewarn "loginctl will not be able to perform privileged operations without"
		ewarn "USE=\"policykit\"! That means e.g. no suspend or hibernate."
		ewarn
	fi
}

pkg_postrm() {
	udev_reload
}
