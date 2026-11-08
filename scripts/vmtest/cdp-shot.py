#!/usr/bin/env python3
# Runs on the build host: a screenshot of what the page whose URL starts with
# URL renders (DevTools Page.captureScreenshot), for when the VM's QMP
# screenshots lag behind (the OOBE dialog, for one). PORT is the VM's
# --remote-debugging-port forwarded here, as for cdp-eval.py.
#   cdp-shot.py PORT URL OUT.png
import base64, json, os, sys, urllib.request

here = os.path.dirname(os.path.abspath(__file__))
port, url, out = int(sys.argv[1]), sys.argv[2], sys.argv[3]
# cdp-eval.py's websocket client, without running its main part.
src = open(os.path.join(here, "cdp-eval.py")).read().split("\ntabs = ")[0]
sys.argv = [sys.argv[0], str(port), url, "/dev/null"]
exec(src)

tabs = json.load(urllib.request.urlopen(f"http://127.0.0.1:{port}/json/list"))
target = next((t for t in tabs if t.get("url", "").startswith(url)), None)
if not target:
    sys.exit(f"no page at {url}")
ws = WS(target["webSocketDebuggerUrl"])
r = ws.call("Page.captureScreenshot", {"format": "png"}, timeout=30)
with open(out, "wb") as f:
    f.write(base64.b64decode(r["result"]["data"]))
print(out)
