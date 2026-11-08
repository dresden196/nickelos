# nickelos.dev: design brief (2026-10-11)

Dresden asked for a site that doesn't look generated, leaning towards a
Material, Google-like feel. Looked at first: chromeos.google (and Flex),
ubuntu.com, canonical.com, redhat.com, elementary.io, fedoraproject.org,
kde.org, asahilinux.org, system76.com/pop.

What each taught:

- ChromeOS: huge, friendly type with one word in a tinted pill; rounded,
  soft containers; lots of air. All marketing, no facts.
- Ubuntu / Canonical: a strict grid of hairline-ruled rows; light display
  type; square corners; dense and serious.
- elementary, KDE: the real OS on screen is the hero.
- Asahi: the install command is the call to action; plain and honest.
- Red Hat, Fedora: enterprise cards, logo walls; avoid.

The idea: **Chrome is cold, nickel is warm.** Nickel plating is a warmer,
slightly yellow silver next to chrome's blue-white. The site keeps
ChromeOS's friendly shapes and type (it is set in Google Sans Flex, the
typeface NickelOS itself uses, OFL) but plates them in warm silvers and
graphite instead of Google's white and primaries. The logo's blue-to-teal
is the only saturated colour.

One signature: the brush streak (`branding/stroke/brush.py`), drawn once
in the hero, passing behind the screenshot and breaking out of its frame
at the top right (Project Crowbar, prying out). Nowhere else. Dresden
likes it (2026-10-12). It is a stroke, not a logo.

Honesty is the style:

- real screenshots of NickelOS (taken as the tester account, but never
  showing its name: a plain `~ $` prompt and tab title in a terminal, no
  build numbers, no kernel strings, no list of our own installed apps),
  with plain captions;
- a ledger of what changes (hairline rows, Ubuntu-like);
- the real stack, layer by layer, with where each part comes from;
- a status board: works today, being built, not yet;
- no download button until there is a release.

Avoid: gradient blobs, glass cards, rows of icon cards, "seamless",
"unleash", testimonials, fake numbers, purple, stock photos.

No JavaScript, nothing from other hosts: fonts, images and styles are all
served from nickelos.dev. No analytics.

Dresden's review (2026-10-11), now rules:

- The logo is Dresden's (blue disc, calligraphic N), from his 2026-10-29
  sheets, everywhere: the header, the footer, the favicons, and anything
  shown in a screenshot (a printed test page once carried a logo drawn
  here; that logo is deleted). It's his flat version
  (`branding/logo/nickelos-logo-flat.svg`), the one logo the site and the
  OS both use (his call, 2026-11-02; the wave version isn't used). The
  logo files are named `-v3` (the favicons `?v=3`) so no browser shows an
  old cached copy.
- Never show the test machines' names or specs, or anything else internal
  (no "tested on X"). Say "NVIDIA, AMD and Intel GPUs supported".
- "Built on the latest ChromiumOS": NickelOS tracks ChromiumOS releases a
  few weeks behind, so no version numbers.
- Plain words in Dresden's voice, not polished slogans. His intro and his
  "What changes" line are on the page as he wrote them (typos fixed).
- The Google-related facts live in "De-Googled by default".

Dresden again (2026-10-29), also rules:

- No inside information: not the machines we test on, not that the project
  went through several builds or designs while it was being made, nothing
  about the back end (build hosts, checks, process), nothing about the
  logos. Keep it to NickelOS, what it changes in ChromiumOS, and the
  commands people use.
- The front page says each thing once. Don't let sections repeat each other
  (the old page said "no Crostini", the GPU line and "updates leave your
  apps alone" three or four times).
- Written the way a developer on the project would write it, not like
  generated marketing copy. Have someone critique a larger rewrite before
  it goes up.

Wallpaper in the screenshots: `branding/wallpaper/`.
