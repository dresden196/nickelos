#!/usr/bin/env python3
# For the UI tests: a screen taken through the Screenshot portal (PNG,
# 1280x800) against the default wallpaper (the small JPEG, shown centred
# and not scaled). Standard library only, with netpbm.
#   screen-compare.py SCREEN.png WALLPAPER.jpg picture  the middle is mostly
#                     a blue-teal picture, not the wallpaper
#   screen-compare.py SCREEN.png WALLPAPER.jpg default  the middle is the
#                     wallpaper
import subprocess
import sys


def pnm(tool, path):
    data = subprocess.run([tool, path], capture_output=True, check=True).stdout
    fields, pos = [], 0
    while len(fields) < 4:
        while data[pos:pos + 1].isspace():
            pos += 1
        end = pos
        while not data[end:end + 1].isspace():
            end += 1
        fields.append(data[pos:end])
        pos = end
    pixels = data[pos + 1:]
    if fields[0] == b"P5":
        pixels = bytes(b for v in pixels for b in (v, v, v))
    return int(fields[1]), int(fields[2]), pixels


def at(image, x, y):
    w, _, pixels = image
    i = (y * w + x) * 3
    return pixels[i:i + 3]


screen = pnm("pngtopnm", sys.argv[1])
mode = sys.argv[3]
wall = pnm("jpegtopnm", sys.argv[2])
dx, dy = (wall[0] - screen[0]) // 2, (wall[1] - screen[1]) // 2
same = blue = total = 0
for y in range(200, 600, 4):
    for x in range(300, 1000, 4):
        s, w = at(screen, x, y), at(wall, x + dx, y + dy)
        total += 1
        same += max(abs(a - b) for a, b in zip(s, w)) <= 24
        r, g, b = s
        blue += r < 110 and b > r + 50 and g > 60
same, blue = same * 100 // total, blue * 100 // total
if mode == "picture":
    print("  the screen shows it" if blue >= 80 and same < 50 else
          f"  screen: FAILED, {blue}% blue-teal, {same}% the default wallpaper")
else:
    print("  the screen shows the default" if same >= 90 else
          f"  default: FAILED, {same}% the default wallpaper")
