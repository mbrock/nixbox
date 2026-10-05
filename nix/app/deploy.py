#!/usr/bin/env python3
"""Sign a nixbox package locally, install it on an Xbox, launch it, and take a screenshot.

The console is shared, so deployment first takes its lease (see xbox_lease.py):
it waits its turn, and keeps holding the console for --hold seconds after the
launch so the app can be watched undisturbed. With XBOX_LEASE set (as
`xbox-lease run` sets it), the deployment uses that lease instead.

The package is a mkXboxApp result: a directory with layout/ and one .msix.
The development certificate is created on first use, with the manifest's
publisher as its subject, and kept outside the repository and the Nix store.
"""
import argparse
import atexit
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import time
import urllib.parse
import urllib.request
import xml.etree.ElementTree as ET

from openappx.deploy import DevicePortal

import xbox_lease

config = Path(os.environ.get("XDG_CONFIG_HOME", str(Path.home() / ".config"))) / "nixbox"
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("package", type=Path, help="mkXboxApp result directory")
parser.add_argument("--device", default=os.environ.get("UWP_DEVICE_URL"),
                    help=f"Device Portal URL (default: $UWP_DEVICE_URL, then {config}/device)")
parser.add_argument("--screenshot", type=Path, default=Path("xbox-screenshot.png"),
                    help="where to save the console screenshot (default: %(default)s)")
parser.add_argument("--crash-dumps", action="store_true",
                    help="have the console keep a dump if the app crashes")
parser.add_argument("--hold", type=int, default=300,
                    help="seconds to keep the console lease after launching (default: %(default)s);"
                         " give it back early with `xbox-lease release`")
args = parser.parse_args()
device = xbox_lease.device_url(args.device)

manifest = ET.parse(args.package / "layout/AppxManifest.xml").getroot()
identity = manifest.find("{*}Identity").attrib
application = manifest.find("{*}Applications/{*}Application").attrib
publisher = identity["Publisher"]
(msix,) = args.package.glob("*.msix")

certs = config / "certs"
cert = certs / re.sub(r"[^A-Za-z0-9-]+", "_", publisher)
pfx, cer = cert.parent / (cert.name + ".pfx"), cert.parent / (cert.name + ".cer")
if not pfx.exists():
    certs.mkdir(parents=True, exist_ok=True)
    print(f"Creating a development certificate for {publisher}", flush=True)
    old_umask = os.umask(0o077)
    try:
        subprocess.run(["openappx", "sign", "--make-test-cert", publisher, "--cert-out", str(cert)],
                       check=True)
    finally:
        os.umask(old_umask)

with tempfile.TemporaryDirectory() as tmp:
    signed = Path(tmp) / msix.name
    shutil.copyfile(msix, signed)
    signed.chmod(0o644)
    subprocess.run(["openappx", "sign", "--package", str(signed), "--pfx", str(pfx)],
                   check=True, stdout=subprocess.DEVNULL)

    # Take the console before changing anything on it. A lease this deploy
    # took is given back if it fails before launching, or if --hold is 0.
    lease = os.environ.get("XBOX_LEASE")
    if not lease:
        lease = xbox_lease.acquire(device, xbox_lease.default_label(f"deploy {identity['Name']}"),
                                   ttl=max(args.hold, 120))
        launched = []

        @atexit.register
        def give_back():
            if lease and (not launched or args.hold <= 0):
                xbox_lease.release(device, lease)
            elif lease:
                print(f"Holding the Xbox for {args.hold} s; `xbox-lease release` gives it back sooner",
                      flush=True)
    else:
        launched = [True]

    class LeasedPortal(DevicePortal):
        def _urlopen(self, request):
            if lease:
                request.add_header(xbox_lease.HEADER, lease)
            return super()._urlopen(request)

    # HTTPS uses normal CA verification; credentials come from the environment.
    portal = LeasedPortal(device, os.environ.get("UWP_DEVICE_USER", ""),
                          os.environ.get("OPENAPPX_DEVICE_PASSWORD", ""), timeout=60)
    existing = [p for p in portal.packages() if p["PackageFullName"].startswith(identity["Name"] + "_")]
    for previous in existing:
        if previous.get("Publisher") != publisher:
            raise SystemExit(f"{previous['PackageFullName']} has another publisher; refusing to replace it")
    for previous in existing:
        print(f"Replacing {previous['PackageFullName']}", flush=True)
        portal.uninstall(previous["PackageFullName"])

    portal.install_certificate(cer)
    print(f"Installing {msix.name}", flush=True)
    portal.install(signed)
    state = portal.wait_for_install(timeout=120,
                                    on_progress=lambda s: print(f"Deployment: {s.phase}", flush=True))
    if not state.done or state.failed:
        raise SystemExit(f"Deployment did not succeed: {state}")

installed = next(p for p in portal.packages() if p["PackageFullName"].startswith(identity["Name"] + "_"))
if args.crash_dumps:
    request = urllib.request.Request(
        device + "/api/debug/dump/usermode/crashcontrol?packageFullName="
        + urllib.parse.quote(installed["PackageFullName"]), data=b"", method="POST")
    request.add_header("Content-Length", "0")
    portal._open(request)
    print("Crash dumps enabled: " + device + "/api/debug/dump/usermode/dumps", flush=True)
print(f"Launching {installed['PackageFullName']}!{application['Id']}", flush=True)
portal.start_app(installed["PackageFullName"], application["Id"])
launched.append(True)
if lease and args.hold > 0 and not os.environ.get("XBOX_LEASE"):
    xbox_lease.renew(device, lease, args.hold)
time.sleep(3)
with urllib.request.urlopen(device + "/api/resourcemanager/processes", timeout=30) as response:
    processes = json.load(response)["Processes"]
image = application["Executable"].lower()
running = [p for p in processes if p.get("ImageName", "").lower() == image]
with urllib.request.urlopen(device + "/ext/screenshot?hdr=false", timeout=30) as response:
    screenshot = response.read()
if not screenshot.startswith(b"\x89PNG\r\n\x1a\n"):
    raise SystemExit("Screenshot response was not a PNG")
args.screenshot.write_bytes(screenshot)
print(f"Screenshot: {args.screenshot}", flush=True)
if not running:
    raise SystemExit(f"Launch returned successfully, but {application['Executable']} is not running")
print(f"{application['Executable']} is running (pid {running[0]['ProcessId']})", flush=True)
