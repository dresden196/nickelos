#!/usr/bin/env python3
# The flat version of Dresden's logo (2026-10-29: one blue, no waves) as a
# vector with a transparent background, from his flat logo sheet
# (../from-dresden/logo-sheet-flat-2026-10-29.svg, 1548x775). The sheet is
# three even-odd paths, one per colour; the large "512px" disc is two parts
# of the blue one (the disc on either side of the N). Kept: those two parts
# as they are. Added under them: a white circle just inside the disc's edge,
# so the N is white, as in the wave logo (nickelos-logo.svg).
#   cut-dresden-logo-flat-svg.py SHEET.svg OUT.svg
import re
import sys

src = open(sys.argv[1]).read()
path = next(m for m in re.finditer(r'<path([^>]*)>', src) if 'fill="#3a77a0"' in m.group(1))
d = re.search(r'd="([^"]*)"', path.group(1)).group(1)
parts = []
for sub in (x.strip() for x in re.split(r'(?=M)', d.strip()) if x.strip()):
    n = list(map(float, re.findall(r'-?\d+(?:\.\d+)?', sub)))
    if min(n[0::2]) >= 500 and max(n[0::2]) <= 940 and \
            min(n[1::2]) >= 50 and max(n[1::2]) <= 470:   # the large disc
        parts.append(sub)
assert len(parts) == 2, len(parts)
# The disc's circle, fitted to its edge away from the N: centre (722.3,
# 262.8), radius 175.0 (174.6-175.4 all round but where the N leaves it).
cx, cy, r = 722.30, 262.79, 174.0
half = 175.0 * 345 / 326          # the same margin as nickelos-logo.svg
open(sys.argv[2], 'w').write(
    '<svg xmlns="http://www.w3.org/2000/svg" viewBox="%.2f %.2f %.2f %.2f">'
    '<title>NickelOS</title>'
    '<circle cx="%.2f" cy="%.2f" r="%.2f" fill="#FFFFFF"/>'
    '<path fill="#3A77A0" fill-rule="evenodd" stroke="#3A77A0" '
    'stroke-width="0.25" stroke-linejoin="round" d="%s"/></svg>\n'
    % (cx - half, cy - half, 2 * half, 2 * half, cx, cy, r, ' '.join(parts)))
