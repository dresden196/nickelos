# Kernel config NickelOS needs on top of reven's splitconfig.
#
# cros-kernel only enables option groups (fragments) it defines itself, behind
# USE flags it declares, so a board overlay cannot add one. This hook edits the
# config after cros-kernel_src_configure and re-normalizes it; cros-kernel
# explicitly supports that ("ebuild touched config"), it just skips its
# incremental-config shortcut.
#
# CONFIG_VIRTIO_BLK=y: reven builds it as a module and boots without an
#   initramfs, so a root disk on virtio (Proxmox, libvirt and QEMU defaults)
#   is never found and boot hangs forever.
#
# CONFIG_SECURITY_CHROMIUMOS_NO_UNPRIVILEGED_UNSAFE_MOUNTS off: the ChromiumOS
#   LSM otherwise refuses any mount with exec/suid/dev made inside an
#   unprivileged user namespace ("requires CAP_SYS_ADMIN in init ns"), which
#   breaks bubblewrap and so every Flatpak app. Off = standard Linux behavior:
#   such mounts stay confined to the user's own namespaces.
#
# CONFIG_SECURITY_CHROMIUMOS_NO_SYMLINK_MOUNT off: otherwise mount() fails
#   with ELOOP whenever the target path contains any symlink. bubblewrap
#   applies mount flags through /proc/self/fd/N (a symlink), so Flatpak's
#   sandbox setup fails ("Can't bind mount / on /: Too many levels of
#   symbolic links"). Off = standard Linux mount semantics.
#
# CONFIG_BOOTPARAM_HUNG_TASK_PANIC off: ChromeOS panics (and, with
#   PANIC_TIMEOUT=-1, reboots at once) when a task waits on I/O for 120 s.
#   On a PC that only means slow storage: a sync to a cheap USB stick
#   rebooted the NVIDIA test PC mid-write twice (2026-10-06), losing the
#   data and leaving ext2 inconsistent. Hung tasks are still detected and
#   logged; softlockup and hardlockup panics, real kernel bugs, stay.
#
# CONFIG_ANDROID_BINDERFS=y: Android in Waydroid talks over binder. The
#   kernel's three fixed binder devices (binder, hwbinder, vndbinder) are
#   enough for one Android; binderfs gives each container instance its own,
#   as Waydroid prefers.

#
# CONFIG_HSA_AMD=y: AMD's compute interface (/dev/kfd), which ROCm and HIP
#   need: PyTorch and other AI tools on AMD, Blender's HIP renderer, Gentoo's
#   ROCm packages. ChromeOS builds amdgpu without it (no /dev/kfd on
#   the AMD test PC, 2026-10-21). Vulkan compute (llama.cpp) works without it.
#   HSA_AMD_SVM (HIP's managed memory) needs DEVICE_PRIVATE, which needs
#   memory hotplug, off in ChromeOS's config: not yet.
#
# CONFIG_HID_PLAYSTATION=m (+ PLAYSTATION_FF, and LEDS_CLASS_MULTICOLOR,
#   which it needs for the light bar): Sony's DualShock 4 and DualSense as
#   proper gamepads with rumble, touchpad and motion sensors. ChromeOS
#   leaves it out; games outside Steam saw a bare HID device.
#
# CONFIG_HID_STEAM=m: the Steam Controller and Steam Deck's controls as a
#   gamepad without Steam running (with Steam, Steam drives them as before).
#
# CONFIG_NTSYNC=y: /dev/ntsync, Windows' synchronization objects for Wine
#   and Proton (patches/sys-kernel/chromeos-kernel-6_12/0003 brings the
#   working driver from 6.18). Built in, so the device is always there.
#
# CONFIG_SYSFB_SIMPLEFB=y, CONFIG_DRM_SIMPLEDRM=y: a display on the
#   firmware's framebuffer, on any GPU, before (or without) its driver: the
#   ISO's installer draws there (scripts/iso). NickelOS itself starts with
#   it off, as before (scripts/build_kernel_image.sh: initcall_blacklist).
# CONFIG_ISO9660_FS, FAT_FS, VFAT_FS, EXFAT_FS, NTFS3_FS and the NLS tables
#   they use (437, ISO 8859-1, UTF-8) =y: the ISO's start-up finds the
#   medium on these (a disc or a stick written as it is, Rufus's FAT stick,
#   Ventoy's exFAT or NTFS one), and a module loaded from its initramfs would
#   pin LoadPin to the initramfs: the live session it starts could then load
#   no module of its own (graphics, Wi-Fi, sound). Built in, the start-up
#   loads none (scripts/iso).
NICKEL_KERNEL_CONFIG=(
	CONFIG_VIRTIO_BLK=y
	CONFIG_HSA_AMD=y
	CONFIG_LEDS_CLASS_MULTICOLOR=y
	CONFIG_HID_PLAYSTATION=m
	CONFIG_PLAYSTATION_FF=y
	CONFIG_HID_STEAM=m
	CONFIG_NTSYNC=y
	"# CONFIG_SECURITY_CHROMIUMOS_NO_UNPRIVILEGED_UNSAFE_MOUNTS is not set"
	"# CONFIG_SECURITY_CHROMIUMOS_NO_SYMLINK_MOUNT is not set"
	"# CONFIG_BOOTPARAM_HUNG_TASK_PANIC is not set"
	CONFIG_ANDROID_BINDERFS=y
	CONFIG_SYSFB_SIMPLEFB=y
	CONFIG_DRM_SIMPLEDRM=y
	CONFIG_ISO9660_FS=y
	CONFIG_FAT_FS=y
	CONFIG_VFAT_FS=y
	CONFIG_EXFAT_FS=y
	CONFIG_NTFS3_FS=y
	CONFIG_NLS_CODEPAGE_437=y
	CONFIG_NLS_ISO8859_1=y
	CONFIG_NLS_UTF8=y
)

cros_post_src_configure_nickel_kernel_config() {
	local cfg opt
	cfg="$(get_build_cfg)"
	printf '%s\n' "${NICKEL_KERNEL_CONFIG[@]}" >> "${cfg}" || die
	kmake olddefconfig
	for opt in "${NICKEL_KERNEL_CONFIG[@]}"; do
		grep -qx "${opt}" "${cfg}" || die "nickel: ${opt} did not survive olddefconfig"
	done
	einfo "nickel: applied ${NICKEL_KERNEL_CONFIG[*]}"
}
