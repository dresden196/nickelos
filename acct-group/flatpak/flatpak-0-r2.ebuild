# Copyright 2019-2026 Gentoo Authors
# Distributed under the terms of the GNU General Public License v2

EAPI=7

# NickelOS: ported from Gentoo flatpak-0-r2 (EAPI 8 -> 7): GID 313 -> 29901; the GID that is actually used comes from profiles/base/accounts/group/flatpak.

inherit acct-group

ACCT_GROUP_ID=29901
