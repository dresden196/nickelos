#!/usr/bin/env python3
# Makes the Unified root's PKGDIR (uf-pkgs, the image's and the binhost's)
# hold the instance the root has installed, for every package: one whose
# own instance is missing is packed from the root (quickpkg) and its other
# instances go. Run it after any emerge in the root. Packages Portage takes
# from Gentoo's binhost land in the root's own PKGDIR, so uf-pkgs kept only
# older builds of them, and the image's install (which doesn't respect USE)
# took one: procps with Gentoo's flags broke build 23 (2026-10-20).
#
# uf-binpkg-sync.py ROOT [--dry-run]   (on the build host, as root)
import os, re, subprocess, sys

R = sys.argv[1]
dry = "--dry-run" in sys.argv
P, V = R + "/var/cache/uf-pkgs", R + "/var/db/pkg"
pack, drop = [], []
for c in sorted(os.listdir(V)):
    for pf in sorted(os.listdir(f"{V}/{c}")):
        try:
            b = open(f"{V}/{c}/{pf}/BUILD_ID").read().strip()
        except OSError:
            continue
        pn = re.sub(r"-\d.*", "", pf)
        d = f"{P}/{c}/{pn}"
        inst = [f for f in (os.listdir(d) if os.path.isdir(d) else [])
                if re.fullmatch(re.escape(pf) + r"-\d+\.gpkg\.tar", f)]
        if f"{pf}-{b}.gpkg.tar" not in inst:
            pack.append(f"={c}/{pf}")
            drop += [f"{d}/{f}" for f in inst]
print(f"to pack: {len(pack)}, stale instances: {len(drop)}")
if dry:
    print("\n".join(pack))
    sys.exit(0)
for f in drop:
    os.remove(f)
if pack:
    env = dict(os.environ, NICKEL_GENTOO_ROOT=R)
    subprocess.run(["nickel-gentoo-run",
                    "PKGDIR=/var/cache/uf-pkgs quickpkg --include-config=y --umask=022 "
                    + " ".join(pack) + " >/dev/null 2>&1; "
                    "PKGDIR=/var/cache/uf-pkgs emaint --fix binhost >/dev/null 2>&1"],
                   env=env, check=True)
