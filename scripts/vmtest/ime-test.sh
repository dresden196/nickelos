#!/bin/bash
# Runs on nickel-build, signed in as tester in the test VM (port 9222).
# Typing and middle-click paste in Linux apps (docs/vision.md), at the
# protocol level (src/wl-ime-test.c). --build-only builds and installs the
# test tools and stops.
#   1. text-input-v3: wl-ime-test's window takes the focus; with ChromeOS's
#      Pinyin input method, typing "nihao" and space commits 你好 to it
#      (preedit while composing, done after each batch). The input method is
#      picked through Settings' own API (chrome.languageSettingsPrivate and
#      chrome.inputMethodPrivate), over Chrome's remote debugging, which this
#      turns on in /etc/chrome_dev.conf (the UI restarts and tester signs in
#      again once).
#   2. primary selection: one wl-ime-test selects a text, a second one,
#      focused after it, is offered it and reads it.
# Also installs nickel-vmouse (src/), a relative mouse for the middle and
# right buttons: Chrome takes QEMU's tablet for a touchscreen, which has
# neither. In a GTK app (checked with Bazaar, with GTK_IM_MODULE=wayland from
# the session): `wl-ime-test primary-set TEXT 20 &`, then `nickel-vmouse home
# rel DX DY left middle` on a text field pastes TEXT; a double-clicked word
# there is what `wl-ime-test primary-get 6` reads. Each 4-count step moves
# the pointer 3.2 px (WAYLAND_DEBUG=client on the app shows where it is).
# Needs wayland-scanner on the build host (Ubuntu's libwayland-bin).
set -u
vm() { nickel-vmssh "$@" </dev/null; }
SRC=/srv/nickel/src
B=/build/nickel
GEN=$SRC/.nickel/wl-ime-gen
XML=$SRC/out/build/nickel/usr/share/wayland-protocols
PROTOCOLS="stable/xdg-shell/xdg-shell.xml
  unstable/text-input/text-input-unstable-v3.xml
  unstable/primary-selection/primary-selection-unstable-v1.xml"
PORT=9229

echo "== build the test client"
install -d -o nickel "$GEN"
for x in $PROTOCOLS; do
  b=$(basename "$x" .xml)
  wayland-scanner client-header "$XML/$x" "$GEN/$b-client.h" &&
    wayland-scanner private-code "$XML/$x" "$GEN/$b.c" || exit 1
done
chown -R nickel "$GEN"
install -o nickel -m644 "$(dirname "$0")"/src/wl-ime-test.c "$SRC/.nickel/"
# One line per command: cros_sdk turns the script's newlines into spaces.
# No packed relocations (DT_RELR): Flatpak runtimes' glibc refuses them, and
# files-bridge-test.sh runs the client in a Flatpak sandbox too.
(cd "$SRC" && sudo -u nickel -H "$SRC/chromite/bin/cros_sdk" -- bash -c "
  cd /mnt/host/source/.nickel &&
  export PKG_CONFIG_SYSROOT_DIR=$B PKG_CONFIG_LIBDIR=$B/usr/lib64/pkgconfig:$B/usr/share/pkgconfig &&
  x86_64-cros-linux-gnu-clang --sysroot=$B -O2 -Wall -Iwl-ime-gen -o wl-ime-test wl-ime-test.c wl-ime-gen/*.c \$(pkg-config --cflags --libs wayland-client) -Wl,-z,nopack-relative-relocs -Wl,--pack-dyn-relocs=none") || exit 1
"$(dirname "$0")"/nickel-vmpush /usr/local/bin/wl-ime-test \
  < "$SRC/.nickel/wl-ime-test"
"$(dirname "$0")"/nickel-vmpush /usr/local/bin/nickel-vmouse \
  < "$(dirname "$0")"/src/nickel-vmouse

# Picks an input method through Settings' own API, in a Settings tab opened
# over Chrome's remote debugging: nickel-ime-select WORD (the first input
# method whose id contains WORD). A minimal WebSocket client: the test image
# has no websocket module.
"$(dirname "$0")"/nickel-vmpush /usr/local/bin/nickel-ime-select <<'EOF'
#!/usr/bin/env python3
import base64, json, os, socket, struct, sys, time, urllib.request
PORT = 9229
word = sys.argv[1]

def ws_connect(url):
    rest = url.split("://", 1)[1]
    hostport, path = rest.split("/", 1)
    host, port = hostport.split(":")
    s = socket.create_connection((host, int(port)), timeout=20)
    key = base64.b64encode(os.urandom(16)).decode()
    s.sendall((f"GET /{path} HTTP/1.1\r\nHost: {hostport}\r\n"
               "Upgrade: websocket\r\nConnection: Upgrade\r\n"
               f"Sec-WebSocket-Key: {key}\r\nSec-WebSocket-Version: 13\r\n\r\n")
              .encode())
    buf = b""
    while b"\r\n\r\n" not in buf:
        buf += s.recv(4096)
    return s

def recv_exact(s, n):
    data = b""
    while len(data) < n:
        chunk = s.recv(n - len(data))
        if not chunk:
            raise EOFError
        data += chunk
    return data

def ws_send(s, obj):
    data = json.dumps(obj).encode()
    mask = os.urandom(4)
    head = bytes([0x81])
    n = len(data)
    if n < 126:
        head += bytes([0x80 | n])
    elif n < 65536:
        head += bytes([0x80 | 126]) + struct.pack(">H", n)
    else:
        head += bytes([0x80 | 127]) + struct.pack(">Q", n)
    s.sendall(head + mask + bytes(b ^ mask[i % 4] for i, b in enumerate(data)))

def ws_recv(s):
    payload = b""
    while True:
        b0, b1 = recv_exact(s, 2)
        n = b1 & 0x7f
        if n == 126:
            n = struct.unpack(">H", recv_exact(s, 2))[0]
        elif n == 127:
            n = struct.unpack(">Q", recv_exact(s, 8))[0]
        payload += recv_exact(s, n)
        if b0 & 0x80:
            return json.loads(payload)

next_id = 0
def call(s, method, params=None, session=None):
    global next_id
    next_id += 1
    msg = {"id": next_id, "method": method, "params": params or {}}
    if session:
        msg["sessionId"] = session
    ws_send(s, msg)
    while True:
        reply = ws_recv(s)
        if reply.get("id") == next_id:
            if "error" in reply:
                raise RuntimeError(reply["error"])
            return reply["result"]

# A Settings window's page. Ash often will not open one over DevTools
# ("Failed to open a new tab"); then another page is sent there instead,
# which opens the Settings app.
def settings_pages():
    pages = json.load(urllib.request.urlopen(f"http://127.0.0.1:{PORT}/json/list"))
    return [p for p in pages if p["type"] == "page" and
            p["url"].startswith("chrome://os-settings")], pages

settings, pages = settings_pages()
session = None
if not settings:
    version = json.load(
        urllib.request.urlopen(f"http://127.0.0.1:{PORT}/json/version"))
    b = ws_connect(version["webSocketDebuggerUrl"])
    try:
        call(b, "Target.createTarget",
             {"url": "chrome://os-settings/osLanguages/input"})
    except RuntimeError:
        other = [p for p in pages if p["type"] == "page"]
        if not other:
            sys.exit("no page to open Settings from: open a browser window")
        o = ws_connect(other[0]["webSocketDebuggerUrl"])
        call(o, "Page.navigate", {"url": "chrome://os-settings/osLanguages/input"})
    for _ in range(20):
        settings, _ = settings_pages()
        if settings:
            break
        time.sleep(1)
    if not settings:
        sys.exit("Settings did not open")
s = ws_connect(settings[0]["webSocketDebuggerUrl"])
js = """(async () => {
  await new Promise(r => setTimeout(r, 2000));
  // Enabled ones first: the lists leave out what is already in use (US).
  const enabled = await new Promise(r => chrome.inputMethodPrivate.getInputMethods(r));
  const lists = await chrome.languageSettingsPrivate.getInputMethodLists();
  const all = enabled.concat(lists.componentExtensionImes, lists.thirdPartyExtensionImes);
  // Neither list has the US layout once it is the only one: keyboard
  // layouts (xkb:us::eng) are taken by their id.
  const ime = all.find(i => i.id.includes(WORD)) ||
    (WORD.startsWith('xkb:') &&
     {id: '_comp_ime_fgoepimhcoialccpbmpnnblemnepkkao' + WORD});
  if (!ime) return 'none of: ' + all.map(i => i.id).join(' ');
  if (!enabled.includes(ime)) {
    chrome.languageSettingsPrivate.addInputMethod(ime.id);
    await new Promise(r => setTimeout(r, 1000));
  }
  await new Promise(r => chrome.inputMethodPrivate.setCurrentInputMethod(ime.id, r));
  const now = await new Promise(r => chrome.inputMethodPrivate.getCurrentInputMethod(r));
  return 'current ' + now;
})()""".replace("WORD", json.dumps(word))
result = call(s, "Runtime.evaluate",
              {"expression": js, "awaitPromise": True, "returnByValue": True},
              session)
print(result.get("result", {}).get("value", result))
EOF

# Picks an input method (nickel-ime-select in the VM). It works from an open
# page; right after a sign-in there may be none, so a browser window opens
# (Ctrl+N) and it tries again.
ime_select() {
  local out
  out=$(vm "nickel-ime-select $1" 2>&1)
  if [[ ${out} == *"no page to open Settings from"* ]]; then
    nickel-vmctl key ctrl-n
    sleep 5
    out=$(vm "nickel-ime-select $1" 2>&1)
  fi
  echo "${out}"
}

# --build-only: the test tools only (files-bridge-test.sh uses them).
[ "${1:-}" = --build-only ] && exit 0

session() {
  vm 'dbus-send --system --print-reply --dest=org.chromium.SessionManager /org/chromium/SessionManager org.chromium.SessionManagerInterface.RetrieveSessionState' |
    sed -n 's/.*string "\(.*\)"/\1/p'
}
sign_in() {
  for _ in $(seq 60); do
    [ "$(session)" = stopped ] && break
    sleep 2
  done
  sleep 10
  nickel-vmctl type nickel-test-pw
  nickel-vmctl key ret
  for _ in $(seq 60); do
    [ "$(session)" = started ] && vm 'test -S /run/chrome/wayland-0' && break
    sleep 2
  done
  sleep 15
  # ChromeOS offers the last session's windows ("Welcome Recap"), which
  # takes the keyboard: no thanks. It comes some 35-40 s after sign-in and
  # logs nothing to wait for, so Esc every 5 s for a minute (Esc on the bare
  # desktop does nothing).
  for _ in $(seq 12); do nickel-vmctl key esc; sleep 5; done
  # A first such sign-in with nothing to restore shows the feature's
  # introduction instead, which Esc leaves: its "Got it" (empty desktop
  # when it isn't there).
  nickel-vmctl click 638 536; sleep 3
}

if ! vm "grep -q -- --remote-debugging-port=$PORT /etc/chrome_dev.conf"; then
  echo "== turn on Chrome's remote debugging (UI restart, sign in again)"
  vm "mount -o remount,rw / 2>/dev/null; echo --remote-debugging-port=$PORT >> /etc/chrome_dev.conf; restart ui >/dev/null"
  sign_in
elif [ "$(session)" != started ]; then
  sign_in
fi

echo "== 1. text-input-v3 with Pinyin"
ime_select "pinyin"
vm "rm -f /tmp/ime.out; (nickel-session-exec /usr/local/bin/wl-ime-test textinput 15 > /tmp/ime.out 2>&1 &)"
sleep 4
nickel-vmctl type nihao
sleep 1
nickel-vmctl key spc
sleep 12
vm "cat /tmp/ime.out"
ime_select "xkb:us::eng" >/dev/null

echo "== 2. primary selection between two clients"
vm "rm -f /tmp/ps-set.out; (nickel-session-exec /usr/local/bin/wl-ime-test primary-set 'NickelOS primary 選択' 12 > /tmp/ps-set.out 2>&1 &)"
sleep 4
vm "nickel-session-exec /usr/local/bin/wl-ime-test primary-get 6"
sleep 9
vm "cat /tmp/ps-set.out"
