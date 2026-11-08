#!/usr/bin/env python3
# Dresden's logo (the blue-to-teal disc with the calligraphic N, from his
# mock-up sheet, 2026-10-05) as a transparent PNG. In the sheet the N is
# negative space: the light gray background seen through the disc. So the
# disc's circle is fitted from its edges; inside it the N is painted white
# (edges blended by how coloured each pixel is), outside it only the
# coloured tips stay, their colour solved from P = a*F + (1-a)*B against a
# background plane fitted to the border.
#   cut-dresden-logo.py SHEET.png OUT.png
import sys
import numpy as np
from PIL import Image

src = np.asarray(Image.open(sys.argv[1]).convert("RGB"), dtype=np.float64)
img = src[30:310, 340:620]                     # the large disc
h, w, _ = img.shape
yy, xx = np.mgrid[0:h, 0:w]

border = np.zeros((h, w), bool)
border[:6, :] = border[-6:, :] = border[:, :6] = border[:, -6:] = True
A = np.c_[xx[border], yy[border], np.ones(border.sum())]
bg = np.empty_like(img)
for c in range(3):
    coef, *_ = np.linalg.lstsq(A, img[..., c][border], rcond=None)
    bg[..., c] = coef[0] * xx + coef[1] * yy + coef[2]

chroma = img.max(axis=2) - img.min(axis=2)
coloured = chroma > 40

# The circle: from the coloured extents along the row and column through
# the centre, refined a few times (the tips stick out at two corners only).
ys, xs = np.nonzero(coloured)
cy, cx = ys.mean(), xs.mean()
for _ in range(6):
    row = np.nonzero(coloured[int(round(cy))])[0]
    col = np.nonzero(coloured[:, int(round(cx))])[0]
    left, right, top, bottom = row.min(), row.max(), col.min(), col.max()
    cx, cy = (left + right) / 2, (top + bottom) / 2
r = ((right - left) + (bottom - top)) / 4 - 0.5   # just inside the antialiased rim
dist = np.hypot(xx - cx, yy - cy)
inside = np.clip(r - dist + 0.5, 0, 1)         # 1 px soft edge

# Inside: how coloured a pixel is says how much disc it is; the rest is N.
t = np.clip(chroma / 95.0, 0, 1)[..., None]
inner = img + (1 - t) * (255 - bg)
# Outside: the tips.
a_out = np.clip((chroma - (bg.max(axis=2) - bg.min(axis=2)) - 6) / 70.0, 0, 1)
a_out[np.abs(img - bg).max(axis=2) < 4] = 0
outer = (img - (1 - a_out[..., None]) * bg) / np.maximum(a_out[..., None], 0.02)

a = np.maximum(inside, a_out)
F = np.where(inside[..., None] >= a_out[..., None], inner, outer)
out = np.dstack([np.clip(F, 0, 255), a * 255]).astype(np.uint8)
im = Image.fromarray(out, "RGBA")
im = im.crop(im.getbbox())
im.save(sys.argv[2])
print(sys.argv[2], im.size, "circle", round(cx, 1), round(cy, 1), round(r, 1))
