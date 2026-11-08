#!/usr/bin/env python3
# The hero's streak for index.html: the NickelOS brush stroke
# (branding/stroke/brush.py) along its own centreline over the hero
# (viewBox 0 0 1600 1060).
# The screenshot's frame covers x 100-1500, y 160-948, so the stroke shows
# only where it enters below the frame at the bottom left and where it
# breaks out above the top right, tapering to a point.
import os, sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "..", "branding", "stroke"))
import brush

brush.SEGS = [((24, 1010), (120, 820), (330, 330), (520, 300)),
              ((520, 300), (650, 280), (700, 760), (860, 780)),
              ((860, 780), (1060, 800), (1300, 250), (1580, 26))]
print(brush.outline(64))
