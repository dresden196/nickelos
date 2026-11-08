#!/bin/bash
# Runs on nickel-build. The installer, through its UI (Chrome patch 0007):
#   1. the test image boots as a USB stick next to two blank SATA disks
#      (64 GB and 32 GB); setup offers "Install" or "Try it first"
#   2. the installer lists both disks (the USB stick is not offered), the
#      larger one recommended; the 32 GB one is picked instead
#   3. the confirmation names the picked disk; installing writes it
#   4. the VM boots from the target disks alone, from the picked disk
# Uses the second test VM ($NICKEL_VMTEST_DIR /srv/nickel/vmtest-b, ssh port
# 9224), so it can run next to the other tests. The VM renders in software
# and OOBE loads its screens lazily, so screens can take a minute to appear;
# the waits below follow Chrome's log, the screenshots may lag.
set -u
export NICKEL_VMTEST_DIR=/srv/nickel/vmtest-b NICKEL_VMTEST_SSHPORT=9224
D=$NICKEL_VMTEST_DIR
IMG=${1:-$(readlink -f /srv/nickel/src/src/build/images/nickel/latest)/chromiumos_test_image.bin}
vm() { nickel-vmssh "$@" </dev/null; }
wait_ssh() { sleep 20; for i in $(seq 1 120); do timeout 15 nickel-vmssh true </dev/null 2>/dev/null && return 0; sleep 4; done; echo "SSH NEVER CAME UP"; exit 1; }
wait_log() { for i in $(seq 1 "${3:-60}"); do vm "grep -q '$2' $1" && return 0; sleep 3; done; echo "TIMEOUT waiting for: $2"; return 1; }
shot() { nickel-vmctl click 1270 300; sleep 6; nickel-vmctl shot "$D/flow/installer-$1.png"; }
start_vm() {
  systemctl stop nickel-vmtest-b 2>/dev/null; systemctl reset-failed nickel-vmtest-b 2>/dev/null
  systemd-run --unit nickel-vmtest-b --collect -E NICKEL_VMTEST_DIR="$D" \
    -E NICKEL_VMTEST_SSHPORT=9224 -E "NICKEL_VMTEST_TARGETS=64 32" "$@" >/dev/null
}

mkdir -p "$D/flow"
echo "== 1. boot the installer from USB: $IMG"
start_vm -E NICKEL_VMTEST_DISK=usb /usr/local/bin/nickel-vmtest "$IMG"
wait_ssh
wait_log /var/log/chrome/chrome "AdvanceToScreen" 60; sleep 20
# By model: the kernel names the two SATA disks sda/sdb in either order.
vm 'lsblk -dno MODEL,SIZE,RM,NAME | grep -E "sd[a-z]\$" | sort | sed -E "s/ +sd[a-z]\$//"'
nickel-vmctl click 214 649
wait_log /var/log/chrome/chrome "SetCurrentScreen: os-trial" && sleep 60
shot 1-trial
nickel-vmctl click 1077 647
wait_log /var/log/chrome/chrome "SetCurrentScreen: os-install" && sleep 90
shot 2-intro

echo "== 2. choose the disk"
nickel-vmctl click 1043 647
wait_log /var/log/messages "best disk" && sleep 90
shot 3-choose
nickel-vmctl click 606 227; sleep 20
nickel-vmctl click 1077 647; sleep 75
shot 4-confirm

echo "== 3. install"
nickel-vmctl click 844 500
# Up to 20 min: the VM installs from an emulated USB stick, and since one
# Mesa (2026-10-19) the image's root is 4800 MiB in 5 GiB partitions.
wait_log /var/log/messages "install succeeded\|install failed" 400
vm 'grep os_install_service /var/log/messages | grep -E "Installation to|install succeeded|install failed" | sed "s/.*>>> //; s/.*\] //"'
sleep 10
shot 5-done

echo "== 4. boot from the target disks"
sleep 20
start_vm -E NICKEL_VMTEST_TARGETS_KEEP=1 /usr/local/bin/nickel-vmtest none
wait_ssh
vm 'echo "root: $(rootdev -s)"; grep CHROMEOS_RELEASE_BOARD /etc/lsb-release'
