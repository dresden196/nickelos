# Apply this overlay's patches/chromeos-base/chromeos-chrome/*.patch to Chrome.
#
# SERVER_SOURCE (the ebuild syncs Chrome itself): sync_chrome --reset drops
# local changes, then the ebuild applies PATCHES from Chrome's src/; ours are
# added to it.
#
# LOCAL_SOURCE: nickel-build-stock builds the existing checkout when it is
# already at the ebuild's commit, because every sync_chrome re-downloads
# Chrome's prebuilt toolchains (gclient --force), which makes ninja rebuild
# nearly everything. The checkout then still carries the previous build's
# patches. A state file in it records which patch set is applied; if the set
# changed, the files the old set touched are restored from git and the new set
# is applied, so only those files (and what includes them) are rebuilt. If the
# set is unchanged nothing is touched.

NICKEL_OVERLAY_ROOT=$(realpath "$(dirname "${BASH_SOURCE[0]}")/../../..")

# Chromium's field-trial testing config (testing/variations) turns on Google's
# experiments in every build that is not Google-branded: Gemini in Chrome,
# Help me write, AI Mode on the new tab page and more, all needing Google's
# servers. NickelOS builds without it, so every feature is as its code says
# by default. (The bashrc is read in every phase: add the argument once.)
case " ${EXTRA_GN_ARGS} " in
*" disable_fieldtrial_testing_config=true "*) ;;
*) EXTRA_GN_ARGS="${EXTRA_GN_ARGS:+${EXTRA_GN_ARGS} }disable_fieldtrial_testing_config=true" ;;
esac

nickel_chrome_patch_dir() {
	echo "${NICKEL_OVERLAY_ROOT}/patches/chromeos-base/chromeos-chrome"
}

nickel_chrome_state() {
	echo "${CHROME_ROOT}/src/.nickel-patches"
}

# First line: hash of the patch set (with the translations that go in after
# it). Then every file the set touches.
nickel_chrome_patch_set() {
	local dir
	dir=$(nickel_chrome_patch_dir)
	cat "${dir}"/*.patch "${NICKEL_OVERLAY_ROOT}"/scripts/i18n/*.py \
		"${NICKEL_OVERLAY_ROOT}"/i18n/chrome/*.json 2>/dev/null |
		sha256sum | cut -d' ' -f1
	sed -n 's|^+++ b/||p' "${dir}"/*.patch 2>/dev/null | sort -u
}

nickel_chrome_git() {
	git -c safe.directory='*' -C "${CHROME_ROOT}/src" "$@"
}

# Chrome's checkout holds other repositories (third_party/cros_system_api,
# v8, ...) that src's own git does not track. nickel_chrome_split FILE...
# sorts files (relative to src) into NICKEL_TRACKED (with the repository
# each belongs to, "repo|file") and NICKEL_UNTRACKED, asking each repository
# once: one git call a file took ten minutes for a patch set (2026-10-28).
nickel_chrome_split() {
	local f d r
	local -A rel=()
	NICKEL_TRACKED=() NICKEL_UNTRACKED=()
	for f in "$@"; do
		[[ -n ${f} ]] || continue
		d=.
		[[ ${f} == */* ]] && d=${f%/*}
		while [[ ${d} != . && ! -e ${CHROME_ROOT}/src/${d}/.git ]]; do
			if [[ ${d} == */* ]]; then d=${d%/*}; else d=.; fi
		done
		rel[${d}]+="${f#"${d}"/}"$'\n'
	done
	for r in "${!rel[@]}"; do
		local -a want have
		local -A is=()
		mapfile -t want < <(printf '%s' "${rel[${r}]}")
		mapfile -d '' -t have < <(git -c safe.directory='*' \
			--literal-pathspecs -C "${CHROME_ROOT}/src/${r}" \
			ls-files -z -- "${want[@]}")
		for f in "${have[@]}"; do is[${f}]=1; done
		for f in "${want[@]}"; do
			if [[ -n ${is[${f}]:-} ]]; then
				NICKEL_TRACKED+=("${r}|${f}")
			elif [[ ${r} == . ]]; then
				NICKEL_UNTRACKED+=("${f}")
			else
				NICKEL_UNTRACKED+=("${r}/${f}")
			fi
		done
	done
}

# Tracked files back as git has them, one git call a repository.
nickel_chrome_restore() {
	local t r
	local -A in=()
	for t in "$@"; do
		in[${t%%|*}]+="${t#*|}"$'\n'
	done
	for r in "${!in[@]}"; do
		local -a files
		mapfile -t files < <(printf '%s' "${in[${r}]}")
		git -c safe.directory='*' --literal-pathspecs \
			-C "${CHROME_ROOT}/src/${r}" checkout -q HEAD -- "${files[@]}" ||
			return 1
	done
}

# Run by hand, emerge-nickel chromeos-chrome is SERVER_SOURCE: sync_chrome
# --reset runs gclient revert, which deletes the downloaded toolchains and
# node modules, and the sync after it extracts them again with new times, so
# ninja rebuilds nearly all of Chrome. Stopped in between (2026-10-07), it
# left the checkout without clang or node. When the checkout is already at the
# commit the ebuild pins, there is nothing to sync: refuse, and build it as
# nickel-build-stock does. NICKEL_CHROME_ALLOW_SYNC=1 syncs anyway.
cros_pre_src_unpack_nickel_chrome() {
	[[ ${CHROME_ORIGIN} == SERVER_SOURCE ]] || return 0
	[[ ${NICKEL_CHROME_ALLOW_SYNC:-0} == 1 ]] && return 0
	local src="${PORTAGE_ACTUAL_DISTDIR:-${DISTDIR}}/chrome-src/src" head
	head=$(git -c safe.directory='*' -C "${src}" rev-parse HEAD 2>/dev/null) ||
		return 0
	[[ ${head} == "${GIT_COMMIT}" ]] || return 0
	die "nickel: Chrome's checkout is already at ${GIT_COMMIT:0:12}; build it" \
		"with CHROME_ORIGIN=LOCAL_SOURCE" \
		"CHROME_ROOT=/var/cache/chromeos-cache/distfiles/chrome-src" \
		"(as nickel-build-stock does), or NICKEL_CHROME_ALLOW_SYNC=1 to sync"
}

cros_pre_src_prepare_nickel_chrome() {
	local dir state want have
	dir=$(nickel_chrome_patch_dir)
	case ${CHROME_ORIGIN} in
	SERVER_SOURCE)
		[[ -d ${dir} ]] && PATCHES+=( "${dir}" )
		;;
	LOCAL_SOURCE)
		state=$(nickel_chrome_state)
		want=$(nickel_chrome_patch_set | head -n1)
		have=$(head -n1 "${state}" 2>/dev/null)
		if [[ ${have} == "${want}" ]]; then
			einfo "nickel: Chrome patch set ${want:0:12} already applied"
			return 0
		fi
		# Restore what the previous set touched, plus any other local
		# change to tracked files (e.g. a set applied before the state
		# file existed), and every file the new set touches: a set that
		# stopped halfway leaves no state file, and its changes in the
		# other repositories (third_party/cros_system_api, ...) don't show
		# in src's own git diff (2026-10-28). Then drop files that set added.
		local -a files
		mapfile -t files < <({
			tail -n +2 "${state}" 2>/dev/null
			nickel_chrome_git diff --name-only HEAD
			nickel_chrome_patch_set | tail -n +2
		} | sort -u)
		# Keep a copy of every file either set touches, so that files the new
		# set leaves as they were get their time back afterwards and ninja
		# does not rebuild what includes them.
		local f n=0 keep="${T}/nickel-keep"
		local -a all
		mapfile -t all < <({
			printf '%s\n' "${files[@]}"
			nickel_chrome_patch_set | tail -n +2
		} | sort -u)
		for f in "${all[@]}"; do
			[[ -n ${f} && -f ${CHROME_ROOT}/src/${f} ]] || continue
			mkdir -p "${keep}/${f%/*}" || die
			cp -p "${CHROME_ROOT}/src/${f}" "${keep}/${f}" || die
		done
		nickel_chrome_split "${files[@]}"
		nickel_chrome_restore "${NICKEL_TRACKED[@]}" || die
		for f in "${NICKEL_UNTRACKED[@]}"; do
			rm -f "${CHROME_ROOT}/src/${f}" || die
		done
		einfo "nickel: restored ${#NICKEL_TRACKED[@]} files before applying ${want:0:12}"
		# Files the new set creates must not exist yet (left over from a
		# previous set, or copied in for a compile check).
		mapfile -t files < <(nickel_chrome_patch_set | tail -n +2)
		nickel_chrome_split "${files[@]}"
		for f in "${NICKEL_UNTRACKED[@]}"; do
			rm -f "${CHROME_ROOT}/src/${f}" || die
		done
		rm -f "${state}" || die
		if [[ -d ${dir} ]]; then
			pushd "${CHROME_ROOT}/src" >/dev/null || die
			eapply "${dir}"
			popd >/dev/null || die
		fi
		n=0
		for f in "${all[@]}"; do
			[[ -n ${f} && -f ${keep}/${f} && -f ${CHROME_ROOT}/src/${f} ]] ||
				continue
			if cmp -s "${keep}/${f}" "${CHROME_ROOT}/src/${f}"; then
				touch -r "${keep}/${f}" "${CHROME_ROOT}/src/${f}" || die
				n=$((n + 1))
			fi
		done
		einfo "nickel: ${n} files came out unchanged and keep their time"
		;;
	esac
}

# Both modes: write NickelOS's translations of its wording into Chrome's
# .xtb files (scripts/i18n/nickel-translations.py, from i18n/chrome/*.json),
# then record the applied set, with the .xtb files the translations touch, for
# the next LOCAL_SOURCE build to restore.
cros_post_src_prepare_nickel_chrome() {
	local f n=0 xtb="${T}/nickel-xtb" keep="${T}/nickel-keep"
	case ${CHROME_ORIGIN} in
	SERVER_SOURCE|LOCAL_SOURCE)
		: > "${xtb}" || die
		NICKEL_CHROME_SRC="${CHROME_ROOT}/src" python3 \
			"${NICKEL_OVERLAY_ROOT}/scripts/i18n/nickel-translations.py" \
			--list "${xtb}" ||
			die "nickel: translations not written"
		{
			nickel_chrome_patch_set
			cat "${xtb}"
		} | awk 'NR == 1 { print; next } !seen[$0]++' \
			> "$(nickel_chrome_state)" || die
		# The restore took these back too: one that came out as it was
		# keeps its time (and grit does not redo its language).
		while read -r f; do
			[[ -f ${keep}/${f} ]] || continue
			if cmp -s "${keep}/${f}" "${CHROME_ROOT}/src/${f}"; then
				touch -r "${keep}/${f}" "${CHROME_ROOT}/src/${f}" || die
				n=$((n + 1))
			fi
		done < "${xtb}"
		einfo "nickel: translations in $(wc -l < "${xtb}") .xtb files," \
			"${n} unchanged"
		;;
	esac
}
