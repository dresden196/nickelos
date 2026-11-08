# NickelOS: Google's Mesa binds its own symbols to itself.
#
# Mesa is Gentoo's in NickelOS. Of Google's build only radeonsi_dri.so
# stays, for minigbm's amdgpu backend (scripts/unified/nickel-unify-records.py,
# MESA_KEEP). The driver exports amdgpu_winsys_create and
# radeon_drm_winsys_create, and its own calls to them went through the PLT.
# Gentoo's libgallium, already loaded in Chrome's GPU process, exports the
# same two names, so Google's Mesa 24 driver called Gentoo's Mesa 26 winsys:
# "ac: Unknown GPU", no GBM device, nothing on screen (the AMD test PC, 2026-10-20).
# Linked -Bsymbolic, the driver's references to what it defines itself bind
# to itself; what it imports (libc, libdrm, the C++ runtime) binds as before,
# so Chrome's malloc stays everyone's. RTLD_DEEPBIND would have done the
# first, but also bound the driver's free() to glibc's beside Chrome's
# allocator: "double free or corruption" in the GPU process.
case " ${LDFLAGS} " in
*" -Wl,-Bsymbolic "*) ;;
*) LDFLAGS="${LDFLAGS:+${LDFLAGS} }-Wl,-Bsymbolic" ;;
esac
