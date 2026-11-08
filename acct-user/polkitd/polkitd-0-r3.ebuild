# Copyright 2019-2026 Gentoo Authors
# Distributed under the terms of the GNU General Public License v2

EAPI=7

# NickelOS: ported from Gentoo polkitd-0-r3 (EAPI 8 -> 7): UID 102 -> 29900, home /var/lib/polkit-1 -> /dev/null; the values that are actually used come from profiles/base/accounts/user/polkitd.

inherit acct-user

ACCT_USER_ID=29900
ACCT_USER_GROUPS=( polkitd )

acct-user_add_deps
