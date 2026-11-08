#!/usr/bin/env python3
# NickelOS's translations of its own wording in Chrome, into Chrome's .xtb
# files, where grit takes every language's strings from. Runs in the SDK, in
# the patched Chrome checkout, after the patches (nickel-chrome.bashrc):
#   nickel-translations.py [--check] [--list FILE]
# (NICKEL_CHROME_SRC=<checkout>/src: as nickel-strings.py.) --list writes the
# .xtb files that get translations, relative to src, one a line, so that the
# hook can restore them before the next patch set.
# The translations: i18n/chrome/<lang>.json in this overlay,
#   {"IDS_NAME": {"en": "<the English>", "t": "<the translation>"}, ...}
# with placeholders written as their names in capitals, as grit's
# presentable form has them ("Enter the password of NAME, ..."). A
# translation goes in only while "en" is still the message's English: when
# the English changes, its id changes, and the old translation is left out
# until it is redone (--check lists those, and the messages with none).
# Chrome's own messages that patches renamed (ChromeOS -> NickelOS) get
# Google's translation of the old English with the same words swapped, when
# the translation has them; a JSON entry for one wins.
import html
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.realpath(__file__))
sys.path.insert(0, HERE)
import importlib.util  # noqa: E402

spec = importlib.util.spec_from_file_location(
    'nickel_strings', os.path.join(HERE, 'nickel-strings.py'))
nickel_strings = importlib.util.module_from_spec(spec)
spec.loader.exec_module(nickel_strings)

SRC = nickel_strings.SRC
TRANSLATIONS = os.path.join(HERE, '..', '..', 'i18n', 'chrome')
TRANSLATION = re.compile(r'<translation id="(\d+)">(.*?)</translation>', re.S)


def placeholder_names(xml):
  """The message's placeholders (its <ph name=...>), longest first."""
  return sorted(set(re.findall(r'<ph name="([^"]+)"', xml)), key=len,
                reverse=True)


def placeholders(names):
  """The message's placeholder NAMEs in a translation, the longest first (so
  that APP_NAME is not taken for NAME), wherever they are: grit's text runs
  them into the words next to them ("BEGIN_LINKLearn moreEND_LINK",
  Japanese "NAMEの")."""
  return re.compile('|'.join(re.escape(n) for n in
                             sorted(names, key=len, reverse=True)))


def to_xtb(text, names):
  """A translation with NAME placeholders, as an .xtb entry's content."""
  out = html.escape(text, quote=False)
  if names:
    out = placeholders(names).sub(
        lambda m: '<ph name="%s"/>' % m.group(0), out)
  return out


def swapped(old_en, new_en, translation):
  """Google's translation of a renamed message with the same words swapped:
  each run of words that differs between the old and the new English, when
  the translation has the old run as it is."""
  old_words, new_words = old_en.split(), new_en.split()
  if len(old_words) != len(new_words):
    return None
  out = translation
  for old, new in zip(old_words, new_words):
    if old == new:
      continue
    core = re.sub(r'^\W+|\W+$', '', old)
    repl = re.sub(r'^\W+|\W+$', '', new)
    if not core or core not in out:
      return None
    out = out.replace(core, repl)
  return out


_xtb_cache = {}


def google_translations(rel):
  """id -> translation of one .xtb file as Google ships it (read once)."""
  if rel not in _xtb_cache:
    path = os.path.join(SRC, rel)
    found = {}
    if os.path.exists(path):
      with open(path, encoding='utf-8') as f:
        found = dict(TRANSLATION.findall(f.read()))
    _xtb_cache[rel] = found
  return _xtb_cache[rel]


def load_json(lang):
  path = os.path.join(TRANSLATIONS, lang + '.json')
  if not os.path.exists(path):
    return {}
  with open(path, encoding='utf-8') as f:
    return json.load(f)


def main():
  check = '--check' in sys.argv
  listing = None
  if '--list' in sys.argv:
    listing = sys.argv[sys.argv.index('--list') + 1]
  touched = set()
  messages = []
  for path in nickel_strings.touched_files():
    with open(os.path.join(SRC, path), encoding='utf-8') as f:
      now = nickel_strings.messages(f.read())
    before = nickel_strings.messages(nickel_strings.git_show(path))
    grd = nickel_strings.owning_grd(path)
    if not grd:
      continue
    xtb = nickel_strings.xtb_files(grd)
    for name, xml in now.items():
      if name in before and before[name] == xml:
        continue
      mid, en, _ = nickel_strings.grit_info(xml)
      old_id = old_en = None
      if name in before:
        old_id, old_en, _ = nickel_strings.grit_info(before[name])
        if old_id == mid:
          continue
      messages.append((name, mid, en, old_id, old_en, xtb,
                       placeholder_names(xml)))

  langs = sorted({lang for m in messages for lang in m[5]})
  written = missing = stale = 0
  for lang in langs:
    if lang in ('en', 'en-US'):
      continue
    ours = load_json(lang)
    by_file = {}
    for name, mid, en, old_id, old_en, xtb, names in messages:
      if lang not in xtb:
        continue
      entry = ours.get(name)
      text = None
      if entry and entry.get('en') == en:
        text = to_xtb(entry['t'], names)
      elif entry:
        stale += 1
        if check:
          print('stale   %-6s %s' % (lang, name))
      if text is None and old_id:
        # Google's translation of the old English, the words swapped.
        found = google_translations(xtb[lang])
        if old_id in found:
          text = swapped(old_en, en, found[old_id])
      if text is None:
        missing += 1
        if check:
          print('missing %-6s %s' % (lang, name))
        continue
      by_file.setdefault(xtb[lang], {})[mid] = text
    for rel, entries in by_file.items():
      path = os.path.join(SRC, rel)
      if os.path.exists(path):
        touched.add(os.path.normpath(rel))
      if not os.path.exists(path) or check:
        written += len(entries)
        continue
      with open(path, encoding='utf-8') as f:
        content = original = f.read()
      for mid, text in entries.items():
        line = '<translation id="%s">%s</translation>' % (mid, text)
        content, n = re.subn(r'<translation id="%s">.*?</translation>' % mid,
                             lambda _: line, content, flags=re.S)
        if not n:
          content = content.replace('</translationbundle>',
                                    line + '\n</translationbundle>', 1)
        written += 1
      if content != original:  # an unchanged file keeps its time
        with open(path, 'w', encoding='utf-8') as f:
          f.write(content)
  if listing:
    with open(listing, 'w', encoding='utf-8') as f:
      f.writelines(rel + '\n' for rel in sorted(touched))
  print('nickel-translations: %d written, %d without a translation, '
        '%d out of date, %d messages, %d languages' % (
            written, missing, stale, len(messages), len(langs)))


if __name__ == '__main__':
  main()
