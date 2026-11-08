title: Graphics
order: 50
summary: How Linux apps draw on NickelOS. Exo, ChromeOS's Wayland server, and the protocols it offers, X11 through sommelier and Xwayland, Mesa for Intel and AMD, NVIDIA's own driver, video decoding, and how to check which driver is in use.
---
# Graphics

How a Linux app's pixels get to your screen on NickelOS, which drivers draw them, and how to check what's in use.

## How a window reaches the screen

ChromeOS's desktop is **Ash**, the part of Chrome that draws the shelf, the windows and everything around them. Ash has a Wayland server built in, called **Exo**. On a Chromebook, Exo serves Android apps and Crostini's Linux VM. On NickelOS, Linux apps connect to it directly, on the device, through `/run/chrome/wayland-0` (linked as `wayland-0` in your runtime directory).

Since Exo is part of Ash, a Linux window is a real ChromeOS window. It gets the shelf, overview, snapping, desks, tablet mode, display scaling and the input methods. NickelOS doesn't run a second compositor next to ChromeOS's; it adds what desktop Linux apps need to the one ChromeOS already has.

Frames go from the app to the screen the way they do on any modern Wayland desktop. The app draws into a GPU buffer, hands it over with `linux-dmabuf`, and Exo composites it. Nothing gets copied on the way.

## The Wayland protocols apps see

Exo also speaks some private ChromeOS protocols, written for ChromeOS's own trusted clients. A Linux app has no business with those, and some of them could crash the desktop. So on NickelOS, Linux apps only see standard protocols:

| Protocol | What it's for |
|---|---|
| `wl_compositor`, `wl_subcompositor`, `wl_shm`, `wl_output`, `wl_seat` | The core: surfaces, shared-memory buffers, displays, input |
| `wl_data_device_manager` | Copy and paste, drag and drop |
| `wl_shell`, `xdg_wm_base` | Windows (xdg-shell) |
| `zxdg_decoration_manager_v1` | Who draws the title bar |
| `zxdg_output_manager_v1` | Display names and logical sizes |
| `zwp_linux_dmabuf_v1` | GPU buffers |
| `wp_presentation` | Frame timing |
| `wp_viewporter`, `wp_fractional_scale_manager_v1` | Sharp scaling at any display size |
| `wp_single_pixel_buffer_manager_v1`, `wp_content_type_manager_v1` | Cheap solid surfaces; hints for video and games |
| `wp_cursor_shape_manager_v1` | ChromeOS's own cursors instead of each app's |
| `xdg_activation_v1` | Handing focus between windows and apps |
| `zwp_pointer_constraints_v1`, `zwp_relative_pointer_manager_v1` | Pointer lock and raw mouse motion, for games |
| `zwp_pointer_gestures_v1` | Touchpad pinch and swipe |
| `zwp_keyboard_shortcuts_inhibit_manager_v1` | Remote desktops and VMs that want every key |
| `zwp_idle_inhibit_manager_v1` | Keeping the screen on while a video plays |
| `zwp_input_timestamps_manager_v1` | Exact input timing |
| `zwp_text_input_manager_v1`, `zwp_text_input_manager_v3`, `zcr_text_input_extension_v1` | Input methods (Chinese, Japanese, Korean and the rest) |
| `zwp_primary_selection_device_manager_v1` | Middle-click paste |

`WAYLAND_DEBUG=1 some-app` prints what an app asks for, if you're curious or chasing a bug.

We also hardened Exo against apps that misbehave. It survives bad drags, broken pipes, shared memory that shrinks under it, windows destroyed in odd orders and more. A Linux app can crash itself, but it shouldn't be able to take the desktop down with it.

## X11 apps

X11 apps (Wine and Proton games, Steam's client, older toolkits, Java) run on **Xwayland**, display `:0`. Xwayland is run by **sommelier**, the same X11-to-Wayland bridge Crostini uses. sommelier is the window manager for X11 windows and turns each one into an Exo window. It's a trusted client, so it connects on its own socket (`/run/chrome/wayland-sommelier`), where it gets Exo's own protocols for placing and decorating windows.

An X11 window gets the app id `org.chromium.guest_os.nickel.wmclass.<WM_CLASS>`. That's how ChromeOS matches it to the app in the launcher, through the desktop file's `StartupWMClass`. If an X11 app's windows don't group under its shelf icon, its desktop file is probably missing the right `StartupWMClass`.

X11 runs on the GPU too:

- Xwayland draws with **glamor** on the render node of the GPU that drives your display.
- Apps share GPU buffers with Xwayland (DRI3 and Present), and Xwayland shares them with Exo (`linux-dmabuf` with modifiers). GLX, EGL and Vulkan on X11 windows all work this way, and so do Wine and Proton.
- On NVIDIA, which has no implicit sync, sommelier gives Xwayland **explicit sync** (`linux-drm-syncobj`). It holds each frame until the GPU has finished drawing it, then passes it on, so Exo only ever shows finished frames and never waits on an app.
- Cursors: X11 apps get ChromeOS's own cursors too. sommelier looks up the name of the cursor the app asked for and asks Exo for the same shape.

## Intel and AMD

Intel (`i915` or `xe`) and AMD (`amdgpu`, or `radeon` for older cards) use the Linux kernel's drivers and **Mesa**. There's one Mesa on NickelOS, Gentoo's current one. Chrome draws with it, and so do the system's own apps, what you install with `emerge`, AppImages and Steam from Gentoo. Flatpak apps use the Mesa in their runtime, the `org.freedesktop.Platform.GL` extension Flatpak installs with them.

Video in Chrome is decoded on the GPU through VA-API, as on ChromeOS.

## NVIDIA

NVIDIA cards use **NVIDIA's own driver**, unmodified:

- For GPUs on NVIDIA's list for its open kernel modules (Turing and newer: GeForce GTX 16 and RTX 20 series and later), NickelOS loads **nvidia-open**, NVIDIA's open-source kernel modules with NVIDIA's userspace. Older NVIDIA GPUs get **nouveau**. The choice is made per GPU, at boot, from NVIDIA's own list (`/usr/share/nickel/nvidia-open-gpus`).
- The modules are built against NickelOS's kernel and signed with its key, because NickelOS only loads signed modules. Every kernel update rebuilds them.
- Chrome draws through NVIDIA's own buffer allocator (GBM), via an `nvidia-drm` backend we added to minigbm, ChromeOS's graphics buffer library. Chrome's GPU sandbox is adjusted so NVIDIA's driver can work inside it.
- Hardware overlay planes are off on NVIDIA. With them on, page flips timed out once a game's window was up.
- Video decode: NVIDIA's NVDEC, through VA-API (`nvidia-vaapi-driver`), so Chrome's hardware video decoding works on NVIDIA too.
- Linux apps: NVIDIA's EGL is first in the vendor list, GLX goes through glvnd to NVIDIA's, and Vulkan uses NVIDIA's ICD. Xwayland's glamor runs on NVIDIA's EGL and GBM, with explicit sync (above).
- Flatpak apps: Flatpak sees your driver version and installs the matching `org.freedesktop.Platform.GL.nvidia-...` extension (and its 32-bit twin, for Steam) by itself.
- Gentoo: NVIDIA's libraries are in `/usr/lib64`, where Gentoo's programs find them, and `nickel-gentoo gpu` downloads the 32-bit ones for your driver (see [Gentoo and Portage](/help/gentoo/#nvidia)).

`nvidia-smi` is there, as on any distribution with NVIDIA's driver.

## Checking what's in use

```
lsmod | grep -E 'i915|xe|amdgpu|radeon|nvidia|nouveau'   # which kernel driver
cat /sys/module/nvidia/version                            # NVIDIA's driver version
nvidia-smi                                                # NVIDIA: GPU, memory, processes
ls -l /dev/dri                                            # card and render nodes
cat $XDG_RUNTIME_DIR/x11-display                          # Xwayland's display, once it's up
```

In the browser, `chrome://gpu` shows what Chrome itself is drawing with.

## Displays

Display settings are ChromeOS's, in **Settings > Device > Displays**: resolution, display size, refresh rate, orientation, arrangement, mirroring and Night Light. Wayland apps get fractional scaling from Exo, so they stay sharp at any display size.
