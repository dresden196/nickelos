#!/usr/bin/env python3
# A pretend phone for testing NickelOS's KDE Connect (Valent) in the test VM,
# where no real phone can reach it. Runs in the VM (root is fine) and talks
# to the session's Valent over loopback, as the KDE Connect app does over
# Wi-Fi, protocol v8:
#   1. it announces itself with a UDP identity packet to port 1716;
#   2. Valent connects to its TCP port and sends its identity, then they
#      negotiate TLS (Valent the server, this the client, with a certificate
#      whose common name is the device id) and exchange identities again;
#   3. unless --no-pair, it asks to pair (Valent shows the request for 30
#      seconds); it accepts Valent's requests; once paired it reports its
#      battery (--battery, --charging).
# It prints a line for each packet from Valent ("recv kdeconnect.pair {...}")
# and for each step, and keeps the connection until --seconds have passed.
#   kdeconnect-fake.py [--name N] [--id ID] [--seconds S] [--no-pair]
#                      [--battery PCT] [--charging]
import argparse
import json
import os
import socket
import ssl
import subprocess
import sys
import time

p = argparse.ArgumentParser()
p.add_argument("--name", default="Fake Phone")
# Valent's rule: 32-38 characters from [A-Za-z0-9_].
p.add_argument("--id", default="nickel_fake_phone_00000000000000001")
p.add_argument("--seconds", type=int, default=120)
p.add_argument("--no-pair", action="store_true")
p.add_argument("--battery", type=int, default=80)
p.add_argument("--charging", action="store_true")
# The first free one from here up, within KDE Connect's range.
p.add_argument("--port", type=int, default=1740)
args = p.parse_args()


def out(*words):
    print(*words, flush=True)


# The same certificate every run, so a pairing outlives the script.
state = f"/var/tmp/kdeconnect-fake/{args.id}"
os.makedirs(state, exist_ok=True)
cert, key = f"{state}/cert.pem", f"{state}/key.pem"
if not os.path.exists(cert):
    subprocess.run(
        ["openssl", "req", "-x509", "-newkey", "ec", "-pkeyopt",
         "ec_paramgen_curve:prime256v1", "-nodes", "-days", "3650",
         "-subj", f"/O=KDE/OU=Kde connect/CN={args.id}",
         "-keyout", key, "-out", cert],
        check=True, capture_output=True)


def packet(kind, body):
    return (json.dumps({"id": int(time.time() * 1000), "type": kind,
                        "body": body}) + "\n").encode()


identity = {
    "deviceId": args.id,
    "deviceName": args.name,
    "deviceType": "phone",
    "protocolVersion": 8,
    "tcpPort": args.port,
    "incomingCapabilities": [
        "kdeconnect.battery.request", "kdeconnect.findmyphone.request",
        "kdeconnect.ping", "kdeconnect.share.request",
    ],
    "outgoingCapabilities": [
        "kdeconnect.battery", "kdeconnect.ping", "kdeconnect.share.request",
    ],
}


class Lines:
    """Packets, one JSON object per line, from a socket. A timeout returns
    TIMEOUT and leaves the socket usable (a makefile() reader would not)."""

    TIMEOUT = object()

    def __init__(self, sock):
        self.sock = sock
        self.buf = b""

    def read(self, timeout):
        self.sock.settimeout(timeout)
        while b"\n" not in self.buf:
            try:
                chunk = self.sock.recv(65536)
            except (socket.timeout, TimeoutError):
                return Lines.TIMEOUT
            if not chunk:
                return None
            self.buf += chunk
        line, self.buf = self.buf.split(b"\n", 1)
        return json.loads(line)


listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
for port in range(args.port, 1765):
    try:
        listener.bind(("127.0.0.1", port))
        break
    except OSError:
        continue
else:
    out("no free port in", args.port, "to 1764")
    sys.exit(1)
identity["tcpPort"] = port
listener.listen(1)
listener.settimeout(20)

udp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
udp.sendto(packet("kdeconnect.identity", identity), ("127.0.0.1", 1716))
out("announced", args.id)

try:
    conn, _ = listener.accept()
except socket.timeout:
    out("timeout: Valent did not connect")
    sys.exit(1)
valent = Lines(conn).read(20)
if valent in (None, Lines.TIMEOUT):
    out("no identity from Valent")
    sys.exit(1)
out("valent", valent["body"]["deviceId"], valent["body"].get("deviceName"))

# Valent opened the connection, so it is the TLS server.
ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
ctx.check_hostname = False
ctx.verify_mode = ssl.CERT_NONE
ctx.load_cert_chain(cert, key)
tls = ctx.wrap_socket(conn, server_side=False)
out("tls", tls.version())
tls.sendall(packet("kdeconnect.identity", identity))
reader = Lines(tls)
secure = reader.read(20)
if secure in (None, Lines.TIMEOUT):
    out("no secure identity from Valent")
    sys.exit(1)
out("secure identity", secure["body"]["deviceId"])

paired = False
asked_at = None
if not args.no_pair:
    tls.sendall(packet("kdeconnect.pair", {"pair": True,
                                           "timestamp": int(time.time())}))
    asked_at = time.time()
    out("asked to pair")


def report_battery():
    tls.sendall(packet("kdeconnect.battery", {
        "currentCharge": args.battery, "isCharging": args.charging,
        "thresholdEvent": 0}))
    out("battery", args.battery)


end = time.time() + args.seconds
while time.time() < end:
    try:
        pkt = reader.read(1)
    except (ssl.SSLError, OSError) as e:
        out("closed", e)
        break
    if pkt is Lines.TIMEOUT:
        continue
    if pkt is None:
        out("closed")
        break
    out("recv", pkt["type"], json.dumps(pkt.get("body", {})))
    if pkt["type"] == "kdeconnect.pair":
        if pkt["body"].get("pair", False):
            # Either Valent accepts our request (within its 30 seconds), or
            # it asks: accept, as the app does when the user taps Accept.
            if asked_at is None or time.time() - asked_at > 30:
                tls.sendall(packet("kdeconnect.pair", {
                    "pair": True, "timestamp": int(time.time())}))
                out("accepted Valent's request")
            asked_at = None
            paired = True
            out("paired")
            report_battery()
        else:
            asked_at = None
            paired = False
            out("unpaired")
    elif pkt["type"] == "kdeconnect.battery.request":
        # Valent asks only paired devices (one paired on an earlier run).
        report_battery()
out("done")
