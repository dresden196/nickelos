#!/usr/bin/env python3
# NickelOS's own wording in Chrome: the messages NickelOS's patches add to
# Chrome's .grd/.grdp files or change there, each with the id its
# translations are filed under (grit's fingerprint of the English) and, for a
# changed one, the id Google's translations of the old English have. Runs in
# the SDK, in the patched Chrome checkout (grit, and git for the originals):
#   cros_sdk -- .../scripts/i18n/nickel-strings.py [OUT.json]
# NICKEL_CHROME_SRC=<checkout>/src for a checkout other than the build's.
# Prints a summary; OUT.json gets every message:
#   {"file", "grd", "xtb": {lang: path}, "name", "status": "new"|"changed",
#    "id", "old_id", "en", "old_en", "desc"}
# "en" is grit's presentable form: placeholders as their NAME in capitals.
import io
import json
import os
import re
import subprocess
import sys

# The checkout nickel-build-stock builds (NICKEL_CHROME_SRC: another one),
# and this overlay's Chrome patches.
SRC = os.environ.get('NICKEL_CHROME_SRC',
                     '/var/cache/chromeos-cache/distfiles/chrome-src/src')
PATCHES = os.path.join(os.path.dirname(os.path.realpath(__file__)), '..', '..',
                       'patches', 'chromeos-base', 'chromeos-chrome')
sys.path.insert(0, os.path.join(SRC, 'tools/grit'))
from grit import grd_reader  # noqa: E402
from grit.node import message as message_node  # noqa: E402

MESSAGE = re.compile(r'<message\s[^>]*?name="([^"]+)"[^>]*>.*?</message>',
                     re.S)


def git_show(path):
  out = subprocess.run(['git', '-c', 'safe.directory=*', '-C', SRC, 'show',
                        'HEAD:' + path], capture_output=True, text=True)
  return out.stdout if out.returncode == 0 else ''


def messages(text):
  return {m.group(1): m.group(0) for m in MESSAGE.finditer(text)}


def grit_info(xml):
  """id, presentable English and description of one <message> element."""
  grd = ('<?xml version="1.0" encoding="UTF-8"?>'
         '<grit latest_public_release="0" current_release="1">'
         '<release seq="1"><messages fallback_to_english="true">%s'
         '</messages></release></grit>' % xml)
  root = grd_reader.Parse(io.StringIO(grd), dir='.', debug=False)
  node = next(n for n in root.Preorder()
              if isinstance(n, message_node.MessageNode))
  msg = node.GetCliques()[0].GetMessage()
  return msg.GetId(), msg.GetPresentableContent(), msg.GetDescription()


def touched_files():
  files = set()
  for name in sorted(os.listdir(PATCHES)):
    with open(os.path.join(PATCHES, name), encoding='utf-8') as f:
      for line in f:
        m = re.match(r'\+\+\+ b/(\S+\.grdp?)$', line.rstrip('\n'))
        if m:
          files.add(m.group(1))
  return sorted(files)


GRD_DIRS = ['chrome/app', 'ash', 'ash/strings', 'ash/login/resources',
            'ui/chromeos', 'chromeos', 'components', 'extensions/strings',
            'chrome/browser/resources/chromeos/accessibility/strings',
            'ui/strings']


def owning_grd(path, seen=()):
  """The .grd that includes a .grdp, through other .grdp files if need be
  (its translations are the grd's)."""
  if path.endswith('.grd'):
    return path
  base = os.path.basename(path)
  for d in GRD_DIRS:
    full = os.path.join(SRC, d)
    if not os.path.isdir(full):
      continue
    for name in sorted(os.listdir(full)):
      includer = os.path.join(d, name)
      if not name.endswith(('.grd', '.grdp')) or includer in seen:
        continue
      with open(os.path.join(full, name), encoding='utf-8') as f:
        if re.search(r'<part file="[^"]*%s"' % re.escape(base), f.read()):
          found = owning_grd(includer, seen + (path,))
          if found:
            return found
  return None


def xtb_files(grd):
  with open(os.path.join(SRC, grd), encoding='utf-8') as f:
    text = f.read()
  out = {}
  for m in re.finditer(r'<file path="([^"]+\.xtb)" lang="([^"]+)"', text):
    out.setdefault(m.group(2),
                   os.path.normpath(os.path.join(os.path.dirname(grd),
                                                 m.group(1))))
  return out


def main():
  result = []
  for path in touched_files():
    with open(os.path.join(SRC, path), encoding='utf-8') as f:
      now = messages(f.read())
    before = messages(git_show(path))
    grd = owning_grd(path)
    xtb = xtb_files(grd) if grd else {}
    for name, xml in now.items():
      if name in before and before[name] == xml:
        continue
      mid, en, desc = grit_info(xml)
      entry = {'file': path, 'grd': grd, 'xtb': xtb, 'name': name,
               'status': 'changed' if name in before else 'new',
               'id': mid, 'en': en, 'desc': desc,
               'old_id': None, 'old_en': None}
      if name in before:
        entry['old_id'], entry['old_en'], _ = grit_info(before[name])
        if entry['old_id'] == mid:
          continue  # only the description or formatting changed
      result.append(entry)
  new = sum(1 for e in result if e['status'] == 'new')
  print('%d messages: %d new, %d changed, in %d files (%d without a grd)' % (
      len(result), new, len(result) - new, len({e['file'] for e in result}),
      len({e['file'] for e in result if not e['grd']})))
  for e in result:
    print('%-7s %-55s %s' % (e['status'], e['name'][:55], e['en'][:70]))
  if len(sys.argv) > 1:
    with open(sys.argv[1], 'w', encoding='utf-8') as f:
      json.dump(result, f, ensure_ascii=False, indent=1)


if __name__ == '__main__':
  main()
