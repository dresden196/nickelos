# NickelOS's GRUB patches (patches/sys-boot/grub). They change files that
# ChromeOS's own GRUB patches add (gptpriority), which the ebuild applies in
# src_prepare, so they go on after it rather than before, as
# nickel-patches.bashrc does for other packages. They only touch C sources,
# so autogen.sh's output stays valid.

NICKEL_OVERLAY_ROOT=$(realpath "$(dirname "${BASH_SOURCE[0]}")/../../..")

cros_post_src_prepare_nickel_grub() {
	local dir="${NICKEL_OVERLAY_ROOT}/patches/${CATEGORY}/${PN}"
	[[ -d ${dir} ]] || return 0
	pushd "${S}" >/dev/null || die
	eapply "${dir}"
	popd >/dev/null || die
}

# ChromeOS's target LDFLAGS (make.conf.generic-target) fold identical code
# and drop unused sections; GRUB links its modules with -r, which lld
# refuses with either ("-r and --icf may not be used together"). Google
# ships GRUB prebuilt, so only a from-source build (needed for the patches
# above) meets this.
cros_pre_src_configure_nickel_grub() {
	filter-ldflags -Wl,--icf=all -Wl,--gc-sections
}
