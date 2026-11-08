# Distributed under the terms of the GNU General Public License v2
#
# NickelOS: login shells of the signed-in user get the session environment
# (Wayland display, session D-Bus bus, runtime dir), so GUI and Flatpak apps
# start from the terminal like they do from the launcher.

if [ -z "${WAYLAND_DISPLAY}" ] && [ -r /run/nickel/session/env ] &&
   [ "$(sed -n 's/^NICKEL_SESSION_USER=//p' /run/nickel/session/env)" = "$(id -un)" ]; then
	while IFS='=' read -r _nickel_k _nickel_v; do
		case "${_nickel_k}" in
		XDG_RUNTIME_DIR|XDG_SESSION_TYPE|XDG_CURRENT_DESKTOP|WAYLAND_DISPLAY|\
		DISPLAY|XAUTHORITY|XDG_SESSION_ID|XDG_SEAT|DBUS_SESSION_BUS_ADDRESS|XDG_DATA_DIRS|\
		GTK_USE_PORTAL|GDK_DEBUG|XMODIFIERS)
			export "${_nickel_k}=${_nickel_v}" ;;
		esac
	done < /run/nickel/session/env
	unset _nickel_k _nickel_v
fi

# Per-user programs (pip --user, npm prefixes, installers such as Claude
# Code's) go to ~/.local/bin; it comes first in PATH, as on most
# distributions.
case ":${PATH}:" in
*":${HOME}/.local/bin:"*) ;;
*) PATH="${HOME}/.local/bin:${PATH}"; export PATH ;;
esac
