# Copyright 2026 The NickelOS Authors
# Distributed under the terms of the GNU General Public License v2

# NVIDIA's driver for Turing and newer GPUs, unmodified: its open kernel
# modules (built here against the board kernel and signed with its module
# key, since NickelOS loads only signed modules from the root filesystem), the
# GSP firmware, and the userspace NickelOS uses (EGL and GLES through glvnd,
# Vulkan with its ray tracing core, the GBM allocator minigbm's nvidia-drm
# backend loads, EGL external platforms for Wayland and X11 apps, CUDA and
# NVDEC for video decode through nvidia-vaapi-driver, nvidia-smi). A modprobe
# rule picks
# nvidia-open or nouveau per GPU from NVIDIA's list of GPUs its open modules
# support (nickel-nvidia-driver). Updating = bumping PV: nothing here is
# patched. The kernel's := dependency rebuilds and re-signs the modules for
# every kernel.

EAPI=7

inherit toolchain-funcs

DESCRIPTION="NVIDIA open GPU kernel modules and userspace driver (Turing and newer)"
HOMEPAGE="https://github.com/NVIDIA/open-gpu-kernel-modules"
SRC_URI="https://download.nvidia.com/XFree86/Linux-x86_64/${PV}/NVIDIA-Linux-x86_64-${PV}.run"

# kernel-open is MIT/GPL-2; the userspace and firmware are NVIDIA's.
LICENSE="NVIDIA-r2 MIT GPL-2"
SLOT="0"
KEYWORDS="*"
RESTRICT="bindist mirror strip test"

KERNEL_PKG="sys-kernel/chromeos-kernel-6_12"
DEPEND="${KERNEL_PKG}:="
RDEPEND="
	${KERNEL_PKG}:=
	media-libs/libglvnd
	media-libs/minigbm
"

S="${WORKDIR}/NVIDIA-Linux-x86_64-${PV}"

src_unpack() {
	cd "${WORKDIR}" || die
	sh "${DISTDIR}/${A}" --extract-only >/dev/null || die "extracting ${A} failed"
}

src_compile() {
	local kernel_out="${SYSROOT}/usr/src/linux"
	# The kernel's build directory points at its long-gone portage work dir
	# for sources; the cros-workon checkout is the same commit.
	local kernel_src="${CHROOT_SOURCE_ROOT:-/mnt/host/source}/src/third_party/kernel/v6.12"
	[[ -f ${kernel_src}/Makefile ]] || die "no kernel sources at ${kernel_src}"

	# kbuild reads ARCH, CFLAGS and LDFLAGS from the environment; portage's
	# values (ARCH=amd64, user flags) are not the kernel's.
	env -u CFLAGS -u CXXFLAGS -u LDFLAGS -u CPPFLAGS \
		emake -C kernel-open \
		ARCH=x86_64 LLVM=1 CC="$(tc-getCC)" LD=ld.lld \
		SYSSRC="${kernel_src}" SYSOUT="${kernel_out}" \
		modules

	# Strip, then sign: a signature covers the file as shipped.
	local m
	for m in nvidia nvidia-modeset nvidia-drm nvidia-uvm; do
		$(tc-getSTRIP) --strip-debug "kernel-open/${m}.ko" || die
		"${kernel_out}/scripts/sign-file" sha256 \
			"${kernel_out}/certs/signing_key.pem" \
			"${kernel_out}/certs/signing_key.x509" \
			"kernel-open/${m}.ko" || die "signing ${m}.ko failed"
	done

	# NVIDIA's list of GPUs its open kernel modules drive, for
	# nickel-nvidia-driver: one PCI device ID per line.
	python3 - supported-gpus/supported-gpus.json > nvidia-open-gpus <<-'EOF' || die
	import json, sys
	chips = json.load(open(sys.argv[1]))["chips"]
	ids = {c["devid"].lower() for c in chips if "kernelopen" in c.get("features", [])}
	print("\n".join(sorted(ids)))
	EOF
	[[ -s nvidia-open-gpus ]] || die "no GPUs in NVIDIA's open-module list"
}

# Installs a versioned library and the link its SONAME names.
nvidia_dolib() {
	local lib=$1 soname
	dolib.so "${lib}"
	soname=$($(tc-getREADELF) -d "${lib}" | sed -n 's/.*(SONAME).*\[\(.*\)\]/\1/p')
	[[ -n ${soname} && ${soname} != "${lib}" ]] && dosym "${lib}" "/usr/$(get_libdir)/${soname}"
}

src_install() {
	local kver
	kver=$(<"${SYSROOT}/usr/src/linux/include/config/kernel.release") || die
	insinto "/lib/modules/${kver}/extra"
	doins kernel-open/nvidia{,-modeset,-drm,-uvm}.ko

	insinto "/lib/firmware/nvidia/${PV}"
	doins firmware/gsp_*.bin

	# libnvidia-rtcore: NVIDIA's Vulkan driver loads it for
	# VK_KHR_acceleration_structure; without it every device asking for that
	# extension failed to create (VK_ERROR_INITIALIZATION_FAILED): ray-traced
	# games, and Android's UI on Vulkan, which asks for every extension.
	# GPU compute and encoding, for the apps people install: CUDA's JIT
	# compilers (libnvidia-ptxjitcompiler, libnvidia-nvvm: CUDA programs that
	# ship PTX, most of them, failed to load their kernels with "PTX JIT
	# compiler library not found", 2026-10-21), NVENC (libnvidia-encode: OBS
	# and ffmpeg's hardware encoding) and OpenCL (libnvidia-opencl, its ICD
	# registered below). About 200 MB.
	local lib
	for lib in libEGL_nvidia libGLESv2_nvidia libGLESv1_CM_nvidia libGLX_nvidia \
		libnvidia-eglcore libnvidia-glcore libnvidia-glsi libnvidia-tls \
		libnvidia-gpucomp libnvidia-glvkspirv libnvidia-rtcore libnvidia-allocator \
		libnvidia-ml libcuda libnvcuvid \
		libnvidia-ptxjitcompiler libnvidia-nvvm libnvidia-encode libnvidia-opencl; do
		nvidia_dolib "${lib}.so.${PV}"
	done
	for lib in libnvidia-egl-gbm.so.* libnvidia-egl-wayland.so.* \
		libnvidia-egl-wayland2.so.* libnvidia-egl-xcb.so.* libnvidia-egl-xlib.so.*; do
		nvidia_dolib "${lib}"
	done
	# Mesa's GBM loader (Gentoo's Mesa, the OS's one: Xwayland's glamor and
	# Linux apps) finds NVIDIA's allocator here; minigbm loads it by SONAME.
	dosym "../libnvidia-allocator.so.1" "/usr/$(get_libdir)/gbm/nvidia-drm_gbm.so"

	# NVIDIA's EGL and Vulkan are registered only where its driver runs. On
	# any other GPU, its Vulkan driver (nvidia_icd.json) broke Chrome's GPU
	# process: GL failed to start ("OpenGL ES 2.0 is not supportable") and
	# Chrome quit ("Software compositing fallback is unavailable"), so
	# images with this package did not start on Intel, AMD or VMs. Its EGL
	# vendor file came first in glvnd's order (10_nvidia before 50_mesa).
	# Both files are links into /run, which nickel-nvidia-driver fills when
	# it loads nvidia; elsewhere they point nowhere and are skipped. Same
	# paths as before for Chrome's GPU sandbox (patch 0032).
	insinto /usr/share/nvidia/egl_vendor.d
	doins 10_nvidia.json
	dosym /run/nickel/nvidia/10_nvidia.json /usr/share/glvnd/egl_vendor.d/10_nvidia.json
	insinto /usr/share/nvidia/vulkan/icd.d
	doins nvidia_icd.json
	dosym /run/nickel/nvidia/nvidia_icd.json /usr/share/vulkan/icd.d/nvidia_icd.json
	# NVIDIA's OpenCL for the OpenCL loader (ocl-icd), the same way: a link
	# into /run, filled only where NVIDIA's driver runs.
	insinto /usr/share/nvidia/OpenCL/vendors
	doins nvidia.icd
	dosym /run/nickel/nvidia/nvidia.icd /etc/OpenCL/vendors/nvidia.icd
	# Read by NVIDIA's EGL only.
	insinto /usr/share/egl/egl_external_platform.d
	doins 09_nvidia_wayland2.json 10_nvidia_wayland.json 15_nvidia_gbm.json \
		20_nvidia_xcb.json 20_nvidia_xlib.json

	dobin nvidia-smi

	insinto /usr/share/nickel
	doins nvidia-open-gpus
	dosbin "${FILESDIR}/nickel-nvidia-driver"
	insinto /etc/modprobe.d
	doins "${FILESDIR}/nickel-nvidia.conf"

	dodoc LICENSE
}

pkg_postinst() {
	local kver
	kver=$(<"${SYSROOT}/usr/src/linux/include/config/kernel.release") || die
	depmod -b "${ROOT:-/}" -a "${kver}" || die "depmod failed"
}
