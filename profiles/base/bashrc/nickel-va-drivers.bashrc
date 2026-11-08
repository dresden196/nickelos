# NickelOS: VA-API drivers where Gentoo's libva looks for them.
#
# libva is Gentoo's in NickelOS (scripts/unified/gentoo/swap.txt, since
# 2026-10-19), and Gentoo's libva looks for its drivers in /usr/lib64/dri,
# where Gentoo's VA-API drivers and Mesa's install them. ChromiumOS's looked
# in /usr/lib64/va/drivers, and the drivers built against it here still went
# there, so vaInitialize found none on Intel and NVIDIA GPUs: Chrome decoded
# video on the CPU, and HEVC didn't play at all (the NVIDIA test PC, 16852.17.9,
# 2026-11-02). AMD's comes from Gentoo's Mesa, in /usr/lib64/dri already.
# nvidia-vaapi-driver, ours, does the same in its ebuild.
cros_post_src_install_nickel_va_drivers() {
	local from=${ED}/usr/lib64/va/drivers to=${ED}/usr/lib64/dri
	[[ -d ${from} ]] || return 0
	mkdir -p "${to}" || die
	mv "${from}"/*_drv_video.so "${to}/" || die
	rmdir "${from}" "${ED}/usr/lib64/va" 2>/dev/null
	return 0
}
