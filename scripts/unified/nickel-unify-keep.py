#!/usr/bin/env python3
# NickelOS's image step, the libraries kept (nickel-unify, step 4,
# after Gentoo's packages are in): a library one of ChromeOS's files needs
# and the image no longer has anywhere comes back from the board's root into
# /usr/lib64/cros, beside Gentoo's, as libcares does. That happens when
# Gentoo's newer package of a library has another library name and Portage's
# preserve-libs didn't keep the old one: Google's cups-filters' pdftoraster
# needs Google's libpoppler-cpp.so.1, Gentoo's poppler has .so.3 (and
# Google's C++ programs need Google's libc++ build of it anyway).
#   - the library and what it needs in turn, until nothing is missing
#   - the files that need them get /usr/lib64/cros in their RUNPATH (added
#     to nickel-unify's list, RPATH_LIST), and their records say so
#   - the libraries go into ChromeOS's additions' record
# Gentoo's own files missing a library are reported, not changed: that's a
# package to fix.
#   nickel-unify-keep.py IMAGE_ROOT BOARD_ROOT RPATH_LIST
import hashlib, importlib.util, os, shutil, sys

ROOT, BOARD, LIST = os.path.realpath(sys.argv[1]), sys.argv[2], sys.argv[3]
HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location('records', os.path.join(HERE, 'nickel-unify-records.py'))
rec = importlib.util.module_from_spec(spec)
sys.argv = [sys.argv[0], ROOT, '', '', '', BOARD]
spec.loader.exec_module(rec)
PRIV = rec.PRIV
vdb = ROOT + '/var/db/pkg'

# Who owns each file, and whether that's a package of Gentoo's.
owner, gentoo = {}, set()
for cat in (os.listdir(vdb) if os.path.isdir(vdb) else []):
    for pv in os.listdir(os.path.join(vdb, cat)):
        d = os.path.join(vdb, cat, pv)
        if not os.path.isfile(d + '/CONTENTS'):
            continue
        try:
            if open(d + '/repository').read().strip() == 'gentoo':
                gentoo.add(d)
        except OSError:
            pass
        for l in rec.entries(d):
            if not l.startswith('dir '):
                owner[rec.canon(rec.path_of(l))] = d

# The records step listed the files that load a library from PRIV; where a
# package of Gentoo's has since put its own file in such a place (step 3),
# that file takes Gentoo's libraries (Mesa's GBM, not minigbm's).
users = [l.strip() for l in open(LIST) if l.strip()]
replaced = [u for u in users if owner.get(rec.canon(u)) in gentoo]
users = [u for u in users if u not in replaced]
system = rec.loader_dirs(ROOT)


def board_lib(so):
    """The board's file of the library of that name, or None."""
    for top in ('/usr/lib64', '/lib64'):
        p = BOARD + top + '/' + so
        if os.path.lexists(p):
            real = os.path.realpath(p)
            if real.startswith(BOARD + '/') and os.path.isfile(real):
                return real
    return None


kept, adds = [], []
while True:
    names = rec.lib_names(ROOT)
    need, unfixed, gentoo_missing = {}, {}, {}
    for here, e in rec.elf_files(ROOT):
        rel = os.path.dirname(here)
        dirs = [x.replace('$ORIGIN', rel).replace('${ORIGIN}', rel) for x in e[1].split(':') if x]
        if here in users:
            dirs.append(PRIV)
        dirs += system
        for so in e[0]:
            if '/' in so or so in names or any(os.path.exists(ROOT + os.path.join(x, so)) for x in dirs):
                continue
            if owner.get(rec.canon(here)) in gentoo:
                gentoo_missing.setdefault(so, []).append(here)
            else:
                need.setdefault(so, []).append(here)
    new = False
    for so, who in sorted(need.items()):
        real = board_lib(so)
        if not real:
            unfixed[so] = who
            continue
        base = os.path.basename(real)
        dst = ROOT + PRIV + '/' + base
        if not os.path.exists(dst):
            shutil.copy2(real, dst)
            adds.append('obj %s/%s %s %d' % (PRIV, base, hashlib.md5(open(dst, 'rb').read()).hexdigest(),
                                             int(os.stat(dst).st_mtime)))
        if base != so and not os.path.lexists(ROOT + PRIV + '/' + so):
            os.symlink(base, ROOT + PRIV + '/' + so)
            adds.append('sym %s/%s -> %s %d' % (PRIV, so, base, int(os.lstat(ROOT + PRIV + '/' + so).st_mtime)))
        kept.append('%s (for %s)' % (so, ' '.join(sorted(who)[:3])))
        for h in who + [PRIV + '/' + base]:
            if h not in users:
                users.append(h)
        new = True
    if not new:
        break

# The records: the kept libraries in ChromeOS's additions, with what they
# need; every new user's RUNPATH as it will be.
add = os.path.join(vdb, 'nickel-base', 'chromeos-additions-1')
if adds and os.path.isdir(add):
    with open(add + '/CONTENTS', 'a', errors='surrogateescape') as f:
        f.write('\n'.join(adds) + '\n')
    lines = []
    for l in adds:
        if l.startswith('obj '):
            p = rec.path_of(l)
            e = rec.elf_dynamic(ROOT + p)
            if e:
                lines.append('X86_64;%s;%s;%s;%s;x86_64' % (p, e[2], PRIV, ','.join(e[0])))
    with open(add + '/NEEDED.ELF.2', 'a') as f:
        f.write('\n'.join(lines) + '\n')
old = set(l.strip() for l in open(LIST) if l.strip())
for h in users:
    if h in old or h.startswith(PRIV + '/'):
        continue
    d = owner.get(rec.canon(h))
    f = d and os.path.join(d, 'NEEDED.ELF.2')
    if not f or not os.path.exists(f):
        continue
    out = []
    for l in open(f, errors='surrogateescape').read().splitlines():
        x = l.split(';')
        if len(x) >= 6 and rec.canon(x[1]) == rec.canon(h) and PRIV not in x[3].split(':'):
            x[3] = (x[3] + ':' + PRIV) if x[3] else PRIV
        out.append(';'.join(x))
    with open(f, 'w', errors='surrogateescape') as fh:
        fh.write('\n'.join(out) + '\n')
with open(LIST, 'w') as f:
    f.write(''.join(h + '\n' for h in users))

if replaced:
    print("files now Gentoo's, no RUNPATH %s: %s" % (PRIV, ' '.join(replaced[:5])))
print('libraries kept from the board in %s: %d%s' % (PRIV, len(kept), ''.join('\n  ' + k for k in kept)))
for so, who in sorted(unfixed.items()):
    print("  not in the image or the board: %s (for %s)" % (so, ' '.join(sorted(who)[:3])))
for so, who in sorted(gentoo_missing.items()):
    print("  Gentoo's files missing %s: %s" % (so, ' '.join(sorted(who)[:3])))
