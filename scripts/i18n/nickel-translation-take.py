#!/usr/bin/env python3
# Takes translations made from a kit (nickel-translation-kit.py) into this
# overlay's i18n/chrome/<lang>.json, where nickel-translations.py reads them:
#   nickel-translation-take.py KIT_DIR [LANG...]
# KIT_DIR/en.json is the kit's English; KIT_DIR/out/<lang>.json and
# <lang>.*.json are the translations, {name: translation}. Each goes in with
# the English it translates, so that a later change of the English shows the
# translation as out of date. Entries already there for other messages stay.
import glob
import json
import os
import sys

HERE = os.path.dirname(os.path.realpath(__file__))
TRANSLATIONS = os.path.join(HERE, '..', '..', 'i18n', 'chrome')


def main():
  kit = sys.argv[1]
  with open(os.path.join(kit, 'en.json'), encoding='utf-8') as f:
    english = {m['name']: m['en'] for m in json.load(f)}
  langs = sys.argv[2:] or sorted({
      os.path.basename(p).split('.')[0]
      for p in glob.glob(os.path.join(kit, 'out', '*.json'))})
  os.makedirs(TRANSLATIONS, exist_ok=True)
  for lang in langs:
    got = {}
    for path in sorted(glob.glob(os.path.join(kit, 'out', lang + '.json')) +
                       glob.glob(os.path.join(kit, 'out', lang + '.*.json'))):
      with open(path, encoding='utf-8') as f:
        got.update(json.load(f))
    path = os.path.join(TRANSLATIONS, lang + '.json')
    ours = {}
    if os.path.exists(path):
      with open(path, encoding='utf-8') as f:
        ours = json.load(f)
    unknown = 0
    for name, text in got.items():
      if name not in english:
        unknown += 1
        continue
      ours[name] = {'en': english[name], 't': text}
    with open(path, 'w', encoding='utf-8') as f:
      json.dump(dict(sorted(ours.items())), f, ensure_ascii=False, indent=1)
      f.write('\n')
    print('%-7s %d taken, %d in all%s' % (
        lang, len(got) - unknown, len(ours),
        ', %d not in the kit' % unknown if unknown else ''))


if __name__ == '__main__':
  main()
