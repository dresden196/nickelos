# Apply this overlay's patches/<category>/<package>/*.patch to packages that
# live in other overlays (platform2, the kernel and friends; Chrome has its own
# nickel-chrome.bashrc). NickelOS changes to Google's code stay reviewable
# patch files here instead of forks of Google's repos.
#
# Patches are made against the package's source root. For platform2 packages
# that is the platform2 checkout; S points into its PLATFORM_SUBDIR.
# Map a package to this file in profiles/base/package.bashrc to enable it.

NICKEL_OVERLAY_ROOT=$(realpath "$(dirname "${BASH_SOURCE[0]}")/../../..")

nickel_patch_dir() {
	echo "${NICKEL_OVERLAY_ROOT}/patches/${CATEGORY}/${PN}"
}

# Out-of-tree cros-workon builds (the kernel, dev-install) point S at the
# checkout in src/ itself, so patching would edit Google's tree. Build patched
# packages in-tree instead: cros-workon then clones the pinned commit into the
# work dir. Set here rather than at file scope because bashrcs are sourced
# before the ebuild, which may set it to 1.
cros_pre_src_unpack_nickel_patches() {
	[[ -d $(nickel_patch_dir) ]] || return 0
	[[ ${CROS_WORKON_OUTOFTREE_BUILD} == 1 ]] || return 0
	CROS_WORKON_OUTOFTREE_BUILD=0
	# In-tree platform2 builds keep the checkout under ${S}/platform2.
	if [[ -n ${PLATFORM_SUBDIR} && -z ${CROS_WORKON_DESTDIR} ]]; then
		CROS_WORKON_DESTDIR="${S}/platform2"
	fi
}

cros_pre_src_prepare_nickel_patches() {
	local dir root="${S}"
	dir=$(nickel_patch_dir)
	[[ -d ${dir} ]] || return 0
	[[ ${S} == "${CHROOT_SOURCE_ROOT:-/mnt/host/source}"/* ]] &&
		die "nickel: ${S} is the source checkout itself; refusing to patch it"
	[[ -n ${PLATFORM_SUBDIR} ]] && root="${S%/"${PLATFORM_SUBDIR}"}"
	pushd "${root}" >/dev/null || die
	eapply "${dir}"
	# The kernel names itself from its git tree (scripts/setlocalversion), and
	# changes not committed make it "-dirty" (6.12.100-01493-g5e3b3561121e-dirty
	# in 157). A commit can't be made here: the work tree borrows Google's
	# objects (git clone --shared), and git touches those packs, which the
	# sandbox refuses. So the files our patches changed are marked
	# assume-unchanged in the work tree's own index: setlocalversion's
	# `git status -uno` doesn't list them, the version is Google's base commit
	# without -dirty, and nothing outside the work dir is written. (Which
	# patches went in: /usr/share/nickel/patched.)
	if [[ ${CATEGORY} == sys-kernel ]] && git -C "${root}" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
		git -C "${root}" ls-files -z -m -d | xargs -0 -r git -C "${root}" update-index --assume-unchanged -- || die
		[[ -z $(git -C "${root}" --no-optional-locks status -uno --porcelain) ]] || die "the kernel tree still looks changed"
	fi
	popd >/dev/null || die
}

# A record in the package that the patches went in, and which (their hash):
# /usr/share/nickel/patched/<category>__<package>. A binary package from
# Google's binhost has none, so nickel-build-stock checks every patched
# package for it after build-packages.
cros_post_src_install_nickel_patches() {
	local dir LC_ALL=C
	dir=$(nickel_patch_dir)
	[[ -d ${dir} ]] || return 0
	# The kernel's also says its version is clean (no -dirty), so kernels
	# patched before that are rebuilt (nickel-build-stock: the same).
	{ cat "${dir}"/*.patch; [[ ${CATEGORY} == sys-kernel ]] && echo "clean version"; } |
		sha256sum | cut -c1-16 > "${T}/nickel-patched" || die
	insinto /usr/share/nickel/patched
	newins "${T}/nickel-patched" "${CATEGORY}__${PN}"
}
