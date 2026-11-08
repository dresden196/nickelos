#!/bin/bash
# Runs on the build host, as root, with a test VM booted with the disks of
# other-os-disks.sh (NICKEL_VMTEST_SSHPORT selects the VM): how cros-disks
# treats another system's disks, through its D-Bus interface as Chrome uses
# it (no sign-in needed; mounts then belong to chronos). Prints each
# partition of the two SATA disks, whether it is hidden, and how it mounts:
#   Windows disk  EFI, Microsoft reserved, Recovery: hidden
#                 Windows: read-only, and unchanged by it (sha256 of the
#                 partition); read-write with nickel-windows-writable
#   data disk     Data: read-write
#   other-os-disks-test.sh
set -euo pipefail
vm() {
	nickel-vmssh "$@"
}
# shellcheck disable=SC2016 # expanded in the VM
vm 'set -u
call() {
	dbus-send --system --print-reply --dest=org.chromium.CrosDisks \
		/org/chromium/CrosDisks "org.chromium.CrosDisks.$@"
}
prop() {
	call GetDeviceProperties "string:$1" | grep -A1 "\"$2\"" | tail -1 |
		sed -E "s/.*(boolean|string) //; s/\"//g"
}
mount_as() {
	local dev=$1; shift
	local opts="rw"
	[ $# -gt 0 ] && opts="rw,$1"
	call Mount "string:${dev}" string: "array:string:${opts}" >/dev/null
	for i in $(seq 1 20); do
		mnt=$(awk -v d="${dev}" "\$1 == d {print \$2, \$4}" /proc/mounts | head -1)
		[ -n "${mnt}" ] && break
		sleep 0.5
	done
	echo "${mnt:-not mounted}" | sed -E "s/^([^ ]*) (r[ow]).*/\2 \1/"
}
unmount() {
	local dev=$1 mp
	mp=$(awk -v d="${dev}" "\$1 == d {print \$2}" /proc/mounts | head -1)
	[ -n "${mp}" ] && call Unmount "string:${mp}" array:string: >/dev/null
	sleep 1
}
for dev in /dev/sda1 /dev/sda2 /dev/sda3 /dev/sda4 /dev/sdb1; do
	unmount "${dev}"
done
for dev in /dev/sda1 /dev/sda2 /dev/sda3 /dev/sda4 /dev/sdb1; do
	label=$(prop "${dev}" IdLabel)
	hidden=$(prop "${dev}" DevicePresentationHide)
	if [ "${hidden}" = true ]; then
		echo "${dev} ${label:-(no label)}: hidden"
		continue
	fi
	echo "${dev} ${label:-(no label)}: shown, mounts $(mount_as "${dev}")"
	unmount "${dev}"
done
# Read-only means unchanged: ntfs-3g once wrote the dirty flag anyway.
before=$(sha256sum /dev/sda3 | cut -c1-16)
mount_as /dev/sda3 >/dev/null
unmount /dev/sda3
after=$(sha256sum /dev/sda3 | cut -c1-16)
if [ "${before}" = "${after}" ]; then
	echo "/dev/sda3 after a read-only mount: unchanged"
else
	echo "/dev/sda3 after a read-only mount: CHANGED (${before} -> ${after})"
fi
echo "/dev/sda3 with nickel-windows-writable: $(mount_as /dev/sda3 nickel-windows-writable)"
unmount /dev/sda3
echo "cros-disks crashes: $(ls /var/spool/crash/ 2>/dev/null | grep -c cros_disks)"'
