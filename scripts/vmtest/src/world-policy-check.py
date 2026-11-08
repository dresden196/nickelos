#!/usr/bin/python3
# Runs in the test VM as root (gentoo-test.sh, step 7), after Gentoo's
# setup: vision decision 1, "Who updates what" (Dresden, 2026-10-21).
#   1. A world update (emerge -puDN @world) leaves the OS's packages at the
#      OS's versions: none of the OS's packages that have files (@nickel-os;
#      virtuals have none and follow your world) is upgraded or downgraded.
#      Rebuilding one with your flags is allowed and not counted.
#   2. One line in /etc/portage/package.unmask takes a newer version of an
#      OS package: the first one held at the OS's version
#      (profile/package.mask/00-nickel-os-versions) that Gentoo has a newer
#      stable version of, and that your layer hasn't taken over already, is
#      unmasked, and the world update then upgrades it.
#      "works" also when Gentoo has nothing newer to try yet, so the line
#      doesn't change with Gentoo's moves; the package tried is printed only
#      when it fails.
#   3. A package that needs that newer version installs with a plain
#      emerge: NickelOS's make.conf lets Portage unmask what a dependency
#      needs (autounmask, nickel-gentoo r29). A package in a scratch
#      repository depending on >= the newer version, planned (-p).
# Nothing is installed (-p), and the unmask line and the scratch repository
# are removed again.
import os, re, subprocess
import portage
from portage.versions import catpkgsplit, pkgcmp

PLAN = re.compile(r'^\[(?:ebuild|binary)\s*([^\]]*)\]\s+(\S+)')
UNMASK_DIR = '/etc/portage/package.unmask'
UNMASK = UNMASK_DIR + '/zz-world-policy-check'


def world():
    out = subprocess.run(['emerge', '-puDN', '--quiet', '@world'],
                         capture_output=True, text=True)
    plan = []
    for line in (out.stdout + out.stderr).splitlines():
        m = PLAN.match(line)
        if m:
            plan.append((m.group(1).strip(), m.group(2)))
    return out.returncode, plan


os_names = set()
for line in open('/etc/portage/sets/nickel-os'):
    line = line.strip()
    if not line or line.startswith('#'):
        continue
    atom = line.lstrip('<>=~').split(':')[0]
    if atom.startswith('virtual/'):
        continue
    os_names.add(re.sub(r'-\d[^/]*$', '', atom))

rc, plan = world()
changed = [p for flags, p in plan
           if 'U' in flags and re.sub(r'-\d[^/]*$', '', p) in os_names]
print('a world update resolves: %s; OS packages it would update: %d%s' % (
    'yes' if rc == 0 else 'NO (%d)' % rc, len(changed),
    (' (' + ' '.join(changed) + ')') if changed else ''))

portdb = portage.db[portage.root]['porttree'].dbapi
vardb = portage.db[portage.root]['vartree'].dbapi
tried = None
for line in open('/etc/portage/profile/package.mask/00-nickel-os-versions'):
    if not line.startswith('>'):
        continue
    cat, pn, ver, rev = catpkgsplit(line[1:].split(':')[0].strip())
    # One your layer has already taken over (a newer version installed, as
    # unified-update-test's f) has nothing left to take.
    if any(pkgcmp(catpkgsplit(c)[1:], (pn, ver, rev)) > 0
           for c in vardb.match('%s/%s' % (cat, pn))):
        continue
    for cpv in portdb.xmatch('match-all', '%s/%s' % (cat, pn)):
        if cpv.endswith('9999') or pkgcmp(catpkgsplit(cpv)[1:], (pn, ver, rev)) <= 0:
            continue
        keys = ['KEYWORDS', 'LICENSE', 'SLOT', 'repository']
        meta = dict(zip(keys, portdb.aux_get(cpv, keys)))
        meta['USE'] = ''
        # Only the OS's mask in the way: a licence the device doesn't accept
        # (unrar's unRAR, non-free, 17.9's check) is the user's own choice,
        # which package.unmask rightly doesn't lift.
        if portage.settings._getMissingLicenses(cpv, meta):
            continue
        if 'amd64' in meta['KEYWORDS'].split():
            tried = '%s/%s' % (cat, pn)
            newer = cpv
            break
    if tried:
        break
if not tried:
    print('one line in package.unmask takes a newer OS package: works')
else:
    # emerge's autounmask makes package.unmask a file when there is none
    # (an earlier step's =cpv): then the line goes in it, for this check only.
    old = None
    if os.path.isfile(UNMASK_DIR):
        with open(UNMASK_DIR) as f:
            old = f.read()
        with open(UNMASK_DIR, 'a') as f:
            f.write(tried + '\n')
    else:
        os.makedirs(UNMASK_DIR, exist_ok=True)
        with open(UNMASK, 'w') as f:
            f.write(tried + '\n')
    try:
        rc, plan = world()
        taken = any('U' in flags and re.sub(r'-\d[^/]*$', '', p) == tried
                    for flags, p in plan)
    finally:
        if old is not None:
            with open(UNMASK_DIR, 'w') as f:
                f.write(old)
        else:
            os.unlink(UNMASK)
            try:
                os.rmdir(UNMASK_DIR)
            except OSError:
                pass
    print('one line in package.unmask takes a newer OS package: %s' % (
        'works' if taken else 'FAILED (%s)' % tried))

# 3. Needed by a package: a plain emerge takes it.
if not tried:
    print('a package needing a newer OS package installs: works')
else:
    repo = '/var/tmp/nickel-needs-newer'
    conf = '/etc/portage/repos.conf/zz-nickel-needs-newer.conf'
    pkg = os.path.join(repo, 'app-misc/nickel-needs-newer')
    os.makedirs(os.path.join(repo, 'profiles'), exist_ok=True)
    os.makedirs(os.path.join(repo, 'metadata'), exist_ok=True)
    os.makedirs(pkg, exist_ok=True)
    os.makedirs(os.path.dirname(conf), exist_ok=True)
    with open(os.path.join(repo, 'profiles/repo_name'), 'w') as f:
        f.write('nickel-needs-newer\n')
    with open(os.path.join(repo, 'metadata/layout.conf'), 'w') as f:
        f.write('masters = gentoo\nthin-manifests = true\n')
    with open(os.path.join(pkg, 'nickel-needs-newer-1.ebuild'), 'w') as f:
        f.write('EAPI=8\nDESCRIPTION="test"\nHOMEPAGE="https://nickelos.dev"\n'
                'S="${WORKDIR}"\nLICENSE="MIT"\nSLOT="0"\nKEYWORDS="amd64"\n'
                'RDEPEND=">=%s"\n' % newer)
    with open(conf, 'w') as f:
        f.write('[nickel-needs-newer]\nlocation = %s\npriority = 1\n' % repo)
    try:
        subprocess.run(['ebuild', 'nickel-needs-newer-1.ebuild', 'manifest'],
                       cwd=pkg, capture_output=True)
        out = subprocess.run(['emerge', '-p', '--quiet', 'app-misc/nickel-needs-newer'],
                             capture_output=True, text=True)
        text = out.stdout + out.stderr
        plan = [PLAN.match(l).groups() for l in text.splitlines() if PLAN.match(l)]
        # Planned with -p: Portage shows the mask change it would make and the
        # whole plan, but exits 1, as it can't make the change in a pretend
        # run (a real emerge makes it and goes on). Without autounmask there
        # is no plan, only "have been masked".
        planned = (any(re.sub(r'-\d[^/]*$', '', p) == tried and 'U' in flags
                       for flags, p in plan) and
                   any(p.startswith('app-misc/nickel-needs-newer') for flags, p in plan) and
                   'have been masked' not in text)
    finally:
        os.unlink(conf)
        subprocess.run(['rm', '-rf', repo])
    print('a package needing a newer OS package installs: %s' % (
        'works' if planned else 'FAILED (%s)' % newer))
