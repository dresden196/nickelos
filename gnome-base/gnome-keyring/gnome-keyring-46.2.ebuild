# Copyright 1999-2026 Gentoo Authors
# Distributed under the terms of the GNU General Public License v2

EAPI=7

# NickelOS: from Gentoo gnome-keyring-48.0-r1 (EAPI 8 -> 7), at 46.2, the
# last release that builds with R156's GLib 2.76 (48 needs 2.80). The Secret
# Service (org.freedesktop.secrets) and the Secret portal's backend for
# Flatpak apps; no PAM (ChromeOS's sign-in does not go through it), ssh-agent,
# systemd, SELinux, libcap-ng or docs. The session bus starts it through
# nickel-keyring (files/), which unlocks the login keyring; nothing starts
# it at sign-in, and only its secrets component runs (no PKCS#11 module for
# other programs to load).

inherit gnome.org gnome2-utils xdg

DESCRIPTION="Password and keyring managing daemon"
HOMEPAGE="https://gitlab.gnome.org/GNOME/gnome-keyring"

LICENSE="GPL-2+ LGPL-2+"
SLOT="0"
KEYWORDS="*"
RESTRICT="test"

RDEPEND="
	>=dev-libs/glib-2.44:2
	>=app-crypt/gcr-3.27.90:0=
	>=dev-libs/libgcrypt-1.2.2:0=
	app-crypt/p11-kit
"
DEPEND="${RDEPEND}"
BDEPEND="
	dev-util/glib-utils
	>=sys-devel/gettext-0.19.8
	virtual/pkgconfig
"

src_configure() {
	econf \
		--disable-pam \
		--disable-ssh-agent \
		--without-libcap-ng \
		--disable-selinux \
		--without-systemd \
		--disable-doc \
		--disable-p11-tests \
		--with-dbus-services="${EPREFIX}"/usr/share/dbus-1/services \
		LIBGCRYPT_CONFIG="${SYSROOT}"/usr/bin/libgcrypt-config
}

src_install() {
	default
	find "${ED}" -name '*.la' -delete || die
	# Started by the session bus only, through nickel-keyring.
	rm -r "${ED}"/etc/xdg/autostart || die
	rm -f "${ED}"/usr/share/p11-kit/modules/gnome-keyring.module || die
	local s
	for s in org.gnome.keyring org.freedesktop.secrets \
			org.freedesktop.impl.portal.Secret; do
		sed -i 's|^Exec=.*|Exec=/usr/libexec/nickel-keyring|' \
			"${ED}"/usr/share/dbus-1/services/${s}.service || die
	done
	exeinto /usr/libexec
	doexe "${FILESDIR}"/nickel-keyring
}

pkg_postinst() {
	xdg_pkg_postinst
	gnome2_schemas_update
}

pkg_postrm() {
	xdg_pkg_postrm
	gnome2_schemas_update
}
