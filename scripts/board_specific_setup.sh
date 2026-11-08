# NickelOS image hooks. build_image sources this file (see
# load_board_specific_script in src/scripts/common.sh) and calls the hooks
# defined here.

# Called by create_base_image (src/scripts/build_library/base_image_util.sh)
# with the image's root filesystem mounted at ${root_fs_dir}.
board_finalize_base_image() {
  nickel_dbus_policies_for_group_chronos &&
  nickel_no_dev_install &&
  nickel_unified
}

# NickelOS (docs/unified.md, "Step 3 in the real build"): merged
# /usr, Gentoo's packages for the ones Gentoo shares with ChromeOS, Portage
# and the records of every package, by the Unified Gentoo root's Portage
# (scripts/unified/nickel-unify). After the D-Bus and dev_install hooks, which
# work on the image as build_image made it. build_image labels everything by
# path after these hooks (restore_fs_contexts, with the board's
# file_contexts, which nickel-unify gives merged /usr's rules), then hashes
# and signs the image.
nickel_unified() {
  local root=${root_fs_dir:?}
  local tool
  tool="$(dirname "${BASH_SOURCE[0]}")/unified/nickel-unify"
  sudo bash "${tool}" "${root}" "${BOARD_ROOT:?}" || return 1
  info "nickel: NickelOS (merged /usr, Gentoo's packages, records)"
}

# ChromiumOS's own Portage on the device (dev_install) has no place on
# NickelOS, where Gentoo's Portage is the OS's (docs/unified.md): its /etc/portage
# (build_image's link to /usr/share/dev-install/portage) would be the OS's,
# which wins over Gentoo's /etc/portage, and its command-not-found hook
# tells people to run dev_install. Nor do build_image's links into
# /usr/local for a Portage and file(1) installed there (/usr/share/portage,
# /usr/lib/portage, /usr/share/misc/magic.mgc): as the OS's paths they hid
# Gentoo's Portage files and file's database. Out of the image before
# nickel-unify installs Gentoo's Portage.
nickel_no_dev_install() {
  local root=${root_fs_dir:?} p
  if [[ -L ${root}/etc/portage ]]; then
    sudo rm -f "${root}/etc/portage"
  fi
  sudo rm -rf "${root}/usr/share/dev-install" "${root}/usr/bin/dev_install" \
    "${root}/etc/bash/bashrc.d/dev-install.sh" "${root}/etc/env.d/99devinstall"
  for p in usr/share/portage usr/lib/portage usr/share/misc/magic.mgc; do
    if [[ -L ${root}/${p} && $(readlink "${root}/${p}") == /usr/local/* ]]; then
      sudo rm -f "${root}/${p}"
    fi
  done
  if [[ -e ${root}/etc/portage ]]; then
    error "nickel: the image has an /etc/portage of its own"
    return 1
  fi
  info "nickel: no dev_install; /etc/portage is Gentoo's"
}

# A local account's session runs Chrome as the account's Linux user, a member
# of group chronos (nickel-accountsd), instead of chronos. D-Bus policies that
# let Chrome reach system services name the user chronos; they name the group
# instead. Group chronos holds only chronos and local accounts.
nickel_dbus_policies_for_group_chronos() {
  local root=${root_fs_dir:?}
  local dirs=() d
  for d in etc/dbus-1/system.d usr/share/dbus-1/system.d opt/google/chrome/dbus; do
    [[ -d ${root}/${d} ]] && dirs+=( "${root}/${d}" )
  done
  [[ ${#dirs[@]} -gt 0 ]] || return 0

  local files=()
  mapfile -t files < <(sudo grep -rlE "user=[\"']chronos[\"']" "${dirs[@]}")
  if [[ ${#files[@]} -gt 0 ]]; then
    sudo sed -i -E "s/user=([\"'])chronos([\"'])/group=\1chronos\2/g" \
      "${files[@]}" || return 1
  fi
  info "nickel: ${#files[@]} D-Bus policy files now grant group chronos"
  if sudo grep -rqE "user=[\"']chronos[\"']" "${dirs[@]}"; then
    error "nickel: D-Bus policies still name user chronos"
    return 1
  fi
}

