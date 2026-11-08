# Distributed under the terms of the GNU General Public License v2
#
# Hooks for every package built for NickelOS (ChromiumOS's profile.bashrc calls
# each cros_<phase>_* function at that phase).

# Images are built with chromite's INSTALL_MASK, which leaves out
# /usr/share/applications: ChromeOS itself needs no .desktop files. NickelOS's
# Linux apps do: they are how xdg-open, GIO and the OpenURI portal find which
# app opens a link or a file (org.nickelos.ChromeOS.desktop and
# nickelos-mimeapps.list hand web links to Chrome), and how apps find their
# own. Without them every link a Linux app opens went nowhere (Slack's "Sign
# in", 2026-10-06). Keep a copy beside it, where the mask does not reach;
# nickel-session puts /usr/share/nickel in XDG_DATA_DIRS.
cros_post_src_install_nickel_applications() {
	[[ -d ${ED}/usr/share/applications ]] || return 0
	mkdir -p "${ED}/usr/share/nickel" || die
	cp -a "${ED}/usr/share/applications" "${ED}/usr/share/nickel/" || die
}
