#!/usr/bin/env python3
# The material for translating NickelOS's wording in Chrome: what
# nickel-translations.py has no translation for yet, per language, with what
# helps a translator write it as ChromeOS already speaks that language. Runs
# in the SDK, in the patched Chrome checkout (as nickel-strings.py):
#   nickel-translation-kit.py OUT_DIR
# OUT_DIR/en.json: the messages, [{"name", "en", "desc", "ph"}] (one file for
#   every language: the English is the same; "ph", its placeholders).
# OUT_DIR/<lang>.json: {"todo": [name, ...] (the messages it lacks),
#   "old": {name: {"en": <the English Google translated>, "t": <Google's
#   translation>}} for the messages a patch changed (the new one is mostly
#   that with a word or two different), "glossary": {English term: Google's
#   translation in this language}}.
# Placeholders are their NAME in capitals everywhere, as in the JSON files the
# translations go into (i18n/chrome/<lang>.json).
import functools
import html
import importlib.util
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.realpath(__file__))


def load(name, file):
  spec = importlib.util.spec_from_file_location(name, os.path.join(HERE, file))
  module = importlib.util.module_from_spec(spec)
  spec.loader.exec_module(module)
  return module


nickel_strings = load('nickel_strings', 'nickel-strings.py')
translations = load('nickel_translations', 'nickel-translations.py')
SRC = nickel_strings.SRC
xtb_files = functools.lru_cache(maxsize=None)(nickel_strings.xtb_files)

# Terms whose ChromeOS translation a translator should reuse, from the files
# that hold most of ChromeOS's own wording.
GLOSSARY_GRDS = ['chrome/app/generated_resources.grd',
                 'chromeos/chromeos_strings.grd', 'ash/ash_strings.grd',
                 'ui/chromeos/ui_chromeos_strings.grd']
TERMS = [
    'Settings', 'Apps', 'App', 'Linux apps', 'Linux', 'Android apps',
    'Files', 'My files', 'Downloads', 'Device', 'Devices', 'Account',
    'Accounts', 'Sign in', 'Sign out', 'Password', 'Passwords', 'Lock',
    'Lock screen', 'Shelf', 'Launcher', 'Terminal', 'Screen saver',
    'Wallpaper', 'Default apps', 'Email', 'Documents', 'Guest', 'Owner',
    'Administrator', 'Network', 'Wi-Fi', 'Bluetooth', 'Keyboard', 'Language',
    'Languages', 'Accessibility', 'Privacy and security', 'Personalization',
    'Update', 'Updates', 'Restart', 'Install', 'Uninstall', 'Remove', 'Open',
    'Cancel', 'Next', 'Back', 'Done', 'Continue', 'Get started', 'Close',
    'Turn on', 'Turn off', 'Learn more', 'Browser', 'Developers',
    'Device name', 'Computer name', 'Notifications', 'Storage', 'Display',
    'Power', 'Battery', 'Sound', 'Microphone', 'Camera', 'Printers',
    'Screenshot', 'Screen capture', 'Clipboard', 'Search', 'Desk', 'Desks',
    'Window', 'Windows', 'Tablet mode', 'Quick Settings', 'Status area',
    'Do not disturb', 'Night Light', 'Overview', 'Welcome', 'Setup',
    'Restore', 'Recover', 'Reset', 'Powerwash', 'Channel', 'Version',
    'Folder', 'Pictures', 'Photos', 'Album', 'Slideshow', 'Theme', 'Dark',
    'Light', 'Auto', 'Gaming', 'Game controller', 'Steam', 'Wine',
]


def plain(xml):
  """A <message>'s text without its tags (good enough to spot one-term
  messages)."""
  body = re.sub(r'^<message[^>]*>|</message>$', '', xml.strip(), flags=re.S)
  return re.sub(r'\s+', ' ', re.sub(r'<[^>]+>', '', body)).strip()


def files_of(grd):
  """A .grd and every .grdp it takes parts from, relative to src."""
  out, todo = [], [grd]
  while todo:
    f = todo.pop()
    out.append(f)
    path = os.path.join(SRC, f)
    if not os.path.exists(path):
      continue
    with open(path, encoding='utf-8') as fh:
      text = fh.read()
    for m in re.finditer(r'<part file="([^"]+)"', text):
      todo.append(os.path.normpath(os.path.join(os.path.dirname(f),
                                                m.group(1))))
  return out


def names_from_ph(text):
  """An .xtb entry back in the NAME form."""
  return html.unescape(re.sub(r'<ph name="([^"]+)"\s*/>', r'\1', text))


def main():
  out_dir = sys.argv[1]
  os.makedirs(out_dir, exist_ok=True)

  # The messages, as nickel-translations.py sees them.
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
      mid, en, desc = nickel_strings.grit_info(xml)
      old_id = old_en = None
      if name in before:
        old_id, old_en, _ = nickel_strings.grit_info(before[name])
        if old_id == mid:
          continue
      messages.append(dict(name=name, id=mid, en=en, desc=desc,
                           old_id=old_id, old_en=old_en, xtb=xtb,
                           ph=translations.placeholder_names(xml)))

  # Glossary terms: their ids, from the main .grd files.
  terms = set(TERMS)
  term_ids = {}  # term -> [(grd, id)]
  for grd in GLOSSARY_GRDS:
    for f in files_of(grd):
      path = os.path.join(SRC, f)
      if not os.path.exists(path):
        continue
      with open(path, encoding='utf-8') as fh:
        found = nickel_strings.messages(fh.read())
      for name, xml in found.items():
        text = plain(xml)
        if text in terms and 'translateable="false"' not in xml:
          mid, en, _ = nickel_strings.grit_info(xml)
          term_ids.setdefault(en, []).append((grd, mid))

  with open(os.path.join(out_dir, 'en.json'), 'w', encoding='utf-8') as f:
    json.dump([{'name': m['name'], 'en': m['en'], 'desc': m['desc'],
                'ph': m['ph']} for m in messages], f, ensure_ascii=False,
              indent=1)

  langs = sorted({lang for m in messages for lang in m['xtb']})
  for lang in langs:
    if lang in ('en', 'en-US'):
      continue
    ours = translations.load_json(lang)
    todo, old = [], {}
    for m in messages:
      if lang not in m['xtb']:
        continue
      entry = ours.get(m['name'])
      if entry and entry.get('en') == m['en']:
        continue
      google = translations.google_translations(m['xtb'][lang])
      if m['old_id'] and m['old_id'] in google:
        old_t = names_from_ph(google[m['old_id']])
        if translations.swapped(m['old_en'], m['en'],
                                google[m['old_id']]) is not None:
          continue  # nickel-translations.py swaps the words itself
        old[m['name']] = {'en': m['old_en'], 't': old_t}
      todo.append(m['name'])
    glossary = {}
    for term, places in sorted(term_ids.items()):
      for grd, mid in places:
        rel = xtb_files(grd).get(lang)
        if rel:
          t = translations.google_translations(rel).get(mid)
          if t:
            glossary[term] = names_from_ph(t)
            break
    with open(os.path.join(out_dir, lang + '.json'), 'w',
              encoding='utf-8') as f:
      json.dump({'todo': todo, 'old': old, 'glossary': glossary}, f,
                ensure_ascii=False, indent=1)
    print('%-7s %3d to translate (%3d changed from Google\'s), %3d terms' % (
        lang, len(todo), len(old), len(glossary)))


if __name__ == '__main__':
  main()
