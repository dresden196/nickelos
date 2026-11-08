# Distributed under the terms of the GNU General Public License v2
#
# build_image's board hook for the kernel's command line (the GRUB and
# syslinux templates, and the kernel partitions' own command line).
#   $1: the file of arguments, one per line.
modify_kernel_command_line() {
	# simpledrm is built in for the ISO's installer (its GRUB leaves it on,
	# profiles/base/bashrc/nickel-kernel.bashrc); NickelOS's own boots
	# start without it, as before it was built in.
	echo "initcall_blacklist=simpledrm_platform_driver_init" >> "$1"
}
