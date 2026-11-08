#!/usr/bin/env python3
# Builds the NickelOS guide (nickelos.dev/help) from site/help-src/*.md into
# site/help/: help/index.html for index.md, help/<name>/index.html for the
# rest, with the site's header and footer and a list of the guide's pages.
#
# The pages are a small Markdown subset, so the site needs nothing installed:
#   first lines "key: value" up to a line "---" (title, order, summary)
#   # Title / ## Section / ### Part   (an id: "## Section {#id}")
#   paragraphs, "- " and "1. " lists, ``` fenced code, | tables |,
#   "> " notes, `code`, **bold**, *emphasis*, [text](link)
import html
import os
import re
import sys

here = os.path.dirname(os.path.abspath(__file__))
src = os.path.join(here, "help-src")
out = os.path.join(here, "help")


def slug(text):
    s = re.sub(r"[^a-z0-9]+", "-", text.lower()).strip("-")
    return s or "section"


def inline(text):
    # Code spans first, kept out of the way while links and emphasis are
    # found (bold may hold code: **`/tmp` is noexec**).
    codes = []

    def keep(m):
        codes.append("<code>%s</code>" % html.escape(m.group(1)))
        return "\x00%d\x00" % (len(codes) - 1)

    t = re.sub(r"`([^`]+)`", keep, text)
    t = html.escape(t, quote=False)
    t = re.sub(r"\[([^\]]+)\]\(([^)\s]+)\)",
               lambda m: '<a href="%s">%s</a>' % (html.escape(m.group(2)), m.group(1)), t)
    t = re.sub(r"\*\*(.+?)\*\*", r"<strong>\1</strong>", t)
    t = re.sub(r"(?<![\w*])\*([^*\s][^*]*)\*(?![\w*])", r"<em>\1</em>", t)
    return re.sub(r"\x00(\d+)\x00", lambda m: codes[int(m.group(1))], t)


def convert(lines):
    body, toc = [], []
    i = 0
    para = []

    def flush():
        if para:
            body.append("<p>%s</p>" % inline(" ".join(para)))
            para.clear()

    while i < len(lines):
        line = lines[i].rstrip("\n")
        stripped = line.strip()
        if not stripped:
            flush()
            i += 1
            continue
        if stripped.startswith("```"):
            flush()
            code = []
            i += 1
            while i < len(lines) and not lines[i].strip().startswith("```"):
                code.append(lines[i].rstrip("\n"))
                i += 1
            i += 1
            body.append("<pre><code>%s</code></pre>" % html.escape("\n".join(code)))
            continue
        m = re.match(r"^(#{1,3}) (.+?)(?: \{#([a-z0-9-]+)\})?$", stripped)
        if m:
            flush()
            level = len(m.group(1))
            text = m.group(2)
            ident = m.group(3) or slug(text)
            if level == 1:
                body.append("<h1>%s</h1>" % inline(text))
            else:
                body.append('<h%d id="%s">%s</h%d>' % (level, ident, inline(text), level))
                if level == 2:
                    toc.append((ident, text))
            i += 1
            continue
        if stripped.startswith("|"):
            flush()
            rows = []
            while i < len(lines) and lines[i].strip().startswith("|"):
                rows.append([c.strip() for c in lines[i].strip().strip("|").split("|")])
                i += 1
            head, rest = rows[0], [r for r in rows[1:] if not all(re.fullmatch(r":?-+:?", c) for c in r)]
            t = ["<div class=\"table\"><table><thead><tr>"]
            t += ["<th>%s</th>" % inline(c) for c in head]
            t.append("</tr></thead><tbody>")
            for r in rest:
                t.append("<tr>%s</tr>" % "".join("<td>%s</td>" % inline(c) for c in r))
            t.append("</tbody></table></div>")
            body.append("".join(t))
            continue
        if stripped.startswith("> "):
            flush()
            note = []
            while i < len(lines) and lines[i].strip().startswith(">"):
                note.append(lines[i].strip()[1:].strip())
                i += 1
            body.append('<aside class="note"><p>%s</p></aside>' % inline(" ".join(note)))
            continue
        lm = re.match(r"^(- |\d+\. )", stripped)
        if lm:
            flush()
            ordered = lm.group(1) != "- "
            items = []
            while i < len(lines):
                s = lines[i].strip()
                m2 = re.match(r"^(- |\d+\. )(.*)$", s)
                if m2 and (m2.group(1) == "- ") != ordered:
                    items.append(m2.group(2))
                    i += 1
                elif s and items and not re.match(r"^(- |\d+\. |#|```|\||> )", s) and lines[i].startswith("  "):
                    items[-1] += " " + s
                    i += 1
                else:
                    break
            tag = "ol" if ordered else "ul"
            body.append("<%s>%s</%s>" % (tag, "".join("<li>%s</li>" % inline(t) for t in items), tag))
            continue
        para.append(stripped)
        i += 1
    flush()
    return "\n".join(body), toc


def read_page(path):
    with open(path, encoding="utf-8") as f:
        lines = f.readlines()
    meta = {}
    for n, line in enumerate(lines):
        if line.strip() == "---":
            lines = lines[n + 1:]
            break
        k, _, v = line.partition(":")
        meta[k.strip()] = v.strip()
    return meta, lines


LAYOUT = """<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>{title}</title>
<meta name="description" content="{summary}">
<meta name="color-scheme" content="light dark">
<meta name="theme-color" content="#f3f1ec" media="(prefers-color-scheme: light)">
<meta name="theme-color" content="#1c1b18" media="(prefers-color-scheme: dark)">
<link rel="icon" href="/favicon.png?v=3" type="image/png" sizes="64x64">
<link rel="icon" href="/favicon.ico?v=3" sizes="16x16 32x32 48x48">
<link rel="apple-touch-icon" href="/apple-touch-icon.png?v=3">
<link rel="preload" href="/assets/fonts/GoogleSansFlex-nickelos.woff2" as="font" type="font/woff2" crossorigin>
<link rel="stylesheet" href="/assets/css/site.css">
</head>
<body>

<a class="skip" href="#main">Skip to the content</a>

<header class="masthead">
  <div class="wrap masthead-row">
    <a class="brand" href="/" aria-label="NickelOS home">
      <img src="/assets/img/nickelos-logo-v3.png" alt="" width="32" height="32">
      <span>NickelOS</span>
    </a>
    <nav aria-label="Site">
      <a href="/help/">Guide</a>
      <a href="/help/troubleshooting/">Troubleshooting</a>
      <a href="/help/community/">Get help</a>
    </nav>
    <span class="chip" title="NickelOS has not been released yet">Alpha</span>
  </div>
</header>

<main id="main" class="guide">
  <div class="wrap guide-layout">
    <nav class="guide-nav" aria-label="Guide">
      <p class="eyebrow">Guide</p>
      <ul>
{nav}
      </ul>
    </nav>
    <article class="guide-page">
{toc}{body}
    </article>
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


# The copy of the guide that comes with NickelOS (chromeos-base/nickel-guide,
# Settings > Help): the same pages as plain files, index.html and
# <page>.html, with relative links and the stylesheet, fonts and logo beside
# them, so they open from disk in the browser and work offline. Links that
# leave the guide go to nickelos.dev.
#   build-help.py --offline DIR
SITE = "https://nickelos.dev"
OFFLINE_NOTE = ('      <p>This copy of the guide came with NickelOS and works offline. '
                'The newest is at <a href="%s/help/">nickelos.dev/help</a>.</p>\n' % SITE)


def offline_links(page):
    def help_link(m):
        key, frag = m.group(1), m.group(2) or ""
        return 'href="%s%s"' % ((key + ".html") if key else "index.html", frag)
    page = re.sub(r'href="/help/(?:([a-z0-9-]+)/)?(#[^"]*)?"', help_link, page)
    page = re.sub(r'(href|src)="/assets/', r'\1="assets/', page)
    page = re.sub(r'<link rel="(?:icon|apple-touch-icon)"[^>]*>\n', "", page)
    page = re.sub(r'href="/([^"]*)"', lambda m: 'href="%s/%s"' % (SITE, m.group(1)), page)
    return page.replace('<div class="small footer-notes">\n',
                        '<div class="small footer-notes">\n' + OFFLINE_NOTE, 1)


def write_offline(dest, rendered):
    import shutil
    if os.path.isdir(dest):
        shutil.rmtree(dest)
    for sub in ("css", "fonts", "img"):
        os.makedirs(os.path.join(dest, "assets", sub))
    for key, page in rendered:
        name = "index.html" if key == "index" else key + ".html"
        with open(os.path.join(dest, name), "w", encoding="utf-8") as f:
            f.write(offline_links(page))
    with open(os.path.join(here, "assets", "css", "site.css"), encoding="utf-8") as f:
        css = f.read().replace('url("/assets/fonts/', 'url("../fonts/')
    with open(os.path.join(dest, "assets", "css", "site.css"), "w", encoding="utf-8") as f:
        f.write(css)
    for rel in ("fonts/GoogleSansFlex-nickelos.woff2", "fonts/GoogleSansCode-nickelos.woff2",
                "fonts/OFL.txt", "img/nickelos-logo-v3.png"):
        shutil.copy(os.path.join(here, "assets", rel), os.path.join(dest, "assets", rel))
    print("guide, offline: %d pages in %s" % (len(rendered), dest))


def main():
    offline_dir = sys.argv[2] if len(sys.argv) == 3 and sys.argv[1] == "--offline" else None
    rendered = []
    pages = []
    for name in sorted(os.listdir(src)):
        if not name.endswith(".md"):
            continue
        meta, lines = read_page(os.path.join(src, name))
        key = name[:-3]
        pages.append((int(meta.get("order", "99")), key, meta, lines))
    pages.sort()
    for _, key, meta, lines in pages:
        url = "/help/" if key == "index" else "/help/%s/" % key
        meta["url"] = url
    for _, key, meta, lines in pages:
        body, toc = convert(lines)
        nav = "\n".join(
            '        <li><a href="%s"%s>%s</a></li>' % (
                m["url"], ' aria-current="page"' if k == key else "",
                html.escape(m.get("nav", m["title"])))
            for _, k, m, _ in pages)
        toc_html = ""
        if len(toc) > 2:
            toc_html = ('      <nav class="toc" aria-label="On this page">%s</nav>\n' %
                        "".join('<a href="#%s">%s</a>' % (i, inline(t)) for i, t in toc))
        # The page's own <h1> comes first, then its table of contents.
        # After the paragraph that follows it, if one does.
        h1_end = body.find("</h1>")
        if h1_end != -1 and toc_html:
            cut = h1_end + 5
            if body.startswith("\n<p>", cut):
                cut = body.find("</p>", cut) + 4
            body = body[:cut] + "\n" + toc_html + body[cut:]
            toc_html = ""
        title = meta["title"] if key == "index" else meta["title"] + " · NickelOS guide"
        page = LAYOUT.format(title=html.escape(title),
                             summary=html.escape(meta.get("summary", "")),
                             nav=nav, toc=toc_html, body=body)
        dest = out if key == "index" else os.path.join(out, key)
        os.makedirs(dest, exist_ok=True)
        with open(os.path.join(dest, "index.html"), "w", encoding="utf-8") as f:
            f.write(page)
        rendered.append((key, page))
    print("guide: %d pages" % len(pages))
    if offline_dir:
        write_offline(offline_dir, rendered)


if __name__ == "__main__":
    sys.exit(main())
