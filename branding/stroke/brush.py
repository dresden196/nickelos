# The NickelOS brush stroke: a filled outline around a centreline of cubic
# curves, rounded at the start, full width through the middle, tapering to
# a point at the end. The website's hero streak (site/make-hero-stroke.py)
# and the wallpapers (branding/wallpaper/make-wallpaper.py) are this stroke
# along their own centrelines. It is not a logo: the logo is Dresden's
# (branding/logo/nickelos-logo-dresden.png).
#
#   import brush; brush.SEGS = [...]; brush.outline(width)  ->  SVG path data
import math

# The centreline, set by the caller: a list of cubic segments
# ((x0, y0), (x1, y1), (x2, y2), (x3, y3)), each starting where the last ends.
SEGS = [((-118, 112), (-94, 56), (-70, -42), (-36, -54)),
        ((-36, -54), (-4, -64), (6, 48), (32, 62)),
        ((32, 62), (64, 79), (114, -66), (178, -178))]

def bez(p, t):
    a, b, c, d = p
    u = 1 - t
    return (u**3*a[0] + 3*u*u*t*b[0] + 3*u*t*t*c[0] + t**3*d[0],
            u**3*a[1] + 3*u*u*t*b[1] + 3*u*t*t*c[1] + t**3*d[1])

def centreline(n=120):
    pts = []
    for i, s in enumerate(SEGS):
        for k in range(n + (1 if i == len(SEGS) - 1 else 0)):
            pts.append(bez(s, k / n))
    return pts

def width(f, full):
    # f: 0..1 along the stroke. A brush stroke: starts at 85% width (a
    # rounded start inside the disc), full by 8%, holds past the disc's
    # edge, then tapers to a point over the last 14%, outside it.
    if f < 0.08:
        return full * (0.85 + 0.15 * math.sin(f / 0.08 * math.pi / 2))
    if f > 0.86:
        g = (f - 0.86) / 0.14
        return full * max(0.0, math.cos(g * math.pi / 2)) ** 0.9
    return full

def outline(full):
    pts = centreline()
    # Arc-length fractions.
    d = [0.0]
    for a, b in zip(pts, pts[1:]):
        d.append(d[-1] + math.dist(a, b))
    L = d[-1]
    left, right = [], []
    for i, p in enumerate(pts):
        a = pts[max(i - 1, 0)]
        b = pts[min(i + 1, len(pts) - 1)]
        tx, ty = b[0] - a[0], b[1] - a[1]
        n = math.hypot(tx, ty) or 1
        nx, ny = -ty / n, tx / n
        w = width(d[i] / L, full) / 2
        left.append((p[0] + nx * w, p[1] + ny * w))
        right.append((p[0] - nx * w, p[1] - ny * w))
    # A round start: from the right edge back around to the left edge.
    p0 = pts[0]
    tx, ty = pts[1][0] - p0[0], pts[1][1] - p0[1]
    n = math.hypot(tx, ty)
    tx, ty = tx / n, ty / n
    nx, ny = -ty, tx
    w = width(0, full) / 2
    cap = [(p0[0] + w * (math.cos(a) * nx - math.sin(a) * tx),
            p0[1] + w * (math.cos(a) * ny - math.sin(a) * ty))
           for a in (math.pi - k * math.pi / 16 for k in range(1, 16))]
    poly = left + right[::-1] + cap
    return "M" + " L".join(f"{x:.1f} {y:.1f}" for x, y in poly) + " Z"
