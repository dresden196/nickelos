# Distributed under the terms of the GNU General Public License v2

EAPI=7

inherit toolchain-funcs

# NickelOS: Android apps through Waydroid, installed by default. This package
# is what NickelOS puts around app-containers/waydroid: the container
# manager's upstart job, with the cgroup v2 mount Android's network filters
# need to stay off the host (files/init/waydroid-container.conf,
# files/nickel-waydroid-prepare). The rest
# lives where it belongs: Waydroid's network and time zone patches in
# app-containers/waydroid, shill leaving Waydroid's bridge alone
# (patches/chromeos-base/shill), overlayfs for root and binderfs in the
# kernel (patches/sys-kernel, nickel-kernel.bashrc). Android's images
# (~2.3 GB) download at first use: `sudo waydroid init`, then
# `sudo start waydroid-container` (a Settings page will do both). On NVIDIA
# it sets Waydroid up for waydroid-nvidia by itself and runs its render
# server in the session (files/init/nickel-waydroid-venus.conf). ARM app
# support is the user's to turn on (files/nickel-waydroid-arm): NickelOS
# ships no translator, it downloads Intel's or Google's from where
# waydroid_script takes them, after the user agrees. nickel-waydroidd (root,
# org.nickelos.Android) does what Settings > Apps > Android apps and the APK
# handler (nickel-waydroid-apk, which opens .apk files) need root for;
# nickel-waydroid-open is behind Waydroid's launcher entry, and Settings
# opens Android's own Settings app through org.nickelos.AndroidSettings
# (Chrome 0072). libnickelhwcfix.so (files/hwcfix) fixes Waydroid's
# hwcomposer: two hangs (a roundtrip that can wait forever, and a focus
# change made with the window lock held), buffers made again every frame
# (choppy scrolling, a lagging pointer), the mouse pointer's GPU read-back
# (noise on NVIDIA) and a display hotplug on every focus change (Android
# windows jumping back in front). It runs inside Android, so it's
# built for Android (bionic) and put in Waydroid's overlay by
# nickel-waydroid-hwcfix.

DESCRIPTION="NickelOS: Android apps through Waydroid"
HOMEPAGE="https://nickelos.dev"

LICENSE="GPL-2"
SLOT="0"
KEYWORDS="*"

S="${WORKDIR}"

RDEPEND="
	app-containers/waydroid
	app-containers/waydroid-nvidia
"

src_compile() {
	# For Android's x86_64 (bionic): the SDK's own clang, told the target,
	# against link-time stand-ins for bionic's libc and libdl, without the
	# host's hardening defaults (they'd need the host's libc). Shipped
	# gzipped, as the Android file it is; unpacked into Waydroid's overlay.
	local cc t=(--target=x86_64-linux-android33 -fPIC -O2 -nostdlib
		-fno-stack-protector -fuse-ld=lld)
	cc=$(tc-getBUILD_CC)
	local d="${FILESDIR}"/hwcfix
	${cc} "${t[@]}" -shared -Wl,-soname,libdl.so -o libdl.so \
		"${d}"/libdl-stub.c || die
	${cc} "${t[@]}" -shared -Wl,-soname,libc.so -o libc.so \
		"${d}"/libc-stub.c || die
	${cc} "${t[@]}" -shared -Wl,-soname,libnickelhwcfix.so \
		-Wl,-z,max-page-size=4096 -Wl,--hash-style=both \
		-o libnickelhwcfix.so "${d}"/nickel-hwc-roundtrip.c \
		"${d}"/nickel-hwc-buffers.c "${d}"/nickel-hwc-focus.c \
		"${d}"/nickel-hwc-pointer.c "${d}"/nickel-hwc-configure.c \
		"${d}"/nickel-hwc-surfaces.c "${d}"/nickel-hwc-opaque.c \
		"${d}"/nickel-hwc-client.c \
		-L. -ldl -lc || die
	gzip -9n libnickelhwcfix.so || die
}

src_install() {
	exeinto /usr/libexec/nickel-waydroid
	doexe "${FILESDIR}"/nickel-waydroid-prepare "${FILESDIR}"/nickel-waydroid-nvidia \
		"${FILESDIR}"/nickel-waydroid-arm "${FILESDIR}"/nickel-waydroidd \
		"${FILESDIR}"/nickel-waydroid-apk "${FILESDIR}"/nickel-waydroid-open \
		"${FILESDIR}"/nickel-waydroid-session-check \
		"${FILESDIR}"/nickel-waydroid-hwcfix \
		"${FILESDIR}"/nickel-waydroid-venus-socket
	insinto /usr/share/nickel-waydroid
	doins libnickelhwcfix.so.gz
	insinto /etc/init
	doins "${FILESDIR}"/init/waydroid-container.conf
	doins "${FILESDIR}"/init/nickel-waydroid-venus.conf
	doins "${FILESDIR}"/init/nickel-waydroidd.conf
	doins "${FILESDIR}"/init/nickel-waydroid-session.conf
	insinto /etc/dbus-1/system.d
	doins "${FILESDIR}"/dbus/org.nickelos.Android.conf
	insinto /usr/share/applications
	doins "${FILESDIR}"/applications/org.nickelos.AndroidInstaller.desktop
	doins "${FILESDIR}"/applications/org.nickelos.AndroidSettings.desktop
}
