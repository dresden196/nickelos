# NickelOS: Vulkan's loader with its window-system code (package.use: X
# wayland). CMake takes X11's and xcb's include directory from pkg-config
# without the sysroot (/usr/include); it leaves that out of C compiles, where
# it is a default directory, but not out of the loader's one assembler file
# (unknown_ext_chain_gas_x86.S), which ChromiumOS's compiler then refuses as
# a host path (-Wpoison-system-directories). Telling CMake the directory is
# a default one for assembler too drops it there as well.
MYCMAKEARGS="${MYCMAKEARGS} -DCMAKE_ASM_IMPLICIT_INCLUDE_DIRECTORIES=/usr/include"
