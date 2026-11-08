#!/usr/bin/env python3
# Assembles ChromeOS's Japanese input method extension (the directory
# Chrome's mozc_manifest.json names, input_methods/nacl_mozc) from Mozc's
# NaCl extension (google/mozc src/chrome/nacl at afb03ddf, BSD-3-Clause),
# NickelOS's starter (nickel-mozc-init.js) and the engine in WebAssembly
# (nickel-mozc.js, nickel-mozc.wasm).
#   assemble.py MOZC_NACL_DIR ENGINE_DIR OUT_DIR
import json, os, re, shutil, sys
src, engine, out = sys.argv[1:4]
here = os.path.dirname(os.path.abspath(__file__))
os.makedirs(out, exist_ok=True)
for f in ["nacl_mozc.js", "key_translator.js", "option_page.js",
          "dictionary_tool.js", "options.html", "options.css", "options.js"]:
    shutil.copy(os.path.join(src, f), out)
shutil.copy(os.path.join(here, "nickel-mozc-init.js"), out)
for f in ["nickel-mozc.js", "nickel-mozc.wasm"]:
    shutil.copy(os.path.join(engine, f), out)
# The page: the engine and its starter instead of the NaCl one.
page = open(os.path.join(src, "nacl_mozc.html")).read()
page = page.replace(
    '<script type="text/javascript" src="nacl_mozc_init.js"></script>',
    '<script type="text/javascript" src="nickel-mozc.js"></script>\n'
    '<script type="text/javascript" src="nickel-mozc-init.js"></script>')
page = page.replace("Chrome NaCl Mozc exntension", "Mozc")
open(os.path.join(out, "nacl_mozc.html"), "w").write(page)
version = open(os.path.join(src, "nacl_mozc_version_template.js")).read()
for key, value in {"MAJOR": "2", "MINOR": "0", "BUILD": "0", "REVISION": "0",
                   "BRANDING": "Mozc", "DEV_CHANNEL": "false",
                   "OFFICIAL_BUILD": "false"}.items():
    version = version.replace("@%s@" % key, value)
open(os.path.join(out, "nacl_mozc_version.js"), "w").write(version)
# Messages: the templates without their comment header, plus the names of
# the two input methods that Chrome's manifest uses.
names = {
    "en": ("Japanese", "Japanese for JIS keyboard"),
    "ja": ("日本語入力 (US キーボード)", "日本語入力 (JIS キーボード)"),
}
for lang in ("en", "ja"):
    text = open(os.path.join(src, "_locales", lang, "messages_template.json"),
                encoding="utf-8").read()
    text = "\n".join(l for l in text.split("\n") if not l.startswith("//"))
    text = text.replace("@APP_NAME_EN@", "Mozc").replace("@BRANDING@", "Mozc")
    messages = json.loads(text)
    messages["inputmethod_mozc_us"] = {"message": names[lang][0]}
    messages["inputmethod_mozc_jp"] = {"message": names[lang][1]}
    os.makedirs(os.path.join(out, "_locales", lang), exist_ok=True)
    json.dump(messages, open(os.path.join(out, "_locales", lang,
                                          "messages.json"), "w",
                             encoding="utf-8"), ensure_ascii=False, indent=1)
print("assembled", out)
