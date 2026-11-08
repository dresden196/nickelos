#!/usr/bin/env python3
# Runs on the build host: prints Ash's window and view tree from Chrome's UI
# DevTools (chrome started with --enable-ui-devtools=PORT, forwarded here),
# one node per line with its attributes, for debugging Ash UI that DevTools
# pages cannot see. With FILTER, only the subtrees of nodes whose line
# contains it, with each node's properties (bounds, visibility...).
#   ui-tree.py PORT [FILTER]
import json, os, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import importlib.util
spec = importlib.util.spec_from_file_location(
    "cdp", os.path.join(os.path.dirname(os.path.abspath(__file__)), "cdp-eval.py"))

port = int(sys.argv[1])
filt = sys.argv[2] if len(sys.argv) > 2 else None

# cdp-eval.py runs on import; take only its WebSocket class.
src = open(spec.origin).read()
ns = {}
exec(src[:src.index("\nport, url, js_file")], ns)
exec(src[src.index("class WS:"):src.index("\n\n\n", src.index("class WS:"))], ns)
ws = ns["WS"](f"ws://127.0.0.1:{port}/0")


def call(method, params=None):
    n = ws.send(method, params)
    while True:
        msg = ws.recv()
        if msg.get("id") == n:
            return msg.get("result", msg.get("error"))


call("DOM.enable")
call("CSS.enable")
root = call("DOM.getDocument", {"depth": -1})["root"]


def line(node):
    a = node.get("attributes", [])
    attrs = " ".join(f"{a[i]}={a[i + 1]}" for i in range(0, len(a) - 1, 2))
    return f"{node.get('nodeName')} {attrs}"


def walk(node, depth, show):
    text = line(node)
    show = show or (filt is None) or (filt in text)
    if show:
        print("  " * depth + text)
        if filt is not None:
            styles = call("CSS.getMatchedStylesForNode",
                          {"nodeId": node["nodeId"]}) or {}
            props = [q for rule in styles.get("matchedCSSRules", [])
                     for q in rule["rule"]["style"].get("cssProperties", [])]
            if props:
                print("  " * depth + "  [" + ", ".join(
                    f"{q['name']}={q['value']}" for q in props) + "]")
    for child in node.get("children", []):
        walk(child, depth + 1, show)


walk(root, 0, False)
