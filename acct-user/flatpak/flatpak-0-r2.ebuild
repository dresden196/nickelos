# Copyright 2019-2026 Gentoo Authors
# Distributed under the terms of the GNU General Public License v2

EAPI=7

# NickelOS: ported from Gentoo flatpak-0-r2 (EAPI 8 -> 7): UID 313 -> 29901; the values that are actually used come from profiles/base/accounts/user/flatpak.

inherit acct-user

ACCT_USER_ID=29901
ACCT_USER_GROUPS=( flatpak )

acct-user_add_deps
