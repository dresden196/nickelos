#!/usr/bin/env python3
# NickelOS, plan step 1-2 (docs/vision.md): which of the image's
# libraries Gentoo has under the same name (SONAME), and from which package
# each side's copy comes. Read-only.
#   uf-inventory.py IMAGE_ROOT GENTOO_ROOT BOARD_VDB OUTDIR
import collections, json, multiprocessing, os, re, subprocess, sys

IMG, GEN, BVDB, OUT = sys.argv[1:5]
GVDB = os.path.join(GEN, 'var/db/pkg')
SKIP = ('proc', 'sys', 'dev', 'lib/modules', 'lib/firmware', 'usr/lib/debug', 'var/db/repos',
        'usr/share', 'usr/include', 'usr/src')


def elf_files(root, top=('',)):
    for t in top:
        for d, dirs, files in os.walk(os.path.join(root, t)):
            rel = os.path.relpath(d, root)
            if rel != '.' and any(rel == s or rel.startswith(s + '/') for s in SKIP):
                dirs[:] = []
                continue
            for f in files:
                p = os.path.join(d, f)
                if os.path.islink(p) or not os.path.isfile(p):
                    continue
                try:
                    with open(p, 'rb') as fh:
                        h = fh.read(5)
                except OSError:
                    continue
                if h[:4] == b'\x7fELF' and h[4:5] == b'\x02':
                    yield p


def dyn(p):
    r = subprocess.run(['readelf', '-dhW', p], capture_output=True, text=True)
    t = r.stdout
    so = re.search(r'\(SONAME\)\s+Library soname: \[([^\]]+)\]', t)
    need = re.findall(r'\(NEEDED\)\s+Shared library: \[([^\]]+)\]', t)
    typ = re.search(r'Type:\s+(\w+)', t)
    return p, (so.group(1) if so else None), need, (typ.group(1) if typ else '?')


def owners(vdb, strip=''):
    own = {}
    for cat in os.listdir(vdb):
        cd = os.path.join(vdb, cat)
        if not os.path.isdir(cd):
            continue
        for pv in os.listdir(cd):
            pd = os.path.join(cd, pv)
            try:
                repo = open(os.path.join(pd, 'repository')).read().strip()
            except OSError:
                repo = '?'
            try:
                for line in open(os.path.join(pd, 'CONTENTS'), errors='replace'):
                    if line.startswith('obj '):
                        path = line[4:].rsplit(' ', 2)[0]
                        own[path] = (cat + '/' + pv, repo)
            except OSError:
                pass
    return own


def realpath_in(root, rel):
    return '/' + os.path.relpath(os.path.realpath(os.path.join(root, rel.lstrip('/'))), root)


if __name__ == '__main__':
    os.makedirs(OUT, exist_ok=True)
    with multiprocessing.Pool(48) as pool:
        img = pool.map(dyn, list(elf_files(IMG)), chunksize=16)
        gen = pool.map(dyn, list(elf_files(GEN, ('usr/lib64', 'usr/lib/gcc/x86_64-pc-linux-gnu',
                                                    'usr/bin', 'usr/libexec'))), chunksize=16)
    bown = owners(BVDB)
    gown = owners(GVDB)
    # Gentoo's libraries by soname (64-bit only; merged /usr: /lib64 = /usr/lib64).
    gso = {}
    for p, so, need, typ in gen:
        if so and typ == 'DYN':
            rel = '/' + os.path.relpath(p, GEN)
            gso.setdefault(so, []).append((rel, gown.get(rel, gown.get(rel.replace('/usr/lib64/', '/lib64/', 1), ('?', '?')))))
    rows = []
    for p, so, need, typ in img:
        rel = '/' + os.path.relpath(p, IMG)
        pkg, repo = bown.get(rel, ('?', '?'))
        g = gso.get(so) if so else None
        rows.append({'path': rel, 'soname': so, 'type': typ, 'needed': need, 'pkg': pkg, 'repo': repo,
                     'gentoo': g[0][0] if g else None, 'gentoo_pkg': g[0][1][0] if g else None})
    json.dump(rows, open(os.path.join(OUT, 'image-elf.json'), 'w'), indent=0)
    json.dump({k: v for k, v in gso.items()}, open(os.path.join(OUT, 'gentoo-so.json'), 'w'), indent=0)
    libs = [r for r in rows if r['soname']]
    # Who in the image needs each soname.
    users = collections.Counter(n for r in rows for n in r['needed'])
    by_repo = collections.defaultdict(lambda: [0, 0])
    for r in libs:
        by_repo[r['repo']][0] += 1
        by_repo[r['repo']][1] += bool(r['gentoo'])
    with open(os.path.join(OUT, 'summary.txt'), 'w') as f:
        f.write('image ELF files: %d, with a soname: %d, Gentoo has the soname: %d\n' % (
            len(rows), len(libs), sum(bool(r['gentoo']) for r in libs)))
        f.write('Gentoo 64-bit libraries: %d sonames\n\n' % len(gso))
        f.write('by repository (libraries, Gentoo has the same soname):\n')
        for repo, (n, m) in sorted(by_repo.items(), key=lambda x: -x[1][0]):
            f.write('  %-28s %5d %5d\n' % (repo, n, m))
        f.write('\nmatched (image package -> Gentoo package):\n')
        pairs = collections.Counter((r['pkg'], r['gentoo_pkg']) for r in libs if r['gentoo'])
        for (a, b), n in sorted(pairs.items()):
            f.write('  %-50s %-50s %d\n' % (a, b, n))
        f.write('\nunmatched libraries that something in the image links to (soname, users, package):\n')
        for r in sorted((r for r in libs if not r['gentoo']), key=lambda r: -users[r['soname']]):
            if users[r['soname']]:
                f.write('  %-40s %4d  %s [%s]\n' % (r['soname'], users[r['soname']], r['pkg'], r['repo']))
    print(open(os.path.join(OUT, 'summary.txt')).read()[:3000])
