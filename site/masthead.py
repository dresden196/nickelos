"""The header every page of nickelos.dev shares: the front page and the
download page (build-pages.py), the guide (build-help.py) and the blog
(build-blog.py).

A release (release.json beside this file, from scripts/release/
nickel-release-site) adds the blog, the forum and a Download button; before
one, there's no download to point at. The copy of the guide that comes with
NickelOS (offline) keeps the guide's own links and no Download button: it's
read on NickelOS.
"""
import json
import os

here = os.path.dirname(os.path.abspath(__file__))
FORUM = "https://forum.nickelos.dev/"

# A download arrow, drawn in the button's text colour.
ARROW = ('<svg viewBox="0 0 16 16" aria-hidden="true" focusable="false">'
         '<path d="M8 2v8m0 0-3.5-3.5M8 10l3.5-3.5M3 13.5h10" fill="none" '
         'stroke="currentColor" stroke-width="1.8" stroke-linecap="round" '
         'stroke-linejoin="round"/></svg>')


def release():
    path = os.path.join(here, "release.json")
    if not os.path.exists(path):
        return None
    with open(path, encoding="utf-8") as f:
        return json.load(f)


def masthead(current="", offline=False):
    """The <header>. `current` is the label of the page's own link."""
    rel = release() is not None and not offline
    if offline:
        links = [("Guide", "/help/"), ("Troubleshooting", "/help/troubleshooting/"),
                 ("Get help", "/help/community/")]
    else:
        links = [("Features", "/#what"), ("Status", "/#status"), ("Guide", "/help/")]
        if rel:
            links += [("Blog", "/blog/"), ("Forum", FORUM)]
    nav = "\n".join(
        '      <a href="%s"%s>%s</a>' % (href, ' aria-current="page"' if label == current else "", label)
        for label, href in links)
    button = ""
    if rel:
        here_attr = ' aria-current="page"' if current == "Download" else ""
        button = ('\n    <a class="masthead-download" href="/download/"%s>%s<span>Download</span></a>'
                  % (here_attr, ARROW))
    return """<header class="masthead">
  <div class="wrap masthead-row">
    <a class="brand" href="/" aria-label="NickelOS home">
      <img src="/assets/img/nickelos-logo-v3.png" alt="" width="32" height="32">
      <span>NickelOS</span>
    </a>
    <nav aria-label="Site">
%s
    </nav>%s
  </div>
</header>""" % (nav, button)
