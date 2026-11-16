#!/usr/bin/env python3
# NickelOS's logo for fastfetch, made from Dresden's flat logo
# (branding/logo/nickelos-logo-flat.svg) and nothing else, so the shapes are
# his. Two kinds, both fastfetch logo files:
#   ascii   classic ASCII art in two colours, $1 the disc, $2 the N
#           (fastfetch "type": "file" with "color": {"1": ..., "2": ...})
#   blocks  half-block characters in the logo's own colours, two pixels to a
#           character (fastfetch "type": "file-raw": it carries its colours)
#   make-logo.py ascii|blocks ROWS > logo.txt
# A terminal character is about twice as tall as it is wide: ROWS rows are
# 2*ROWS columns.
import os
import subprocess
import sys

here = os.path.dirname(os.path.abspath(__file__))
SVG = os.path.join(here, "..", "logo", "nickelos-logo-flat.svg")


def pixels(w, h):
    png = subprocess.run(["rsvg-convert", "-w", str(w), "-h", str(h), SVG],
                         check=True, capture_output=True).stdout
    raw = subprocess.run(["magick", "png:-", "-depth", "8", "RGBA:-"], input=png,
                         check=True, capture_output=True).stdout
    return [[raw[(y * w + x) * 4:(y * w + x) * 4 + 4] for x in range(w)] for y in range(h)]


def kind(p):
    r, g, b, a = p
    if a < 128:
        return " "
    return "N" if min(r, g, b) > 200 else "D"   # white: the N; else the disc


def stroke(grid, cx, cy, radius):
    """A character along the N's stroke at (cx, cy): the direction of the
    white pixels around it (their principal axis, over a square patch, since
    one tall character cell would make every stroke look upright)."""
    import math
    pts = [(x, -y) for y in range(max(0, cy - radius), min(len(grid), cy + radius))
           for x in range(max(0, cx - radius), min(len(grid[0]), cx + radius)) if grid[y][x] == "N"]
    if len(pts) < 4:
        return "."
    mx = sum(p[0] for p in pts) / len(pts)
    my = sum(p[1] for p in pts) / len(pts)
    xx = sum((p[0] - mx) ** 2 for p in pts)
    yy = sum((p[1] - my) ** 2 for p in pts)
    xy = sum((p[0] - mx) * (p[1] - my) for p in pts)
    ang = math.degrees(0.5 * math.atan2(2 * xy, xx - yy)) % 180
    return "-" if ang < 22.5 or ang >= 157.5 else "/" if ang < 67.5 else "|" if ang < 112.5 else "\\"


def ascii_logo(rows):
    cols = rows * 2
    sx, sy = 6, 12                       # samples per character
    px = pixels(cols * sx, rows * sy)
    grid = [[kind(p) for p in row] for row in px]
    # Denser glyphs where more of the character is covered.
    disc = " .:-=+*#%@"
    n = " .:;!|ilIH"
    out = []
    for r in range(rows):
        line, cur = [], None
        for c in range(cols):
            cell = [grid[r * sy + y][c * sx + x] for y in range(sy) for x in range(sx)]
            d, w = cell.count("D"), cell.count("N")
            total = len(cell)
            if d + w < total * 0.12:
                ch, col = " ", None
            elif w > d * 0.6:
                ch, col = stroke(grid, c * sx + sx // 2, r * sy + sy // 2, sy), "$2"
            else:
                ch, col = disc[min(len(disc) - 1, max(1, round(d / total * (len(disc) - 1))))], "$1"
            if col and col != cur:
                line.append(col)
                cur = col
            line.append(ch)
        out.append("".join(line).rstrip())
    return "\n".join(out) + "\n"


def blocks_logo(rows):
    cols = rows * 2
    sx, sy = 4, 4
    px = pixels(cols * sx, rows * 2 * sy)

    def avg(x0, y0):
        cells = [px[y][x] for y in range(y0, y0 + sy) for x in range(x0, x0 + sx)]
        a = sum(p[3] for p in cells) / len(cells)
        if a < 100:
            return None
        rgb = [round(sum(p[i] * p[3] for p in cells) / max(1, sum(p[3] for p in cells))) for i in range(3)]
        return tuple(rgb)

    out = []
    for r in range(rows):
        line = []
        for c in range(cols):
            top, bot = avg(c * sx, (2 * r) * sy), avg(c * sx, (2 * r + 1) * sy)
            if top is None and bot is None:
                line.append("\x1b[0m ")
            elif bot is None:
                line.append("\x1b[0m\x1b[38;2;%d;%d;%dm▀" % top)
            elif top is None:
                line.append("\x1b[0m\x1b[38;2;%d;%d;%dm▄" % bot)
            else:
                line.append("\x1b[38;2;%d;%d;%dm\x1b[48;2;%d;%d;%dm▀" % (top + bot))
        out.append("".join(line) + "\x1b[0m")
    return "\n".join(out) + "\n"


if __name__ == "__main__":
    what = sys.argv[1] if len(sys.argv) > 1 else "ascii"
    rows = int(sys.argv[2]) if len(sys.argv) > 2 else 16
    sys.stdout.write(ascii_logo(rows) if what == "ascii" else blocks_logo(rows))
