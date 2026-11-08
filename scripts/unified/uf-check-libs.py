#!/usr/bin/env python3
# NickelOS: does every program and library of an image find the
# libraries it needs? Each ELF file's NEEDED is looked up as the loader does:
# its RUNPATH (or RPATH, $ORIGIN expanded), then the directories of the
# image's ld.so.conf, then /lib64 and /usr/lib64. Prints what isn't found
# anywhere in the image.
# nickel-unify runs it at the end of step 4; it reads only.
#   uf-check-libs.py ROOT
import importlib.util, os, sys

ROOT = os.path.realpath(sys.argv[1])
HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location('records', os.path.join(HERE, 'nickel-unify-records.py'))
rec = importlib.util.module_from_spec(spec)
sys.argv = [sys.argv[0], ROOT, '', '', '', '']
spec.loader.exec_module(rec)


system = rec.loader_dirs(ROOT)
missing, n = [], 0
for here, e in rec.elf_files(ROOT):
    n += 1
    rel = os.path.dirname(here)
    dirs = [x.replace('$ORIGIN', rel).replace('${ORIGIN}', rel) for x in e[1].split(':') if x] + system
    for so in e[0]:
        if '/' in so:
            ok = os.path.exists(ROOT + so)
        else:
            ok = any(os.path.exists(ROOT + os.path.join(x, so)) for x in dirs)
        if not ok:
            missing.append((here, so))
# Not on the file's path but in the image: most likely there for the program
# that loads the file (a module needing its program's library).
names = rec.lib_names(ROOT)
nowhere = [(h, so) for h, so in missing if os.path.basename(so) not in names]
print('libraries checked for %d files: %d not on their path, %d of them nowhere in the image' % (
    n, len(missing), len(nowhere)))
for here, so in nowhere[:40]:
    print('  %s: %s' % (here, so))
if len(nowhere) > 40:
    print('  ... %d more' % (len(nowhere) - 40))
