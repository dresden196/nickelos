#!/usr/bin/env python3
"""Checks, and with --restore restores, Chrome's first-class GCS dependencies
(prebuilt clang, node, node_modules, sysroots: DEPS entries with dep_type
"gcs") as gclient sync would, from the exact objects DEPS pins, verified by
sha256. For a checkout an interrupted `gclient revert` left without some of
their files (2026-10-07: clang and node gone, so ninja could not start).
Unlike gclient, extracted files keep the archive's times, older than any build
output, so ninja does not rebuild what uses them; a real sync gives them new
times and Chrome rebuilds almost entirely.
On nickel-build, outside the chroot, as nickel:
  chrome-gcs-restore.py /srv/nickel/src/.cache/distfiles/chrome-src/src [--restore]"""
import hashlib
import json
import os
import sys
import tarfile
import urllib.request

src = sys.argv[1]
restore = "--restore" in sys.argv
deps_text = open(os.path.join(src, "DEPS")).read()
scope = {"Var": lambda name: "{%s}" % name, "Str": str}
exec(deps_text, scope)
deps = scope["deps"]


# This checkout: ChromeOS on Linux x64 (gclient's own values for these).
CONDITION_VARS = {"host_os": "linux", "host_cpu": "x64", "checkout_linux": True,
                  "checkout_x64": True, "checkout_x86": False,
                  "checkout_chromeos": True, "non_git_source": True}


def applies(condition):
    if not condition:
        return True
    names = dict(CONDITION_VARS)
    while True:
        try:
            return bool(eval(condition, {"__builtins__": {}}, names))
        except NameError as e:
            names[str(e).split("'")[1]] = False  # unset checkout_* vars


def file_prefix(object_name):
    return object_name.replace("/", "_").replace(".", "_")


bad = []
for name, dep in deps.items():
    if not isinstance(dep, dict) or dep.get("dep_type") != "gcs":
        continue
    out_dir = os.path.join(os.path.dirname(src), name.split(":")[0])
    if not os.path.isdir(out_dir):
        continue  # condition not met for this checkout: never downloaded
    if not applies(dep.get("condition")):
        continue
    for obj in dep["objects"]:
        if not applies(obj.get("condition")):
            continue
        prefix = file_prefix(obj["object_name"])
        hash_file = os.path.join(out_dir, f".{prefix}_hash")
        names_file = os.path.join(out_dir, f".{prefix}_content_names")
        problem = None
        if not os.path.exists(hash_file):
            problem = "no hash file"
        elif open(hash_file).read().strip() != obj["sha256sum"]:
            problem = "hash differs"
        elif os.path.exists(names_file):
            names = json.load(open(names_file))
            missing = [n for n in names
                       if not os.path.lexists(os.path.join(out_dir, n))]
            if missing:
                problem = f"{len(missing)}/{len(names)} files missing"
        elif obj.get("output_file") and not os.path.exists(
                os.path.join(out_dir, obj["output_file"])):
            problem = "output file missing"
        if problem:
            bad.append((name, out_dir, dep["bucket"], obj, prefix))
            print(f"{problem}: {name} ({obj['object_name']})")

print(f"{len(bad)} damaged")
if not restore:
    sys.exit(0)

for name, out_dir, bucket, obj, prefix in bad:
    url = f"https://storage.googleapis.com/{bucket}/{obj['object_name']}"
    artifact = os.path.join(
        out_dir, obj.get("output_file") or "." + obj["object_name"].replace("/", "_"))
    if not (os.path.exists(artifact) and hashlib.sha256(
            open(artifact, "rb").read()).hexdigest() == obj["sha256sum"]):
        data = urllib.request.urlopen(url, timeout=300).read()
        if hashlib.sha256(data).hexdigest() != obj["sha256sum"]:
            sys.exit(f"sha256 mismatch for {url}")
        with open(artifact + ".part", "wb") as f:
            f.write(data)
        os.replace(artifact + ".part", artifact)
    if tarfile.is_tarfile(artifact):
        with tarfile.open(artifact, "r:*") as tar:
            with open(os.path.join(out_dir, f".{prefix}_content_names"), "w") as f:
                f.write(json.dumps(tar.getnames()))
            tar.extractall(path=out_dir, filter="data")
    else:
        os.chmod(artifact, 0o755)  # e.g. clang-format, a bare executable
    with open(os.path.join(out_dir, f".{prefix}_hash"), "w") as f:
        f.write(obj["sha256sum"])
    with open(os.path.join(out_dir, f".{prefix}_is_first_class_gcs"), "w") as f:
        f.write("1")
    print(f"restored {name}")
