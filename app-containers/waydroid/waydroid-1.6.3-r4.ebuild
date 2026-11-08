# Copyright 2022-2026 Gentoo Authors
# Distributed under the terms of the GNU General Public License v2

EAPI=7

# NickelOS: from GURU's waydroid-1.6.3 (EAPI 8 -> 7): Android in an LXC
# container, its windows on Exo. iptables, not nftables (ChromeOS's kernel
# has no nf_tables); no systemd unit or D-Bus activation (nickel-waydroid's
# upstart job runs the container manager); no AppArmor profile (LXC is built
# without it). Patched for ChromeOS's network (files/*-nickel-network.patch),
# Android's time zone (*-nickel-timezone.patch), a session started right
# after sign-in (*-nickel-wayland-wait.patch) and NVIDIA through
# waydroid-nvidia (*-waydroid-nvidia.patch, active only when set up for it).
# Its "Install in Waydroid" .apk handler goes (nickel-waydroid's
# nickel-waydroid-apk says whether the app installed), and its launcher entry
# runs nickel-waydroid-open (Settings, before Android is set up; Waydroid's
# own first launch asks for root in a window that can't work here).
# What else NickelOS adds around it (cgroup v2, the time zone, shill and the
# kernel) is in chromeos-base/nickel-waydroid and docs/vision.md.

PYTHON_COMPAT=( python3_{11..14} )
PYTHON_REQ_USE="ssl"
inherit python-single-r1

DESCRIPTION="Container-based approach to boot a full Android system on Linux systems"
HOMEPAGE="https://waydro.id https://github.com/waydroid/waydroid"
SRC_URI="https://github.com/${PN}/${PN}/archive/${PV}.tar.gz -> ${P}.tar.gz"

LICENSE="GPL-3+"
SLOT="0"
KEYWORDS="*"
REQUIRED_USE="${PYTHON_REQUIRED_USE}"

RDEPEND="${PYTHON_DEPS}
	app-containers/lxc
	net-dns/dnsmasq
	net-firewall/iptables
	$(python_gen_cond_dep '
		dev-python/dbus-python[${PYTHON_USEDEP}]
		>=dev-python/gbinder-1.3.0[${PYTHON_USEDEP}]
		dev-python/pygobject[${PYTHON_USEDEP}]
	')
"

PATCHES=(
	"${FILESDIR}"/${P}-nickel-network.patch
	"${FILESDIR}"/${P}-nickel-timezone.patch
	"${FILESDIR}"/${P}-nickel-wayland-wait.patch
	"${FILESDIR}"/${P}-waydroid-nvidia.patch
	"${FILESDIR}"/${P}-nickel-app-background.patch
)

src_prepare() {
	default
	# LXC without AppArmor would refuse the key.
	sed -e '/^lxc\.apparmor\.profile =/d' -i data/configs/config_* || die
}

src_compile() {
	:
}

src_install() {
	python_fix_shebang waydroid.py
	emake DESTDIR="${D}" PREFIX="${EPREFIX}"/usr \
		USE_SYSTEMD=0 USE_NFTABLES=0 USE_DBUS_ACTIVATION=0 install
	python_optimize "${ED}"/usr/lib/waydroid
	rm "${ED}"/usr/share/applications/waydroid.app.install.desktop || die
	sed -i -e 's|^Exec=waydroid$|Exec=/usr/libexec/nickel-waydroid/nickel-waydroid-open|' \
		-e 's|^Actions=stop;initialize;$|Actions=stop;|' \
		-e '/^\[Desktop Action initialize\]$/,$d' \
		"${ED}"/usr/share/applications/Waydroid.desktop || die
	grep -q nickel-waydroid-open "${ED}"/usr/share/applications/Waydroid.desktop || die
}
