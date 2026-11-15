#!/bin/sh
# Builds index.html from index.src.html: puts in the hero's stroke
# (make-hero-stroke.py); and the guide in help/ from help-src/
# (build-help.py). The site is the files in this directory, minus the
# sources (*.src.html, *.py, *.sh, *.md, help-src, blog-src, release.json).
set -eu
cd "$(dirname "$0")"
stroke=$(python3 make-hero-stroke.py | tail -n 1)
# With release.json (a release): the download page, and the front page's
# release wording (build-pages.py).
python3 build-pages.py "$stroke"
# The guide, nickelos.dev/help, from help-src/*.md.
python3 build-help.py --offline ../chromeos-base/nickel-guide/files/guide
# The blog, nickelos.dev/blog, from blog-src/*.md (its posts from the release
# on; build-blog.py).
python3 build-blog.py
# (The offline copy is NickelOS's Settings > Help: bump nickel-guide's
# revision when it changes, or the image keeps the old one.)
