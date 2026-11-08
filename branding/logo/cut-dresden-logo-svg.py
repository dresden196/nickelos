#!/usr/bin/env python3
# Dresden's logo (2026-10-29: the blue-to-teal disc with his waves and the
# calligraphic N) as a vector with a transparent background, from his logo
# sheet (../from-dresden/logo-sheet-2026-10-29.svg, 2912x1458). The sheet's
# large "512px" disc is 69 coloured paths over the sheet's light background;
# the N is that background seen through the disc. Kept: those paths as they
# are, in their order. Added under them: a white circle just inside the
# disc's edge, so the N is white wherever the disc is, as in the earlier
# logo. The rest of the sheet (its background, the other sizes, the
# monochrome logo, the labels) is left out.
#   cut-dresden-logo-svg.py SHEET.svg OUT.svg
import re
import sys

src = open(sys.argv[1]).read()
keep = []
for attrs, d in re.findall(r'<path([^>]*?)d="([^"]*)"', src, re.S):
    fill = re.search(r'fill="#([0-9A-Fa-f]{6})"', attrs).group(1)
    r, g, b = (int(fill[i:i + 2], 16) for i in (0, 2, 4))
    n = list(map(float, re.findall(r'-?\d+(?:\.\d+)?', d)))
    x0, x1, y0, y1 = min(n[0::2]), max(n[0::2]), min(n[1::2]), max(n[1::2])
    # The large disc's area, and colour (not the grey background or labels).
    if x0 >= 1000 and x1 <= 1790 and y0 >= 110 and y1 <= 900 and \
            max(r, g, b) - min(r, g, b) > 30:
        keep.append('<path fill="#%s" d="%s"/>' % (fill.upper(), ' '.join(d.split())))
assert len(keep) == 69, len(keep)
# The disc: centre from the coloured paths' extent (x 1029-1694, y 165-827);
# its edge is 327-329 from the centre all round except where the N leaves it.
cx, cy, r, half = 1361.5, 496.0, 326.0, 345
open(sys.argv[2], 'w').write(
    '<svg xmlns="http://www.w3.org/2000/svg" viewBox="%g %g %d %d">'
    '<title>NickelOS</title>'
    '<circle cx="%g" cy="%g" r="%g" fill="#FFFFFF"/>%s</svg>\n'
    % (cx - half, cy - half, 2 * half, 2 * half, cx, cy, r, ''.join(keep)))
