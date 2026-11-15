#!/usr/bin/env python3
"""index.html from index.src.html, and download/index.html for a release.

  build-pages.py HERO_STROKE

A release is release.json beside this file (scripts/release/nickel-release
site writes it): then index.src.html's <!--rel--> blocks stay and its
<!--pre--> ones go, {{NAME}}, {{SIZE}}, {{DATE}} and the rest are filled in
from it, and download/index.html is made from download.src.html. Without
one, the site says there's no download yet and there is no download/.
"""
import datetime
import json
import os
import re
import shutil
import sys

from masthead import masthead

here = os.path.dirname(os.path.abspath(__file__))
os.chdir(here)
rel = json.load(open("release.json")) if os.path.exists("release.json") else None
keep, drop = ("rel", "pre") if rel else ("pre", "rel")


def announce_date(rel):
    """"8 November 2026" -> "November 8", for the front page's announcement."""
    try:
        d = datetime.datetime.strptime(rel["date"], "%d %B %Y")
    except (KeyError, ValueError):
        return rel.get("date", "")
    return "%s %d" % (d.strftime("%B"), d.day)


if rel:
    rel = dict(rel, announce_date=announce_date(rel))


def fill(s, current=""):
    s = s.replace("{{MASTHEAD}}", masthead(current))
    # A closing marker at the start of a line takes that line's newline
    # with it, so neither choice leaves a blank line.
    s = re.sub(r"(?m)^(<!--/(?:pre|rel)-->)\n", r"\1", s)
    s = re.sub(r"<!--%s-->.*?<!--/%s-->" % (drop, drop), "", s, flags=re.S)
    s = s.replace("<!--%s-->" % keep, "").replace("<!--/%s-->" % keep, "")
    for k, v in (rel or {}).items():
        s = s.replace("{{%s}}" % k.upper(), str(v))
    left = set(re.findall(r"\{\{[A-Z_0-9]+\}\}", s)) - {"{{HERO_STROKE}}"}
    if left:
        sys.exit("build-pages.py: not filled in: " + " ".join(sorted(left)))
    return s


index = fill(open("index.src.html").read()).replace("{{HERO_STROKE}}", sys.argv[1])
open("index.html", "w").write(index)
print("index.html: %d bytes%s" % (len(index.encode()), ", release %s" % rel["name"] if rel else ""))
shutil.rmtree("download", ignore_errors=True)
if rel:
    os.mkdir("download")
    open("download/index.html", "w").write(fill(open("download.src.html").read(), "Download"))
    print("download/index.html: NickelOS %s" % rel["name"])
