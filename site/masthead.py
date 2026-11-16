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
# GitHub's mark (Octicons' mark-github, MIT), as GitHub asks it be used: to
# link to the project there.
GITHUB = "https://github.com/dresden196/nickelos"
GITHUB_MARK = ('<svg viewBox="0 0 16 16" aria-hidden="true" focusable="false"><path fill="currentColor" '
               'd="M6.766 11.328c-2.063-.25-3.516-1.734-3.516-3.656 0-.781.281-1.625.75-2.188-.203-.515-.172-1.609.063-2.062.625-.078 1.468.25 1.968.703.594-.187 1.219-.281 1.985-.281.765 0 1.39.094 1.953.265.484-.437 1.344-.765 1.969-.687.218.422.25 1.515.046 2.047.5.593.766 1.39.766 2.203 0 1.922-1.453 3.375-3.547 3.64.531.344.89 1.094.89 1.954v1.625c0 .468.391.734.86.547C13.781 14.359 16 11.53 16 8.03 16 3.61 12.406 0 7.984 0 3.563 0 0 3.61 0 8.031a7.88 7.88 0 0 0 5.172 7.422c.422.156.828-.125.828-.547v-1.25c-.219.094-.5.156-.75.156-1.031 0-1.64-.562-2.078-1.609-.172-.422-.36-.672-.719-.719-.187-.015-.25-.093-.25-.187 0-.188.313-.328.625-.328.453 0 .844.281 1.25.86.313.452.64.655 1.031.655s.641-.14 1-.5c.266-.265.47-.5.657-.656"/></svg>')

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
        links = [("Features", "/#what"), ("Guide", "/help/")]
        if rel:
            links += [("Blog", "/blog/"), ("Forum", FORUM)]
    nav = "\n".join(
        '      <a href="%s"%s>%s</a>' % (href, ' aria-current="page"' if label == current else "", label)
        for label, href in links)
    button = ""
    if not offline:
        # The project on GitHub, left of the Download button.
        button = ('\n      <a class="masthead-github" href="%s" aria-label="NickelOS on GitHub" '
                  'title="NickelOS on GitHub">%s</a>' % (GITHUB, GITHUB_MARK))
    if rel:
        here_attr = ' aria-current="page"' if current == "Download" else ""
        button += ('\n      <a class="masthead-download" href="/download/"%s>%s<span>Download</span></a>'
                   % (here_attr, ARROW))
    if button:
        button = '\n    <div class="masthead-actions">%s\n    </div>' % button
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
