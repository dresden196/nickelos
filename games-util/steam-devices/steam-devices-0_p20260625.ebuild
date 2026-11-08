# Distributed under the terms of the GNU General Public License v2

EAPI=7

inherit udev

DESCRIPTION="Valve's udev rules for Steam controllers and VR headsets (NickelOS: group input)"
HOMEPAGE="https://github.com/ValveSoftware/steam-devices"
LICENSE="MIT"
SLOT="0"
KEYWORDS="*"

S="${WORKDIR}"

# The rules (files/, from ValveSoftware/steam-devices commit 22ec85e5) give
# the logged-in user access with TAG+="uaccess", which needs logind; ChromeOS
# has none. On NickelOS the session user is in group input (as chronos is),
# so the devices go to that group instead: GROUP:= keeps ChromeOS's later
# rules (80-hidraw-group.rules) from taking them back. /dev/uinput, for Steam
# Input's virtual controllers, keeps its group uinput (ChromeOS daemons use
# it) and gives group input read/write through an ACL, as uaccess would.
# The one rule that runs udev's uaccess builtin by hand (for an emulated
# "Lic Pro Controller") is left out.
# ChromeOS's license check finds MIT's copyright notice in the source dir.
src_unpack() {
	cp "${FILESDIR}"/LICENSE "${S}"/ || die
}

src_install() {
	local f
	for f in 60-steam-input.rules 60-steam-vr.rules; do
		sed -e 's|TAG+="uaccess", OPTIONS+="static_node=uinput"|OPTIONS+="static_node=uinput", RUN+="/bin/setfacl -m g:input:rw /dev/%k"|' \
			-e 's|TAG+="uaccess"|GROUP:="input"|g' \
			-e '/test-builtin uaccess/d' \
			"${FILESDIR}/${f}" > "${T}/${f}" || die
		if grep -q uaccess "${T}/${f}"; then
			die "${f}: uaccess left"
		fi
		udev_dorules "${T}/${f}"
	done
}
