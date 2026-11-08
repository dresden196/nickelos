# Copyright 2021-2026 Gentoo Authors
# Distributed under the terms of the GNU General Public License v2

EAPI=7

# NickelOS: ported from Gentoo geoclue-0-r2 (EAPI 8 -> 7): UID 351 -> 29904, home /var/lib/geoclue -> /dev/null (GeoClue keeps nothing); the values that are actually used come from profiles/base/accounts/user/geoclue.

inherit acct-user

ACCT_USER_ID=29904
ACCT_USER_GROUPS=( geoclue )

acct-user_add_deps
