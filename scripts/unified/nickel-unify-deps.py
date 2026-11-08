#!/usr/bin/env python3
# NickelOS's image step, the dependencies of the OS's own records
# (nickel-unify, step 4, after Gentoo's packages are in; run by the Unified
# Gentoo root's Python, whose Portage reads the image's records): Google's
# and NickelOS's records keep only the dependencies the image itself meets.
#   - build-time ones (DEPEND, BDEPEND) go: the OS's packages are never built
#     from Gentoo's tree on the device
#   - a run-time one nothing in the image meets is a fact of Google's build
#     tree (Rust crates' sources, eselect modules, Python 3.11's targets), not
#     of the image; it goes, and an "any of" group keeps what is met
# Without this, emerge --depclean refused to run at all ("dependencies could
# not be completely resolved"), and world updates tried to install them.
# Gentoo's own records are left as Portage wrote them.
#   nickel-unify-deps.py ROOT
import os, sys
import portage
from portage.dep import Atom, paren_enclose, use_reduce

ROOT = sys.argv[1]
# Portage's databases are made for $ROOT (nickel-unify sets it to the image).
if os.path.realpath(str(portage.root)) != os.path.realpath(ROOT):
    sys.exit('%s: run with ROOT=%s' % (sys.argv[0], ROOT))
vardb = portage.db[str(portage.root)]["vartree"].dbapi


def keep(struct):
    """The dependency structure with what the image doesn't meet left out."""
    out, i = [], 0
    while i < len(struct):
        x = struct[i]
        if x == '||':
            alts = keep(struct[i + 1])
            if len(alts) == 1:
                out.append(alts[0])
            elif alts:
                out += ['||', alts]
            i += 2
            continue
        if isinstance(x, list):
            sub = keep(x)
            if sub:
                out.append(sub)
        elif isinstance(x, Atom):
            if x.blocker or vardb.match(x):
                out.append(x)
        i += 1
    return out


def unmet(struct):
    """The atoms of a dependency structure the image doesn't meet."""
    out, i = [], 0
    while i < len(struct):
        x = struct[i]
        if x == '||':
            alts = struct[i + 1]
            if not keep(alts):
                out.append('|| ( %s )' % paren_enclose(alts))
            i += 2
            continue
        if isinstance(x, list):
            out += unmet(x)
        elif isinstance(x, Atom) and not x.blocker and not vardb.match(x):
            out.append(str(x))
        i += 1
    return out


changed, dropped, gentoo_unmet = 0, 0, []
for cpv in vardb.cpv_all():
    repo, eapi, use = vardb.aux_get(cpv, ['repository', 'EAPI', 'USE'])
    if repo == 'gentoo':
        # Gentoo's own are reported: Portage reads them from Gentoo's ebuild
        # anyway, so what they need must be in the image.
        for k in ('RDEPEND', 'PDEPEND', 'IDEPEND'):
            t = vardb.aux_get(cpv, [k])[0]
            try:
                for a in unmet(use_reduce(t, uselist=use.split(), eapi=eapi or '8', token_class=Atom)):
                    gentoo_unmet.append('%s %s: %s' % (cpv, k, a))
            except portage.exception.InvalidDependString:
                pass
        continue
    d = os.path.join(ROOT, 'var/db/pkg', cpv)
    new = {'DEPEND': '', 'BDEPEND': ''}
    for k in ('RDEPEND', 'PDEPEND', 'IDEPEND'):
        f = os.path.join(d, k)
        if not os.path.exists(f):
            continue
        t = open(f).read().strip()
        try:
            s = use_reduce(t, uselist=use.split(), eapi=eapi or '8', token_class=Atom)
        except portage.exception.InvalidDependString:
            new[k] = ''
            continue
        k2 = keep(s)
        n = len(portage.dep.flatten(s)) - len(portage.dep.flatten(k2))
        dropped += max(n, 0)
        new[k] = paren_enclose(k2)
    for k, v in new.items():
        f = os.path.join(d, k)
        old = open(f).read().strip() if os.path.exists(f) else None
        if old is None and not v:
            continue
        if old != v:
            with open(f, 'w') as fh:
                fh.write(v + ('\n' if v else ''))
            changed += 1
print("dependencies the image doesn't meet: %d taken out of the OS's records (%d files)" % (dropped, changed))
for l in gentoo_unmet:
    print("  Gentoo's package needs what the image hasn't: " + l)
