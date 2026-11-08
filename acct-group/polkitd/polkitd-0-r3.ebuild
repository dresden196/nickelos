# Copyright 2019-2026 Gentoo Authors
# Distributed under the terms of the GNU General Public License v2

EAPI=7

# NickelOS: ported from Gentoo polkitd-0-r3 (EAPI 8 -> 7): GID 102 -> 29900; the GID that is actually used comes from profiles/base/accounts/group/polkitd.

inherit acct-group

ACCT_GROUP_ID=29900
