#!/usr/bin/env python3
# NickelOS wallpapers: the NickelOS brush stroke (../stroke/brush.py), very
# large, sweeping across warm nickel (light) or graphite (dark), as the
# website's hero streak does.
#   make-wallpaper.py OUTDIR  ->  nickel-light.svg, nickel-dark.svg (2560x1440)
import os, sys

here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(here, "..", "stroke"))
import brush
out = os.environ.get("OUT", here)

W, H = 2560, 1440
stroke = brush.outline(38)    # thinner relative to its size than the logo's

def wall(name, bg0, bg1, sheen):
    svg = f'''<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}">
<defs>
  <linearGradient id="bg" x1="0" y1="0" x2="1" y2="1">
    <stop offset="0" stop-color="{bg0}"/><stop offset="1" stop-color="{bg1}"/>
  </linearGradient>
  <linearGradient id="n" x1="-200" y1="200" x2="200" y2="-200" gradientUnits="userSpaceOnUse">
    <stop offset="0" stop-color="#2f56a0"/><stop offset="1" stop-color="#22a99c"/>
  </linearGradient>
  <radialGradient id="glow" cx="0.62" cy="0.42" r="0.6">
    <stop offset="0" stop-color="{sheen}" stop-opacity="0.35"/>
    <stop offset="1" stop-color="{sheen}" stop-opacity="0"/>
  </radialGradient>
</defs>
<rect width="{W}" height="{H}" fill="url(#bg)"/>
<rect width="{W}" height="{H}" fill="url(#glow)"/>
<g transform="translate(1500 900) scale(5.2)">
  <path d="{stroke}" fill="url(#n)"/>
</g>
</svg>
'''
    with open(os.path.join(out, name), "w") as f:
        f.write(svg)

wall("nickel-light.svg", "#ebe7df", "#cfc8bb", "#ffffff")
wall("nickel-dark.svg", "#2a2925", "#171614", "#4a6f8f")
print("written to", out)
