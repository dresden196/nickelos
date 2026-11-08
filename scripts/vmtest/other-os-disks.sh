#!/bin/bash
# Runs on the build host, as root: makes the test VM's second and third disk
# for testing how NickelOS treats other systems' disks (cros-disks patch
# 0002), in NICKEL_VMTEST_DIR (default /srv/nickel/vmtest):
#   target0.qcow2  a Windows system disk, laid out as Windows lays it out
#                  (GPT): EFI system (FAT32), Microsoft reserved, the
#                  Windows volume (NTFS "Windows", with Windows/System32),
#                  Windows recovery (NTFS "Recovery")
#   target1.qcow2  a data disk: one NTFS partition "Data" with a file
# Then boot them as the VM's internal SATA disks:
#   NICKEL_VMTEST_TARGETS="1 1" NICKEL_VMTEST_TARGETS_KEEP=1 nickel-vmtest IMAGE
# Expected: only "Windows" and "Data" show; Windows mounts read-only, Data
# read-write (scripts/vmtest/other-os-disks-test.sh checks it).
set -euo pipefail
D=${NICKEL_VMTEST_DIR:-/srv/nickel/vmtest}
tmp=$(mktemp -d /var/tmp/other-os-disks.XXXXXX)
loop=
cleanup() {
	mountpoint -q "${tmp}/m" && umount "${tmp}/m"
	[ -n "${loop}" ] && losetup -d "${loop}"
	rm -rf "${tmp}"
}
trap cleanup EXIT
mkdir "${tmp}/m"

# The Windows disk: sgdisk type codes ef00 EFI system, 0c01 Microsoft
# reserved, 0700 Microsoft basic data, 2700 Windows recovery.
truncate -s 1G "${tmp}/windows.raw"
sgdisk -Z -n 1:0:+100M -t 1:ef00 -c 1:"EFI system partition" \
	-n 2:0:+16M -t 2:0c01 -c 2:"Microsoft reserved partition" \
	-n 3:0:-200M -t 3:0700 -c 3:"Basic data partition" \
	-n 4:0:0 -t 4:2700 "${tmp}/windows.raw" >/dev/null
loop=$(losetup -P -f --show "${tmp}/windows.raw")
mkfs.vfat -F 32 -n SYSTEM "${loop}p1" >/dev/null
mkntfs -Q -L Windows "${loop}p3" >/dev/null 2>&1
mkntfs -Q -L Recovery "${loop}p4" >/dev/null 2>&1
ntfs-3g "${loop}p3" "${tmp}/m"
mkdir -p "${tmp}/m/Windows/System32" "${tmp}/m/Users/Public"
echo "Windows' own volume (NickelOS test disk)" > "${tmp}/m/Users/Public/readme.txt"
umount "${tmp}/m"
losetup -d "${loop}"
loop=
qemu-img convert -O qcow2 "${tmp}/windows.raw" "${D}/target0.qcow2"

# The data disk.
truncate -s 1G "${tmp}/data.raw"
sgdisk -Z -n 1:0:0 -t 1:0700 -c 1:"Basic data partition" "${tmp}/data.raw" >/dev/null
loop=$(losetup -P -f --show "${tmp}/data.raw")
mkntfs -Q -L Data "${loop}p1" >/dev/null 2>&1
ntfs-3g "${loop}p1" "${tmp}/m"
echo "A data disk (NickelOS test disk)" > "${tmp}/m/readme.txt"
umount "${tmp}/m"
losetup -d "${loop}"
loop=
qemu-img convert -O qcow2 "${tmp}/data.raw" "${D}/target1.qcow2"
echo "${D}/target0.qcow2 (Windows), ${D}/target1.qcow2 (Data)"
