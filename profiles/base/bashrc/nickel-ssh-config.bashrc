# ssh's client config (chromeos-base/chromeos-base installs ChromeOS's,
# files/ssh_config). ChromeOS's keeps known hosts in
# /home/chronos/user/.ssh, Chrome's profile folder, where crosh's ssh ran.
# On NickelOS ssh runs as the account's Linux user, whose home is
# /home/<user>, and couldn't write there: "Failed to add the host to the
# list of known hosts (/home/chronos/user/.ssh/known_hosts)" (Dresden,
# 2026-11-09). OpenSSH's defaults instead (~/.ssh/known_hosts), and the
# settings packages put in /etc/ssh/ssh_config.d (Gentoo's openssh), as on
# Gentoo.
cros_post_src_install_nickel_ssh_config() {
	[[ -f ${D}/etc/ssh/ssh_config ]] || return 0
	cat > "${D}/etc/ssh/ssh_config" <<-'CONF' || die
	# NickelOS: OpenSSH's defaults (known hosts in ~/.ssh/known_hosts).
	# Your own settings go in ~/.ssh/config, packages' in
	# /etc/ssh/ssh_config.d.
	Include /etc/ssh/ssh_config.d/*.conf
	CONF
	einfo "nickel: /etc/ssh/ssh_config with OpenSSH's defaults"
}
