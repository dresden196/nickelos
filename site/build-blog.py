#!/usr/bin/env python3
# Builds the NickelOS blog (nickelos.dev/blog) from site/blog-src/*.md into
# site/blog/: blog/index.html (the posts, newest first), blog/<slug>/index.html
# for each post, and blog/feed.xml (Atom). Same header and footer as the rest
# of the site, the guide's Markdown (build-help.py), and a download box under
# each post.
#
# A post starts with "key: value" lines up to "---":
#   title, slug, summary, date (YYYY-MM-DD, or "release": the release's date)
#   release: yes   only once there is a release (release.json)
#   draft: yes     not on the site (the blog editor's drafts)
# Pictures in a post are in blog-src/media/ and go to blog/media/.
#
# The blog's editor keeps the posts on the web server and builds with this
# file there: NICKEL_BLOG_SRC and NICKEL_BLOG_OUT
# say where the posts are and where the blog goes; post_page() is its
# preview.
# In a post, {{NAME}}, {{VERSION}}, {{SIZE}} and {{DATE}} are the release's.
# Without any post to show, there is no blog/ (and the header has no link to
# it before a release anyway).
import datetime
import html
import importlib.util
import os
import shutil
import sys

from masthead import masthead, release

here = os.path.dirname(os.path.abspath(__file__))
src = os.environ.get("NICKEL_BLOG_SRC") or os.path.join(here, "blog-src")
out = os.environ.get("NICKEL_BLOG_OUT") or os.path.join(here, "blog")
SITE = "https://nickelos.dev"
AUTHOR = "dresden196"

spec = importlib.util.spec_from_file_location("build_help", os.path.join(here, "build-help.py"))
build_help = importlib.util.module_from_spec(spec)
spec.loader.exec_module(build_help)

HEAD = """<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>{title}</title>
<meta name="description" content="{summary}">
<meta name="color-scheme" content="light dark">
<meta name="theme-color" content="#f3f1ec" media="(prefers-color-scheme: light)">
<meta name="theme-color" content="#1c1b18" media="(prefers-color-scheme: dark)">
<meta property="og:title" content="{title}">
<meta property="og:description" content="{summary}">
<meta property="og:image" content="https://nickelos.dev/assets/shots/desktop-gimp-kart-terminal-v2.webp">
<meta property="og:url" content="{url}">
<link rel="icon" href="/favicon.png?v=3" type="image/png" sizes="64x64">
<link rel="icon" href="/favicon.ico?v=3" sizes="16x16 32x32 48x48">
<link rel="apple-touch-icon" href="/apple-touch-icon.png?v=3">
<link rel="alternate" type="application/atom+xml" title="NickelOS blog" href="/blog/feed.xml">
<link rel="preload" href="/assets/fonts/GoogleSansFlex-nickelos.woff2" as="font" type="font/woff2" crossorigin>
<link rel="stylesheet" href="/assets/css/site.css?v=5">
</head>
<body>

<a class="skip" href="#main">Skip to the content</a>

{masthead}

<main id="main" class="guide blog">
  <div class="wrap">
    <div class="blog-page">
{body}
    </div>
  </div>
</main>

<footer class="footer">
  <div class="wrap footer-row">
    <div class="footer-brand">
      <img src="/assets/img/nickelos-logo-v3.png" alt="" width="48" height="48">
      <div>
        <p><strong>NickelOS</strong> &middot; Project Crowbar</p>
        <p class="small">nickelos.dev</p>
      </div>
    </div>
    <div class="small footer-notes">
      <p>NickelOS is a community project, not affiliated with or endorsed by Google. ChromeOS, ChromiumOS, Chrome, Chromebook and Googlebook are trademarks of Google LLC. Other names belong to their owners.</p>
      <p>No trackers or analytics. Set in Google Sans Flex and Google Sans Code (<a href="/assets/fonts/OFL.txt">SIL Open Font License</a>).</p>
    </div>
  </div>
</footer>

</body>
</html>
"""


def long_date(d):
    return "%s %d, %d" % (d.strftime("%B"), d.day, d.year)


def cta(rel):
    if not rel:
        return ""
    return ("""      <div class="blog-cta">
        <p><strong>Download NickelOS %s</strong>One ISO for any 64-bit Intel or AMD PC, with Intel, AMD or NVIDIA graphics: install it, or try it from a USB stick first. %s.</p>
        <a class="download-button" href="/download/">Download NickelOS</a>
      </div>
""" % (html.escape(rel["name"]), html.escape(rel["size"])))


def post_date(meta, rel):
    if meta.get("date") == "release":
        return datetime.datetime.strptime(rel["date"], "%d %B %Y").date()
    return datetime.date.fromisoformat(meta["date"])


def post_lines(lines, rel):
    text = "".join(lines)
    for k, v in (rel or {}).items():
        text = text.replace("{{%s}}" % k.upper(), str(v))
    return text.splitlines(keepends=True)


def post_page(date, meta, lines, rel):
    """A post's page, and its body (for the listing and the feed)."""
    body, _ = build_help.convert(lines)
    url = "/blog/%s/" % meta["slug"]
    # The date and the author over the title.
    byline = '      <p class="blog-meta">%s &middot; %s</p>\n' % (long_date(date), AUTHOR)
    page = HEAD.format(title=html.escape(meta["title"] + " · NickelOS blog"),
                       summary=html.escape(meta.get("summary", "")),
                       url=SITE + url, masthead=masthead("Blog"),
                       body=byline + body + "\n" + cta(rel))
    return page, body


def main():
    rel = release()
    posts = []
    for name in sorted(os.listdir(src)) if os.path.isdir(src) else []:
        if not name.endswith(".md"):
            continue
        meta, lines = build_help.read_page(os.path.join(src, name))
        if meta.get("release") == "yes" and not rel:
            continue
        if meta.get("draft") == "yes":
            continue
        posts.append((post_date(meta, rel), meta, post_lines(lines, rel)))
    posts.sort(key=lambda p: p[0], reverse=True)

    shutil.rmtree(out, ignore_errors=True)
    if not posts:
        print("blog: no posts to show")
        return 0
    os.makedirs(out)
    items = []
    media = os.path.join(src, "media")
    if os.path.isdir(media):
        shutil.copytree(media, os.path.join(out, "media"))
    for date, meta, lines in posts:
        page, body = post_page(date, meta, lines, rel)
        url = "/blog/%s/" % meta["slug"]
        os.makedirs(os.path.join(out, meta["slug"]))
        with open(os.path.join(out, meta["slug"], "index.html"), "w", encoding="utf-8") as f:
            f.write(page)
        items.append((date, meta, url, body))

    listing = "\n".join(
        '        <li><p class="blog-meta">%s</p><h2><a href="%s">%s</a></h2><p>%s</p></li>' % (
            long_date(d), u, html.escape(m["title"]), html.escape(m.get("summary", "")))
        for d, m, u, _ in items)
    body = ("      <h1>Blog</h1>\n"
            "      <p>News about NickelOS: releases, what's new in them, and what's being built.</p>\n"
            '      <ul class="blog-list">\n%s\n      </ul>\n'
            '      <p class="small blog-feed">Follow it in a feed reader: <a href="/blog/feed.xml">feed.xml</a> (Atom).</p>\n'
            % listing) + cta(rel)
    with open(os.path.join(out, "index.html"), "w", encoding="utf-8") as f:
        f.write(HEAD.format(title="Blog · NickelOS",
                            summary="News about NickelOS: releases, what's new in them, and what's being built.",
                            url=SITE + "/blog/", masthead=masthead("Blog"), body=body))

    updated = items[0][0].isoformat() + "T00:00:00Z"
    entries = "\n".join("""  <entry>
    <title>%s</title>
    <link href="%s"/>
    <id>%s</id>
    <updated>%sT00:00:00Z</updated>
    <author><name>%s</name></author>
    <summary>%s</summary>
    <content type="html">%s</content>
  </entry>""" % (html.escape(m["title"]), SITE + u, SITE + u, d.isoformat(), AUTHOR,
                 html.escape(m.get("summary", "")),
                 html.escape(b.replace('href="/', 'href="%s/' % SITE)))
        for d, m, u, b in items)
    feed = """<?xml version="1.0" encoding="utf-8"?>
<feed xmlns="http://www.w3.org/2005/Atom">
  <title>NickelOS blog</title>
  <link href="%s/blog/"/>
  <link rel="self" href="%s/blog/feed.xml"/>
  <id>%s/blog/</id>
  <updated>%s</updated>
%s
</feed>
""" % (SITE, SITE, SITE, updated, entries)
    with open(os.path.join(out, "feed.xml"), "w", encoding="utf-8") as f:
        f.write(feed)
    print("blog: %d posts" % len(items))
    return 0


if __name__ == "__main__":
    sys.exit(main())
