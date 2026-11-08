#!/bin/bash
# Run inside a booted nickel test image: check each overlay-nickel change.
ok()   { printf '  PASS  %s\n' "$*"; }
bad()  { printf '  FAIL  %s\n' "$*"; FAILS=$((FAILS+1)); }
check(){ if eval "$2" >/dev/null 2>&1; then ok "$1"; else bad "$1"; fi; }
FAILS=0

echo "== identity"
# lsb-release has unquoted values with spaces, so read fields, don't source it.
lsb() { sed -n "s/^$1=//p" /etc/lsb-release; }
CHROMEOS_RELEASE_BOARD=$(lsb CHROMEOS_RELEASE_BOARD)
CHROMEOS_RELEASE_APPID=$(lsb CHROMEOS_RELEASE_APPID)
DEVICETYPE=$(lsb DEVICETYPE)
echo "  board=$CHROMEOS_RELEASE_BOARD appid=$CHROMEOS_RELEASE_APPID devicetype=$DEVICETYPE"
check "lsb-release board is nickel"      '[ "$CHROMEOS_RELEASE_BOARD" = nickel ]'
check "NickelOS app id"                  '[ "$CHROMEOS_RELEASE_APPID" = "{84A28531-7D33-44D5-AAA1-6CFA2BCF8652}" ]'
check "DEVICETYPE=OTHER"                 '[ "$DEVICETYPE" = OTHER ]'
grep -E '^(NAME|ID|ID_LIKE)=' /etc/os-release | sed 's/^/  /'
check "os-release NAME=NickelOS"         'grep -qx "NAME=NickelOS" /etc/os-release'

echo "== model config"
echo "  cros_config / name = $(cros_config / name)"
echo "  efi bootvar = $(cros_config /efi bootvar-name-override)"
check "cros_config name is nickel"       '[ "$(cros_config / name)" = nickel ]'
check "power quirks present"             'cros_config /power suspend-to-idle-models | grep -q 20V9'
check "cros_configfs running"            'status cros_configfs | grep -q running'

echo "== Google services removed"
check "flex_hwis not installed"          '! ls /usr/bin/flex_hwis* /usr/sbin/flex_hwis* /etc/init/flex_hwis* 2>/dev/null | grep -q .'

echo "== kernel"
check "virtio_blk built in (not a module)" 'grep -q "/virtio_blk.ko" /lib/modules/$(uname -r)/modules.builtin'
echo "  root device: $(rootdev -s 2>/dev/null)"

echo "== firmware"
n=$(find /lib/firmware -type f -o -type l | wc -l)
echo "  /lib/firmware entries: $n ($(du -sh /lib/firmware | cut -f1))"
check "firmware count >= 2000"           '[ "$n" -ge 2000 ]'
for f in amdgpu/navi10_gpu_info.bin radeon/BONAIRE_mc.bin nvidia/tu102/gr/sw_ctx.bin \
         iwlwifi-so-a0-gf-a0-89.ucode regulatory.db intel/sof/sof-tgl.ri; do
  check "has $f" "[ -e /lib/firmware/$f ]"
done

echo "== fonts"
echo "  fc-match 'Google Sans'      -> $(fc-match -f '%{family[0]} (%{file})' 'Google Sans')"
echo "  fc-match 'Google Sans Text' -> $(fc-match -f '%{family[0]}' 'Google Sans Text')"
check "Google Sans served by Google Sans Flex" '[ "$(fc-match -f "%{family[0]}" "Google Sans")" = "Google Sans Flex" ]'
check "Google Sans Code installed"       'fc-list | grep -q "Google Sans Code"'
# Cache files are named by a hash of the directory path; ask fontconfig which
# cache it actually loaded for our directory.
check "font dir served from shipped cache" 'FC_DEBUG=16 fc-list 2>&1 | grep -q "dir: /usr/share/fonts/google-sans-flex)"'

echo "== disk layout"
for p in 2 12; do echo "  partition $p: $(cgpt show -i $p -s "$(rootdev -s -d)") sectors"; done

echo "== session"
check "ui running"                       'status ui | grep -q running'
check "Exo socket present"               '[ -S /run/chrome/wayland-0 ]'
echo "FAILS=$FAILS"
