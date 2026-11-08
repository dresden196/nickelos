#!/usr/bin/env python3
# Runs on the build host: a minimal Chrome DevTools Protocol client (Python's
# standard library only) for UI tests in the test VM, whose Chrome is started
# with --remote-debugging-port and whose port is forwarded to PORT here. Finds
# the open page whose URL starts with URL, runs the JavaScript in JS-FILE
# there (an expression; a promise is awaited) and prints its value as JSON,
# then exceptions and console messages that follow.
#   cdp-eval.py PORT URL JS-FILE
#   cdp-eval.py PORT --open URL
# --open opens URL and waits (60 s at most) until its page is there. For
# chrome:// pages DevTools answers "Could not create new page" but ChromeOS
# opens them anyway, in their app (Settings): no keyboard focus needed, so
# nothing on the screen (ChromeOS's restore prompt after a sign-in) is in
# the way, as it is for Ctrl+N and typing the address.
import base64, json, os, socket, struct, sys, time, urllib.request

if sys.argv[2] == "--open":
    port, url = int(sys.argv[1]), sys.argv[3]
    try:
        urllib.request.urlopen(urllib.request.Request(
            f"http://127.0.0.1:{port}/json/new?{url}", method="PUT")).read()
    except Exception:
        pass
    for _ in range(30):
        tabs = json.load(urllib.request.urlopen(f"http://127.0.0.1:{port}/json/list"))
        if any(t.get("url", "").startswith(url) for t in tabs):
            sys.exit(0)
        time.sleep(2)
    sys.exit(f"TIMEOUT: {url} did not open in 60 s")

port, url, js_file = int(sys.argv[1]), sys.argv[2], sys.argv[3]


class WS:
    def __init__(self, ws_url):
        host_port, path = ws_url[len("ws://"):].split("/", 1)
        host, p = host_port.split(":")
        self.s = socket.create_connection((host, int(p)))
        key = base64.b64encode(os.urandom(16)).decode()
        self.s.sendall((f"GET /{path} HTTP/1.1\r\nHost: {host_port}\r\n"
                        "Upgrade: websocket\r\nConnection: Upgrade\r\n"
                        f"Sec-WebSocket-Key: {key}\r\n"
                        "Sec-WebSocket-Version: 13\r\n\r\n").encode())
        buf = b""
        while b"\r\n\r\n" not in buf:
            buf += self.s.recv(4096)
        self.rest = buf.split(b"\r\n\r\n", 1)[1]
        self.n = 0

    def send(self, method, params=None):
        self.n += 1
        data = json.dumps({"id": self.n, "method": method,
                           "params": params or {}}).encode()
        hdr = bytearray([0x81])
        ln = len(data)
        if ln < 126:
            hdr.append(0x80 | ln)
        elif ln < 65536:
            hdr.append(0x80 | 126); hdr += struct.pack(">H", ln)
        else:
            hdr.append(0x80 | 127); hdr += struct.pack(">Q", ln)
        mask = os.urandom(4)
        hdr += mask
        self.s.sendall(bytes(hdr) + bytes(b ^ mask[i % 4] for i, b in enumerate(data)))
        return self.n

    def _read(self, k):
        while len(self.rest) < k:
            chunk = self.s.recv(65536)
            if not chunk:
                raise EOFError
            self.rest += chunk
        out, self.rest = self.rest[:k], self.rest[k:]
        return out

    def recv(self):
        b0, b1 = self._read(2)
        ln = b1 & 0x7F
        if ln == 126:
            ln = struct.unpack(">H", self._read(2))[0]
        elif ln == 127:
            ln = struct.unpack(">Q", self._read(8))[0]
        return json.loads(self._read(ln))

    def call(self, method, params=None, timeout=20):
        i = self.send(method, params)
        end = time.time() + timeout
        while time.time() < end:
            m = self.recv()
            if m.get("id") == i:
                return m
            report(m)
        raise TimeoutError(method)


def report(m):
    meth = m.get("method")
    if meth == "Runtime.exceptionThrown":
        d = m["params"]["exceptionDetails"]
        print("EXCEPTION:", d.get("exception", {}).get("description") or d.get("text"))
    elif meth == "Runtime.consoleAPICalled":
        print("console:", " ".join(str(a.get("value", a.get("description")))
                                   for a in m["params"]["args"]))


# A page the test just opened can take a while to show up (Settings after a
# sign-in): wait for chrome:// pages, open others.
target = None
for _ in range(30 if url.startswith("chrome://") else 1):
    tabs = json.load(urllib.request.urlopen(f"http://127.0.0.1:{port}/json/list"))
    target = next((t for t in tabs if t.get("url", "").startswith(url)), None)
    if target:
        break
    time.sleep(2)
if not target and url.startswith("chrome://"):
    sys.exit(f"TIMEOUT: no page at {url} after 60 s")
if not target:
    target = json.load(urllib.request.urlopen(
        urllib.request.Request(f"http://127.0.0.1:{port}/json/new?{url}", method="PUT")))
    time.sleep(6)
ws = WS(target["webSocketDebuggerUrl"])
ws.call("Runtime.enable")
r = ws.call("Runtime.evaluate", {"expression": open(js_file).read(),
                                  "awaitPromise": True, "returnByValue": True},
            timeout=60)
res = r.get("result", {})
if "exceptionDetails" in res:
    print("EXCEPTION:", res["exceptionDetails"].get("exception", {}).get("description"))
else:
    print("result:", json.dumps(res.get("result", {}).get("value"), indent=1))
end = time.time() + 2
ws.s.settimeout(0.5)
while time.time() < end:
    try:
        report(ws.recv())
    except (socket.timeout, EOFError):
        break
