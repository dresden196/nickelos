#!/usr/bin/env python3
# NickelOS's image step, the records (docs/unified.md, "Step 3 in the
# real build"): run by nickel-unify on the image's root while build_image
# makes it, before Gentoo's packages go in. build_image has just recorded the
# image's packages in /var/db/pkg; Portage on the device needs those records
# to know every file's owner, ChromeOS's included.
#   1. each record keeps what Portage needs of an installed package (its
#      files, version, slot, dependencies, the libraries it provides and
#      needs), not its build environment: Google's install and removal steps
#      don't run on the device
#   2. Google's blockers come out (rules of its build tree, not facts about
#      files); virtual packages go to Gentoo's version; Google's Mesa and
#      nickel-mesa go for Gentoo's (installed in step 3)
#   3. ChromeOS's own files in packages Gentoo has too (the swap set,
#      gentoo/swap.txt, and any other, which Portage may replace as a
#      dependency) get a record of their own, nickel-base/chromeos-additions,
#      with every directory of the image, so replacing a package doesn't
#      delete them
#   4. the packages that stay the image's get back the development files
#      build_image leaves out (headers, pkg-config files), from the board's
#      root: Gentoo's packages build against them on the device
#   5. Google's C++ libraries and minigbm move to /usr/lib64/cros, out of
#      Portage's records, so Gentoo's packages of the same names install
#      beside them
#   nickel-unify-records.py IMAGE_ROOT GENTOO_VDB SWAP_TXT OUT BOARD_ROOT
# OUT gets the swap set as this image has it, for the install; OUT.newer the
# image's packages newer than Gentoo's, which stay; OUT.rpath the files that
# get /usr/lib64/cros in their RUNPATH (nickel-unify patches them, and their
# records already say so).
import glob, hashlib, os, re, shutil, struct, subprocess, sys, time

ROOT, GVDB, SWAP, OUT, BOARD = sys.argv[1:6]


def read_list(p):
    return [l.split('#')[0].strip() for l in open(p) if l.split('#')[0].strip()]


def cp_of(cpv):
    m = re.match(r'(.+?)-\d', cpv)
    return m.group(1) if m else cpv


def entries(d):
    for line in open(os.path.join(d, 'CONTENTS'), errors='surrogateescape'):
        yield line.rstrip('\n')


def path_of(line):
    kind = line.split(' ', 1)[0]
    rest = line[len(kind) + 1:]
    if kind == 'obj':
        return rest.rsplit(' ', 2)[0]
    if kind == 'sym':
        return rest.split(' -> ')[0]
    return rest


def canon(p):
    """A path as the image's split /bin and /lib see it, for comparing."""
    for a, b in (('/bin/', '/usr/bin/'), ('/sbin/', '/usr/bin/'), ('/usr/sbin/', '/usr/bin/'),
                 ('/lib64/', '/usr/lib64/'), ('/lib/', '/usr/lib/')):
        if p.startswith(a):
            return b + p[len(a):]
    return p


SUF = {'alpha': 0, 'beta': 1, 'pre': 2, 'rc': 3, '': 4, 'p': 5}


def up(v):
    """Comparable upstream version (Gentoo's revision left out)."""
    v = re.sub(r'-r\d+$', '', v)
    m = re.match(r'^([\d.]+)([a-z]?)((?:_(?:alpha|beta|pre|rc|p)\d*)*)$', v)
    if not m:
        return ((0,), '', ())
    nums = tuple(int(x) for x in m.group(1).split('.') if x)
    sufs = tuple((SUF[a], int(b or 0)) for a, b in re.findall(r'_(alpha|beta|pre|rc|p)(\d*)', m.group(3))) + ((4, 0),)
    return (nums, m.group(2), sufs)


def ver(cpv):
    return cpv[len(cp_of(cpv)) + 1:]


def stem(p):
    """libfoo.so.1.2.3 -> (dir, libfoo.so): one library whatever its version."""
    c = canon(p)
    m = re.match(r'^(.*/)([^/]+\.so)(\.[0-9][0-9.]*)?$', c)
    return (m.group(1), m.group(2)) if m else None


# What Portage needs of an installed package. The libraries each needs
# (NEEDED, NEEDED.ELF.2) let preserve-libs keep an old soname (libxml2.so.2)
# while Google's programs still use it, when Gentoo's newer version replaces
# the package it came from.
WANT = {'CATEGORY', 'PF', 'SLOT', 'EAPI', 'repository', 'COUNTER', 'BUILD_TIME', 'KEYWORDS',
        'IUSE', 'IUSE_EFFECTIVE', 'USE', 'LICENSE', 'DEPEND', 'RDEPEND', 'BDEPEND', 'PDEPEND',
        'IDEPEND', 'INHERITED', 'DESCRIPTION', 'SIZE', 'PROVIDES', 'REQUIRES', 'CONTENTS',
        'NEEDED', 'NEEDED.ELF.2', 'DEFINED_PHASES'}


# Development files: what build_image's INSTALL_MASK leaves out of an image
# that a build needs (headers, pkg-config, CMake and aclocal files, *-config
# scripts; not the cross-compiler's x86_64-cros-linux-gnu-*-config copies).
DEV = re.compile(r'^/usr/(include/|lib(64)?/pkgconfig/|share/pkgconfig/|lib(64)?/cmake/|share/cmake/'
                 r'|share/aclocal/|lib(64)?/[^/]+/include/|bin/(?!x86_64-cros-linux-gnu-)[^/]+-config$)')
# Google's C++ libraries are built against LLVM's libc++: a program of
# Gentoo's (libstdc++) can't link with them, headers or not (docs/unified.md,
# "C++ libraries").
CXX = {'dev-cpp/abseil-cpp', 'dev-libs/protobuf', 'dev-libs/re2', 'net-libs/grpc',
       'dev-libs/libphonenumber', 'dev-cpp/gflags', 'dev-libs/leveldb', 'app-arch/snappy'}
# Google's libraries that stay beside Gentoo's in PRIV: the C++ ones, and
# minigbm, Chrome's buffer allocator, whose libgbm.so.1 has the name of
# Gentoo's Mesa's GBM (one Mesa, Gentoo's: vision decision 5).
BESIDE = CXX | {'media-libs/minigbm'}
# 32-bit libraries of packages Google keeps whole, from the Gentoo root's
# builds (it has abi_x86_32 for them): what 32-bit programs need of them.
# virtual/libudev just says abi_x86_32 too.
LIB32 = {'sys-apps/dbus': ['libdbus-1.so.3'], 'sys-apps/systemd-utils': ['libudev.so.1'],
         'virtual/libudev': []}
# Flags the root's 32-bit builds have that the records then say too: D-Bus's
# X (Steam's launcher wants a 32-bit libdbus built with it). It changes only
# dbus-launch's X11 autolaunch, not libdbus (which links libc alone); the
# session bus is NickelOS's.
LIB32_FLAGS = {'sys-apps/dbus': ['X']}
# Replaced by Gentoo's Mesa: Google's (Chrome's) and NickelOS's second build
# for Linux apps.
MESAS = ('media-libs/mesa-reven', 'media-libs/nickel-mesa')
# Except Google's radeonsi DRI driver, for minigbm alone: its AMD backend
# allocates through the DRI interface, which Gentoo's Mesa 26's
# dri/radeonsi_dri.so (a stub loading libgallium) no longer has. Kept in
# PRIV/dri, where minigbm loads it RTLD_LOCAL (minigbm patch 0002), with
# the libglapi it needs in PRIV, found through its RUNPATH. Without it Chrome
# had no GBM device on AMD: nothing on screen (the AMD test PC, 2026-10-20). It is
# linked -Bsymbolic (nickel-mesa-apart.bashrc; nickel-unify checks): Gentoo's
# libgallium exports some of the names it defines.
MESA_KEEP = {'/usr/lib64/dri/radeonsi_dri.so': '/usr/lib64/cros/dri/radeonsi_dri.so',
             '/usr/lib64/libglapi.so.0.0.0': '/usr/lib64/cros/libglapi.so.0'}
# What was built against nickel-mesa's GBM (Xwayland: its RPATH) wants
# Mesa's, Gentoo's now, not minigbm's.
MESA_GBM_RPATH = '/usr/lib64/nickel-mesa'
# The board's sysroot in its files (/build/nickel/usr/lib64/libical.so in a
# CMake file, -L/build/amd64-generic/usr/lib64 in curl-config): on the
# device the same files are at /usr.
SYSROOT = re.compile(rb'/build/[A-Za-z0-9_.-]+(?=/usr/)')


# Libraries kept Google's beside Gentoo's (vision decision 3): Google's copy
# in PRIV, found through its users' RUNPATH. The C++ libraries move there
# here; libwayland-server and libcares are copied there later (nickel-unify:
# Gentoo's packages of those are in the image) for these programs only.
PRIV = '/usr/lib64/cros'
PRIV_USERS = ('/usr/bin/sommelier', '/usr/bin/dnsproxyd', '/usr/lib64/libnet-base.so')
LIB = re.compile(r'^/(?:usr/)?lib64/(lib[^/]+\.so\.[0-9][^/]*)$')
DEVLINK = re.compile(r'^/(?:usr/)?lib64/lib[^/]+\.so$')


def elf_dynamic(path):
    """(NEEDED, RUNPATH or RPATH, SONAME) of a 64-bit little-endian ELF file,
    or None for anything else."""
    try:
        with open(path, 'rb') as f:
            h = f.read(64)
            if len(h) < 64 or h[:4] != b'\x7fELF' or h[4] != 2 or h[5] != 1:
                return None
            shoff, = struct.unpack_from('<Q', h, 0x28)
            shentsize, shnum = struct.unpack_from('<HH', h, 0x3a)
            if not shoff or not shnum or shentsize != 64:
                return None
            f.seek(shoff)
            sh = f.read(64 * shnum)
            secs = [struct.unpack_from('<IIQQQQIIQQ', sh, 64 * i) for i in range(len(sh) // 64)]
            for sec in secs:
                if sec[1] != 6 or sec[6] >= len(secs):  # SHT_DYNAMIC, its strings
                    continue
                f.seek(sec[4])
                dyn = f.read(sec[5])
                st = secs[sec[6]]
                f.seek(st[4])
                strs = f.read(st[5])
                need, rp, so = [], '', ''
                for i in range(0, len(dyn) - 15, 16):
                    tag, val = struct.unpack_from('<qQ', dyn, i)
                    if tag == 0:
                        break
                    if tag in (1, 14, 15, 29) and val < len(strs):
                        v = strs[val:strs.index(b'\0', val)].decode('utf-8', 'replace')
                        if tag == 1:
                            need.append(v)
                        elif tag == 14:
                            so = v
                        else:
                            rp = v
                return need, rp, so
    except (OSError, ValueError, struct.error):
        return None
    return None


def loader_dirs(root):
    """The directories the loader searches after a file's RUNPATH: the
    image's ld.so.conf (with its includes), then /lib64 and /usr/lib64."""
    seen, out = set(), []

    def read(pattern):
        for p in sorted(glob.glob(root + pattern)):
            if p in seen:
                continue
            seen.add(p)
            for l in open(p, errors='replace'):
                l = l.split('#')[0].strip()
                if l.startswith('include '):
                    for inc in l.split()[1:]:
                        read(inc if inc.startswith('/') else '/etc/' + inc)
                elif l.startswith('/'):
                    out.append(l)
    read('/etc/ld.so.conf')
    return out + ['/lib64', '/usr/lib64']


def strip_atoms(t, cps):
    """Dependency atoms on packages in cps taken out of a dependency string."""
    alt = '|'.join(re.escape(c) for c in cps)
    t = re.sub(r'(?<!\S)[<>=~]*(?:%s)(?=-[0-9]|[:\[\s]|$)\S*' % alt, '', t)
    while True:
        n = re.sub(r'\|\|\s*\(\s*\)|(?<!\S)\S+\?\s*\(\s*\)|(?<!\S)\(\s*\)', '', t)
        if n == t:
            break
        t = n
    return re.sub(r'\s+', ' ', t).strip()


def move_private(installed, vdb):
    """Google's libraries of the records in vdb that stay beside Gentoo's
    (BESIDE: the C++ ones, minigbm) moved to PRIV,
    their records removed (and taken out of installed). Returns the CONTENTS
    lines of what stays (the libraries at their new paths, other files where
    they are), {old path: new path} and the records removed."""
    os.makedirs(ROOT + PRIV, exist_ok=True)
    adds, moved, gone = [], {}, []
    for c in sorted(BESIDE & set(installed)):
        d = os.path.join(vdb, installed[c])
        for l in entries(d):
            if l.startswith('dir '):
                continue
            p = path_of(l)
            m = LIB.match(p)
            if DEVLINK.match(p):
                if os.path.lexists(ROOT + p):
                    os.unlink(ROOT + p)
            elif m and os.path.lexists(ROOT + p):
                q = PRIV + '/' + m.group(1)
                os.rename(ROOT + p, ROOT + q)
                moved[canon(p)] = q
                adds.append(l.replace(p, q, 1))
            elif os.path.lexists(ROOT + p):
                adds.append(l)
        gone.append(installed.pop(c))
        shutil.rmtree(d)
    return adds, moved, gone


SKIP = ('/proc', '/sys', '/dev', '/run', '/tmp', '/var', '/home', '/mnt', '/media',
        '/usr/local', '/usr/lib/debug', '/build')


def elf_files(root):
    """(path, (NEEDED, RUNPATH)) of each dynamically linked ELF file of an
    image's own root filesystem (not what's mounted on it, nor debug files)."""
    for d, sub, files in os.walk(root):
        rel = '/' if d == root else '/' + os.path.relpath(d, root)
        if rel != '/' and (os.path.ismount(d) or rel.startswith(SKIP)):
            sub[:] = []
            continue
        for fn in files:
            p = os.path.join(d, fn)
            if os.path.islink(p) or not ('.so' in fn or os.access(p, os.X_OK)):
                continue
            e = elf_dynamic(p)
            if e and e[0]:
                yield os.path.join(rel, fn), e


def lib_names(root):
    """Every file name in the image's library directories (/usr/lib64,
    /usr/lib, /opt and below): a library one file doesn't find on its path
    may still be there for the program that loads it (localsearch's
    modules, Samba's private libraries)."""
    names = set()
    for top in ('/usr/lib64', '/usr/lib', '/opt'):
        for d, sub, files in os.walk(root + top):
            if d.startswith(root + '/usr/lib/debug'):
                sub[:] = []
                continue
            names.update(files)
            names.update(x for x in sub if os.path.islink(os.path.join(d, x)))
    return names


def priv_users(root, sonames):
    """{path: RUNPATH} of the files that load a library from PRIV: those
    needing one of sonames, and PRIV_USERS."""
    return {canon(p): e[1] for p, e in elf_files(root)
            if (set(e[0]) & sonames or p in PRIV_USERS) and PRIV not in e[1].split(':')
            and MESA_GBM_RPATH not in e[1].split(':')}


def restore_dev(cpv, d):
    """The board's development files of one package back into the image and
    its record. Returns how many files."""
    bd = os.path.join(BOARD, 'var/db/pkg', cpv)
    if not os.path.isfile(os.path.join(bd, 'CONTENTS')):
        return 0
    have = {path_of(l) for l in entries(d)}
    bdirs, add = {}, []
    for l in entries(bd):
        p = path_of(l)
        if l.startswith('dir '):
            bdirs[p] = l
            continue
        if not DEV.match(p) or os.path.lexists(ROOT + p):
            continue
        src, dst = BOARD + p, ROOT + p
        kind = l.split(' ', 1)[0]
        if kind == 'sym':
            if not os.path.islink(src):
                continue
            to = os.readlink(src)
            at = os.path.normpath(os.path.join(os.path.dirname(p), to))
            if DEV.match(at) or os.path.lexists(ROOT + at):
                os.makedirs(os.path.dirname(dst), exist_ok=True)
                os.symlink(to, dst)
                add.append(l)
                continue
            # A link to what stays out (curl-config to the cross-compiler's
            # x86_64-cros-linux-gnu-curl-config): the script itself, by the
            # link's name.
            src = os.path.realpath(BOARD + at)
            if not src.startswith(BOARD + '/') or not os.path.isfile(src):
                continue
            kind = 'obj'
        if kind == 'obj' and os.path.isfile(src) and not os.path.islink(src):
            data = open(src, 'rb').read()
            if p.startswith('/usr/bin/') and not data.startswith(b'#!'):
                continue
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            shutil.copy2(src, dst)
            fixed = SYSROOT.sub(b'', data)
            if fixed != data:
                with open(dst, 'wb') as f:
                    f.write(fixed)
            if fixed != data or not l.startswith('obj '):
                l = 'obj %s %s %d' % (p, hashlib.md5(fixed).hexdigest(), int(os.stat(dst).st_mtime))
            add.append(l)
    if not add:
        return 0
    # The directories they're in, as the board's record lists them.
    dirs = set()
    for l in add:
        q = os.path.dirname(path_of(l))
        while q not in ('/', '') and q not in dirs:
            dirs.add(q)
            q = os.path.dirname(q)
    new = [bdirs[q] for q in sorted(dirs) if q in bdirs and q not in have]
    new += [l for l in add if path_of(l) not in have]
    if new:
        with open(os.path.join(d, 'CONTENTS'), 'a', errors='surrogateescape') as f:
            f.write('\n'.join(new) + '\n')
    return len(add)


def write_record(d, meta):
    os.makedirs(d, exist_ok=True)
    for k, v in meta.items():
        with open(os.path.join(d, k), 'w') as f:
            f.write(v + ('\n' if v else ''))


def main():
    vdb = ROOT + '/var/db/pkg'
    installed = {}
    trimmed = 0
    for cat in sorted(os.listdir(vdb)):
        for pv in sorted(os.listdir(os.path.join(vdb, cat))):
            d = os.path.join(vdb, cat, pv)
            if not os.path.isfile(os.path.join(d, 'CONTENTS')):
                continue
            for k in os.listdir(d):
                if k not in WANT:
                    p = os.path.join(d, k)
                    shutil.rmtree(p) if os.path.isdir(p) else os.unlink(p)
                    trimmed += 1
            with open(os.path.join(d, 'DEFINED_PHASES'), 'w') as f:
                f.write('\n')
            installed[cp_of(cat + '/' + pv)] = cat + '/' + pv
    print("records of the image's packages: %d (%d files of build environments left out)" % (
        len(installed), trimmed))
    # Google's blockers and slot operators: rules of its build tree, not
    # facts about files. A slot operator (dev-libs/openssl:0/3=) asks
    # Portage to rebuild the package when that library's slot changes; the
    # OS's packages are never rebuilt from Gentoo's tree (most aren't in it),
    # and with them in your world (@nickel-os) every update would try.
    unblocked = 0
    for cpv in installed.values():
        for k in ('RDEPEND', 'DEPEND', 'BDEPEND', 'PDEPEND', 'IDEPEND'):
            f = os.path.join(vdb, cpv, k)
            if not os.path.exists(f):
                continue
            t = open(f).read()
            n = re.sub(r'(?<!\S)!!?\S+', '', t)
            n = re.sub(r':([^\s\[/=:]*)(?:/[^\s\[=]*)?=', lambda m: ':' + m.group(1) if m.group(1) else '', n)
            if n != t:
                open(f, 'w').write(re.sub(r' +', ' ', n))
                unblocked += 1
    print('blockers and slot operators taken out of %d records' % unblocked)

    def slot(d):
        try:
            return open(os.path.join(d, 'SLOT')).read().split('/')[0].strip()
        except OSError:
            return '0'

    def provides_of(d):
        try:
            return set(open(os.path.join(d, 'PROVIDES')).read().split()) - {'x86_64:'}
        except OSError:
            return set()

    def slot_of(d):
        try:
            return open(os.path.join(d, 'SLOT')).read().strip()
        except OSError:
            return ''
    gall = {}
    for c in os.listdir(GVDB):
        for p in os.listdir(os.path.join(GVDB, c)):
            gall.setdefault(cp_of(c + '/' + p), []).append(c + '/' + p)
    # Gentoo's installed package of the image's slot (gtk+:3, not gtk+:2).
    ginst = {}
    for cp, pvs in gall.items():
        want = slot(os.path.join(vdb, installed[cp])) if cp in installed else None
        same = [pv for pv in pvs if slot(os.path.join(GVDB, pv)) == want]
        ginst[cp] = (same or pvs)[-1]
    # Where the image's version is newer upstream than Gentoo stable's
    # (NickelOS's ports ahead of Gentoo, now and then Google's), the image's
    # stays (vision: "use Google's for that one"), and Portage must not
    # downgrade it as another package's dependency either: these go to
    # emerge's --exclude with the held-back ones.
    newer = sorted(c for c in installed if c in ginst and not c.startswith('virtual/')
                   and up(ver(installed[c])) > up(ver(ginst[c])))
    for c in newer:
        print('  newer than Gentoo, kept: %s (Gentoo %s)' % (installed[c], ver(ginst[c])))
    swap = []
    for c in read_list(SWAP):
        if c not in installed:
            print('  swap set: %s is not in this image' % c)
        elif c not in ginst:
            print('  swap set: the Gentoo root has no %s' % c)
        elif c not in newer:
            swap.append(c)
    print('swap set: %d packages' % len(swap))
    with open(OUT, 'w') as f:
        f.write('\n'.join(swap) + '\n')
    with open(OUT + '.newer', 'w') as f:
        f.write('\n'.join(newer) + '\n')

    # ChromiumOS gives its own packages their version as subslot (CUPS:
    # 0/2.4.19-r366); Gentoo's packages built against Gentoo's of them name
    # Gentoo's subslot (bluez: net-print/cups:0/0=), and on the device a
    # world update would replace Google's to match. Where Gentoo's subslot is
    # no version of its own (an ABI number, or none), the image's record
    # takes Gentoo's. Real ABI subslots (boost 0/1.81.0 against 0/1.90.0)
    # stay.
    reslotted = []

    def gentoo_slot(c):
        """Gentoo's SLOT for c and its version: the Gentoo root's installed
        package, else the newest in Gentoo's tree."""
        if c in ginst:
            return slot_of(os.path.join(GVDB, ginst[c])), ver(ginst[c]), provides_of(os.path.join(GVDB, ginst[c]))
        best = None
        for f in glob.glob(os.path.join(repo, 'metadata/md5-cache', c + '-[0-9]*')):
            v = os.path.basename(f)[len(c.split('/')[1]) + 1:]
            if best is None or up(v) > up(best[1]):
                m = re.search(r'^SLOT=(.*)$', open(f).read(), re.M)
                best = ((m.group(1).strip() if m else '0'), v)
        return (best[0], best[1], None) if best else ('', '', None)

    def majors(pr):
        return {m.group(1) for m in (re.search(r'\.so\.(\d+)', x) for x in pr) if m}

    repo = os.path.normpath(os.path.join(GVDB, '../repos/gentoo'))
    for c, cpv in sorted(installed.items()):
        if c in swap or c in BESIDE or c.startswith('virtual/'):
            continue
        d = os.path.join(vdb, cpv)
        mine = slot_of(d)
        theirs, gv, gprov = gentoo_slot(c)
        if not mine or not theirs or mine.split('/')[0] != theirs.split('/')[0] or mine == theirs:
            continue
        v = ver(cpv)
        sub = mine.split('/', 1)[1] if '/' in mine else None
        gsub = theirs.split('/', 1)[1] if '/' in theirs else theirs.split('/')[0]
        prov = provides_of(d)
        # Only where the subslot of Google's names no ABI of its own (none,
        # or ChromiumOS's version) and the image's provides the libraries
        # Gentoo's subslot stands for: all of them among Gentoo's installed
        # package's (Gentoo's libva has one more), or (Gentoo's not in the root) a library whose major
        # version is Gentoo's subslot (libva.so.2 for 0/2), or none at all
        # where Gentoo's has no subslot (CUPS: 0).
        if sub is not None and sub not in (v, re.sub(r'-r\d+$', '', v)):
            continue
        if gprov is not None:
            ok = bool(prov) and prov <= gprov
        elif '/' in theirs:
            ok = gsub in majors(prov)
        else:
            ok = True
        if ok:
            with open(os.path.join(d, 'SLOT'), 'w') as f:
                f.write(theirs + '\n')
            reslotted.append('%s %s->%s' % (cp_of(cpv), mine, theirs))
    print("ChromiumOS's version subslots replaced by Gentoo's: %s" % (', '.join(reslotted) or 'none'))

    # Gentoo's multilib packages ask for their libraries' ABI flag
    # (media-libs/libva[abi_x86_64(-)] from Gentoo's Mesa); Google's records
    # have no such flags, and a flag missing counts as off. The image's are
    # 64-bit builds: where Gentoo's package of the name is multilib, the
    # record says abi_x86_64.
    repo = os.path.normpath(os.path.join(GVDB, '../repos/gentoo'))
    abi = []
    for c, cpv in sorted(installed.items()):
        if c in swap or c.startswith('virtual/'):
            continue
        cache = glob.glob(os.path.join(repo, 'metadata/md5-cache', c + '-[0-9]*'))
        if not cache or not any(re.search(r'^IUSE=.*\babi_x86_64\b', open(f).read(), re.M) for f in cache[:1]):
            continue
        d = os.path.join(vdb, cpv)
        changed = False
        for k in ('IUSE', 'IUSE_EFFECTIVE', 'USE'):
            f = os.path.join(d, k)
            cur = open(f).read().split() if os.path.exists(f) else []
            if 'abi_x86_64' not in cur:
                with open(f, 'w') as fh:
                    fh.write(' '.join(cur + ['abi_x86_64']) + '\n')
                changed = True
        if changed:
            abi.append(cp_of(cpv))
    print('abi_x86_64 recorded for %d of the image\'s own multilib packages' % len(abi))

    # 32-bit libraries of the packages Google keeps whole (D-Bus: its
    # dbus-send options that ChromeOS's scripts use; udev): 32-bit programs
    # (Steam, Wine) need libdbus-1 and libudev, and Portage builds a package
    # for 32 and 64 bits at once. The Gentoo root's own 32-bit builds of the
    # same libraries go into the image's /usr/lib, under Google's records,
    # which then say abi_x86_32: Portage keeps Google's packages.
    groot = os.path.normpath(os.path.join(GVDB, '../../..'))
    lib32 = []
    for c, libs in LIB32.items():
        if c not in installed:
            continue
        d = os.path.join(vdb, installed[c])
        add32 = []
        for so in libs:
            src = os.path.realpath(groot + '/usr/lib/' + so)
            if not os.path.isfile(src):
                continue
            real = '/usr/lib/' + os.path.basename(src)
            os.makedirs(ROOT + '/usr/lib', exist_ok=True)
            if not os.path.lexists(ROOT + real):
                shutil.copy2(src, ROOT + real)
            add32.append('obj %s %s %d' % (real, hashlib.md5(open(src, 'rb').read()).hexdigest(),
                                           int(os.stat(ROOT + real).st_mtime)))
            if os.path.basename(real) != so and not os.path.lexists(ROOT + '/usr/lib/' + so):
                os.symlink(os.path.basename(real), ROOT + '/usr/lib/' + so)
                add32.append('sym /usr/lib/%s -> %s %d' % (so, os.path.basename(real),
                                                           int(os.lstat(ROOT + '/usr/lib/' + so).st_mtime)))
        if add32:
            with open(os.path.join(d, 'CONTENTS'), 'a', errors='surrogateescape') as f:
                f.write('\n'.join(add32) + '\n')
        if add32 or not libs:
            for k in ('IUSE', 'IUSE_EFFECTIVE', 'USE'):
                f = os.path.join(d, k)
                cur = open(f).read().split() if os.path.exists(f) else []
                for flag in ('abi_x86_64', 'abi_x86_32') + tuple(
                        LIB32_FLAGS.get(c, []) if k == 'USE' else []):
                    if flag not in cur:
                        cur.append(flag)
                with open(f, 'w') as fh:
                    fh.write(' '.join(cur) + '\n')
            lib32.append('%s%s' % (cp_of(installed[c]), (' (%s)' % ' '.join(libs)) if libs else ''))
    print('32-bit libraries beside the packages Google keeps: %s' % (', '.join(lib32) or 'none'))

    # Development files of the packages that stay the image's (Google's,
    # NickelOS's) where Gentoo has the package too: a source build of
    # Gentoo's that uses GTK 3 needs GTK 3's headers. The swap set gets
    # Gentoo's own; Google's C++ libraries none.
    # And those Gentoo has under a virtual's name only: Google's udev is
    # virtual/libudev's libudev, and without libudev.pc a source build of
    # anything using it failed (VLC 4, build 23f).
    DEV_ALSO = {'sys-fs/udev'}
    repo = os.path.normpath(os.path.join(GVDB, '../repos/gentoo'))
    n = pk = 0
    for c, cpv in sorted(installed.items()):
        if c in swap or c in CXX or c.startswith('virtual/') or (
                not os.path.isdir(os.path.join(repo, c)) and c not in DEV_ALSO):
            continue
        k = restore_dev(cpv, os.path.join(vdb, cpv))
        n += k
        pk += 1 if k else 0
    print('development files back from the board: %d, of %d packages' % (n, pk))

    # Google's C++ libraries beside Gentoo's: their libraries to PRIV (their
    # links for building, libfoo.so, go: a program of Gentoo's can't link
    # with them), their other files to ChromeOS's additions, their records
    # out, and with them every dependency of Google's records on them (the
    # image has them). Gentoo's re2, snappy, leveldb and gflags carry the
    # same library names as Google's: installed into /usr/lib64 as the
    # record's newer version, they replaced Google's under ChromeOS's
    # programs.
    priv_adds, moved, gone = move_private(installed, vdb)
    sonames = {os.path.basename(q) for q in moved.values()}
    stripped = 0
    for cpv in installed.values():
        for k in ('RDEPEND', 'DEPEND', 'BDEPEND', 'PDEPEND', 'IDEPEND'):
            f = os.path.join(vdb, cpv, k)
            if os.path.exists(f):
                t = open(f).read()
                n = strip_atoms(t, BESIDE)
                if n != t.strip():
                    open(f, 'w').write(n + ('\n' if n else ''))
                    stripped += 1
    print("Google's C++ libraries beside Gentoo's: %d files to %s, records out: %s; "
          "dependencies on them out of %d records" % (len(moved), PRIV, ' '.join(gone), stripped))

    # Virtual packages have no files: at Gentoo's version, so Gentoo's
    # packages' newer requirements (virtual/libudev >= 251) are met.
    for c, cpv in list(installed.items()):
        if c.startswith('virtual/') and c in ginst and up(ver(ginst[c])) > up(ver(cpv)):
            new = os.path.join(vdb, ginst[c])
            os.rename(os.path.join(vdb, cpv), new)
            with open(os.path.join(new, 'PF'), 'w') as f:
                f.write(ginst[c].split('/', 1)[1] + '\n')
            installed[c] = ginst[c]
            print('  %s: recorded as %s' % (cpv, ginst[c]))
    # One Mesa, Gentoo's (vision decision 5), installed in step 3: Google's
    # Mesa (Chrome's) and nickel-mesa (NickelOS's second build, for Linux
    # apps) go, records and files, but for Google's AMD driver and its
    # libglapi, which move to PRIV for minigbm (MESA_KEEP; their lines join
    # ChromeOS's additions).
    kept_all = []
    for c in MESAS:
        if c not in installed:
            continue
        d = os.path.join(vdb, installed[c])
        n, kept = 0, []
        for l in entries(d):
            p = path_of(l)
            if l.startswith('dir ') or not os.path.lexists(ROOT + p) or os.path.isdir(ROOT + p):
                continue
            q = MESA_KEEP.get(p) if c == 'media-libs/mesa-reven' else None
            if q:
                os.makedirs(os.path.dirname(ROOT + q), exist_ok=True)
                os.rename(ROOT + p, ROOT + q)
                priv_adds.append(l.replace(p, q, 1))
                kept.append(q)
                continue
            os.unlink(ROOT + p)
            n += 1
        shutil.rmtree(d)
        print("  %s: out for Gentoo's Mesa (%d files)%s" % (
            installed.pop(c), n, ', kept for minigbm: ' + ' '.join(kept) if kept else ''))
        kept_all += kept
    # The driver finds the libglapi beside it through its RUNPATH (below).
    sonames.update(os.path.basename(q) for q in kept_all if os.path.basename(q).startswith('lib'))
    for p in ('/usr/lib64/nickel-mesa', '/usr/share/nickel-mesa'):
        shutil.rmtree(ROOT + p, ignore_errors=True)

    # One Python, Gentoo's. Google's is a slot of its own (3.11), so step 3
    # installs Gentoo's beside it rather than over it: Google's goes, record
    # and files, and with it everything for it (Google's builds of Python
    # packages Gentoo's replace, python-exec's 3.11 wrappers), so none of it
    # stays as ChromeOS's additions below. python3 is python-exec, which
    # knows only Gentoo's Python; our own Python packages are built for it
    # (device-repo.txt). Python 3.11's PyGObject crashed against Gentoo's
    # GLib (waydroid, 2026-10-19).
    # A base image has no Python of Google's at all (its test packages bring
    # one later, for Google's test tools), but Google's builds of Python
    # packages in it leave their files for 3.11 with nothing to run them:
    # every Python directory not of Gentoo's slot goes either way.
    c = 'dev-lang/python'
    if c in ginst:
        keep = slot_of(os.path.join(GVDB, ginst[c])).split('/')[0]
        n = 0
        for base in ('/usr/lib', '/usr/lib64', '/usr/lib/python-exec', '/usr/include'):
            for e in (os.listdir(ROOT + base) if os.path.isdir(ROOT + base) else []):
                m = re.fullmatch(r'python(3\.\d+)', e)
                p = ROOT + base + '/' + e
                if m and m.group(1) != keep and os.path.isdir(p) and not os.path.islink(p):
                    n += sum(len(fs) for _, _, fs in os.walk(p))
                    shutil.rmtree(p)
        if c in installed:
            d = os.path.join(vdb, installed[c])
            for l in entries(d):
                p = path_of(l)
                if not l.startswith('dir ') and os.path.lexists(ROOT + p) and not os.path.isdir(ROOT + p):
                    os.unlink(ROOT + p)
                    n += 1
            shutil.rmtree(d)
            print("  %s: out for Gentoo's Python %s" % (installed.pop(c), ver(ginst[c])))
        print("  Python other than Gentoo's %s: %d files out" % (keep, n))

    # ChromeOS's additions: files only Google's version of a swapped package
    # has, and no Gentoo package has either (Gentoo's app-alternatives/bzip2
    # owns /usr/bin/bzip2, not app-arch/bzip2: Google's copy goes with its
    # package, or bzip2's own install step can't make its link).
    allg = set()
    for c in os.listdir(GVDB):
        for p in os.listdir(os.path.join(GVDB, c)):
            for l in entries(os.path.join(GVDB, c, p)):
                if not l.startswith('dir '):
                    allg.add(canon(path_of(l)))
    adds = list(priv_adds)
    # Every package Gentoo has too, not only the swap set: Portage may
    # replace any of them as another's dependency (Portage itself needs
    # Gentoo's baselayout), and replacing removes what only the old one had.
    for c in sorted(set(installed) & set(ginst)):
        if c.startswith('virtual/'):
            continue
        gpaths = [path_of(l) for l in entries(os.path.join(GVDB, ginst[c]))]
        gcont = {canon(p) for p in gpaths}
        gstems = {stem(p) for p in gpaths} - {None}
        d = os.path.join(vdb, installed[c])
        mine, theirs = [], []
        for l in entries(d):
            if l.startswith('dir '):
                mine.append(l)
                continue
            p = path_of(l)
            if canon(p) in gcont or canon(p) in allg or stem(p) in gstems or not os.path.lexists(ROOT + p):
                mine.append(l)
            else:
                theirs.append(l)
        if theirs:
            with open(os.path.join(d, 'CONTENTS'), 'w', errors='surrogateescape') as f:
                f.write('\n'.join(mine) + '\n')
            adds += theirs
            print("  %s: %d of ChromeOS's own files kept apart (%s)" % (
                installed[c], len(theirs), ' '.join(path_of(l) for l in theirs[:3])))
    # Every directory of the image's root filesystem (not what build_image
    # mounts on it from the data partition: /var, /usr/local,
    # /mnt/stateful_partition). Gentoo keeps an empty directory with a .keep
    # file: Portage removes empty directories when it replaces a package that
    # listed them (Google's brltty listed /run, and Upstart can't start
    # without it).
    dirs, kept_dirs = [], []
    root = os.path.realpath(ROOT)
    for d, sub, files in os.walk(root):
        # The root itself is a mount point too (build_image's image).
        rel = '/' if d == root else '/' + os.path.relpath(d, root)
        if rel != '/' and (os.path.ismount(d) or rel.startswith(('/proc', '/sys', '/dev'))):
            sub[:] = []
            continue
        if rel == '/' or os.path.islink(d):
            continue
        dirs.append('dir ' + rel)
        if (not sub and not files and rel not in ('/tmp', '/mnt', '/media', '/home')) or rel == '/run':
            keep = os.path.join(d, '.keep_nickel-base_chromeos-additions-0')
            open(keep, 'w').close()
            adds.append('obj %s d41d8cd98f00b204e9800998ecf8427e %d' % (
                keep[len(root):], int(os.stat(keep).st_mtime)))
            kept_dirs.append(rel)
    add = os.path.join(vdb, 'nickel-base', 'chromeos-additions-1')
    write_record(add, {
        'CATEGORY': 'nickel-base', 'PF': 'chromeos-additions-1', 'SLOT': '0', 'EAPI': '8',
        'repository': 'nickel', 'KEYWORDS': '*', 'LICENSE': 'BSD-Google', 'IUSE': '', 'USE': '',
        'DESCRIPTION': "ChromeOS's own files in packages Gentoo provides (NickelOS)",
        'COUNTER': '1', 'BUILD_TIME': str(int(time.time())), 'DEFINED_PHASES': '',
        'RDEPEND': '', 'DEPEND': '', 'BDEPEND': '', 'IDEPEND': '', 'PDEPEND': '', 'INHERITED': ''})
    with open(os.path.join(add, 'CONTENTS'), 'w', errors='surrogateescape') as f:
        f.write('\n'.join(dirs + adds) + '\n')
    print("ChromeOS's directories recorded: %d, %d empty ones kept with .keep files" % (
        len(dirs), len(kept_dirs)))
    # The libraries ChromeOS's own files need, in Portage's form: with them
    # preserve-libs keeps an old soname they use (libsframe.so.2) when Gentoo
    # replaces the package it came from.
    needed, needed2 = [], []
    for l in adds:
        if not l.startswith('obj '):
            continue
        p = path_of(l)
        try:
            with open(ROOT + p, 'rb') as fh:
                h = fh.read(5)
        except OSError:
            continue
        if h[:4] != b'\x7fELF' or h[4:5] != b'\x02':
            continue
        t = subprocess.run(['readelf', '-dW', ROOT + p], capture_output=True, text=True).stdout
        need = re.findall(r'\(NEEDED\)\s+Shared library: \[([^\]]+)\]', t)
        if not need:
            continue
        so = re.search(r'\(SONAME\)\s+Library soname: \[([^\]]+)\]', t)
        rp = re.search(r'\((?:RUNPATH|RPATH)\)\s+Library r(?:un)?path: \[([^\]]+)\]', t)
        needed.append('%s %s' % (p, ','.join(need)))
        needed2.append('X86_64;%s;%s;%s;%s;x86_64' % (p, so.group(1) if so else '',
                                                     rp.group(1) if rp else '', ','.join(need)))
    with open(os.path.join(add, 'NEEDED'), 'w') as f:
        f.write('\n'.join(needed) + '\n')
    with open(os.path.join(add, 'NEEDED.ELF.2'), 'w') as f:
        f.write('\n'.join(needed2) + '\n')
    print("ChromeOS's additions: %d files in nickel-base/chromeos-additions" % len(adds))

    # The files that load a library from PRIV: every one needing a library
    # moved there, and PRIV_USERS. nickel-unify adds PRIV to their RUNPATH;
    # their records' NEEDED.ELF.2 say so already, for Portage's view of
    # which library each file uses.
    users = priv_users(root, sonames)
    with open(OUT + '.rpath', 'w') as f:
        f.write(''.join(p + '\n' for p in sorted(users)))
    fixed = 0
    for cat in os.listdir(vdb):
        for pv in os.listdir(os.path.join(vdb, cat)):
            f = os.path.join(vdb, cat, pv, 'NEEDED.ELF.2')
            if not os.path.exists(f):
                continue
            lines = open(f, errors='surrogateescape').read().splitlines()
            out = []
            for l in lines:
                x = l.split(';')
                if len(x) >= 6 and canon(x[1]) in users:
                    x[3] = (x[3] + ':' + PRIV) if x[3] else PRIV
                    fixed += 1
                out.append(';'.join(x))
            if out != lines:
                with open(f, 'w', errors='surrogateescape') as fh:
                    fh.write('\n'.join(out) + '\n')
    print('RUNPATH %s for %d files (%d recorded)' % (PRIV, len(users), fixed))


if __name__ == '__main__':
    main()
