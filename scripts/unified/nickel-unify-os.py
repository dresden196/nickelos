#!/usr/bin/env python3
# NickelOS's image step, the OS as Portage on the device sees it
# (nickel-unify, step 4; run by the Unified Gentoo root's Python): three
# files in the image's /etc/portage, from the image's records.
#   sets/nickel-os            every package of the OS, as >=its version:slot.
#       In your world (nickel-gentoo), so emerge --depclean keeps the OS. Not
#       its bare name: for a package from a repository the device hasn't
#       (Google's, NickelOS's), a world update picked Gentoo's package of the
#       name, older ones included (31 downgrades on 16836.3.48).
#   package.mask/nickel-os    Gentoo's packages of the names the OS keeps
#       Google's or NickelOS's (curl, xkeyboard-config, shflags, the Mesa
#       stand-in...), any version of that slot (FUSE 2 is Google's, FUSE 3
#       Gentoo's): untried with ChromeOS, and a world update
#       would have put them in your layer, the same version reinstalled
#       where Gentoo's declares other USE flags. package.unmask takes one.
#   profile/package.use/00-nickel-os  the USE flags each of Gentoo's
#       packages in the image was built with, for that version: emerge
#       --newuse rebuilds an OS package only when you change its flags. In
#       the user profile, which Portage reads before /etc/portage/package.use,
#       so any line of yours wins: in package.use an exact-version pin beat
#       a user's ">=" or plain line, as Portage applies the most specific
#       atom last (OBS's ffmpeg[x264] could never be had, 2026-10-21).
#   profile/package.mask/00-nickel-os-versions  versions of those packages
#       newer than the OS's: NickelOS updates bring them, so a world update
#       leaves the OS as it is and updates your packages; package.unmask
#       takes one into your layer (vision decision 1, 2026-10-21).
#   nickel-unify-os.py IMAGE_ROOT GENTOO_REPO
import os, sys
import portage
from portage.versions import catpkgsplit

ROOT, REPO = sys.argv[1], sys.argv[2]
# Portage's databases are made for $ROOT (nickel-unify sets it to the image).
if os.path.realpath(str(portage.root)) != os.path.realpath(ROOT):
    sys.exit('%s: run with ROOT=%s' % (sys.argv[0], ROOT))
vardb = portage.db[str(portage.root)]["vartree"].dbapi
etc = os.path.join(ROOT, 'etc/portage')

atoms, masks, uses, newer = [], set(), [], []
for cpv in sorted(vardb.cpv_all()):
    slot, repo, iuse, use = vardb.aux_get(cpv, ['SLOT', 'repository', 'IUSE', 'USE'])
    cat, pn = catpkgsplit(cpv)[:2]
    if cat == 'virtual':
        # A virtual has no files: by its slot alone, and never masked as one
        # the OS keeps (Google's virtual/libintl 0-r3 above Gentoo's 0-r2 kept
        # a 32-bit rebuild for Steam from happening). The virtuals held back
        # on purpose (udev's) are masked by name in nickel-held-back.
        atoms.append('%s/%s:%s' % (cat, pn, slot.split('/')[0] or '0'))
        continue
    atoms.append('>=%s:%s' % (cpv, slot.split('/')[0] or '0'))
    if repo != 'gentoo':
        if os.path.isdir(os.path.join(REPO, cat, pn)):
            masks.add('%s/%s:%s::gentoo' % (cat, pn, slot.split('/')[0] or '0'))
        continue
    # Newer versions than the OS's are NickelOS updates' to bring, not a
    # world update's (vision decision 1, 2026-10-21); package.unmask takes
    # one into your layer.
    newer.append('>%s:%s' % (cpv, slot.split('/')[0] or '0'))
    on = set(use.split())
    # Not the ABI flags: which ABIs you build for is yours (abi_x86_32 for
    # Steam and Wine), and a pinned -abi_x86_32 kept Portage from asking for
    # it ("no ebuilds built with USE flags to satisfy libpcre2[abi_x86_32]").
    # Gentoo's default without a pin is the same: 64-bit only.
    flags = sorted({f.lstrip('+-') for f in iuse.split()
                    if not f.lstrip('+-').startswith('abi_')})
    if flags:
        uses.append('=%s %s' % (cpv, ' '.join(f if f in on else '-' + f for f in flags)))

for path, head, lines in (
        ('sets/nickel-os', "the OS's packages, at least their versions in the image (nickel-unify);\n"
                           "# in your world (nickel-gentoo), so emerge --depclean never takes the OS apart", atoms),
        ('package.mask/nickel-os', "Gentoo's packages of the names the OS keeps Google's or NickelOS's\n"
                                   "# (nickel-unify): untried with ChromeOS. To take Gentoo's instead, unmask it\n"
                                   "# (/etc/portage/package.unmask); the OS's stays under your layer", sorted(masks)),
        ('profile/package.use/00-nickel-os', "the USE flags Gentoo's packages in the image were built\n"
                                             "# with (nickel-unify). Yours in /etc/portage/package.use win", uses),
        ('profile/package.mask/00-nickel-os-versions',
         "versions of the OS's Gentoo packages newer than the OS's (nickel-unify):\n"
         "# NickelOS updates bring them, tested, so emerge -uDN @world leaves the OS\n"
         "# as it is and updates your packages. To take a newer one yourself, put\n"
         "# its name in /etc/portage/package.unmask: it goes into your layer, the\n"
         "# OS's stays underneath, and an OS update that catches up hands it back", sorted(newer))):
    p = os.path.join(etc, path)
    os.makedirs(os.path.dirname(p), exist_ok=True)
    with open(p, 'w') as f:
        f.write('# NickelOS: %s.\n%s\n' % (head, '\n'.join(lines)))
print("the OS for Portage: %d packages in @nickel-os, %d of Gentoo's held back as the OS keeps its own, "
      "%d of Gentoo's with their flags, %d held at the OS's versions" % (len(atoms), len(masks), len(uses), len(newer)))
