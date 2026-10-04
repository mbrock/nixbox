#!/usr/bin/env python3
"""Install and launch the hello example through a configured Xbox Device Portal."""
import argparse
import json
import os
from pathlib import Path
import time
import urllib.request
import xml.etree.ElementTree as ET

from openappx.deploy import DevicePortal

ROOT = Path(__file__).resolve().parent
URL = os.environ.get("UWP_DEVICE_URL")
LAYOUT = ROOT / "build-output/hello-layout"
PACKAGE = ROOT / "build-output/hello-uwp.msix"
CERT_DIR = Path(os.environ.get("UWP_CERT_DIR", str(
    Path(os.environ.get("XDG_CONFIG_HOME", str(Path.home() / ".config"))) / "uwp-crossbuild")))
CERT = CERT_DIR / "dev.cer"
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--replace", action="store_true", help="replace this installed hello example")
args = parser.parse_args()
if not URL:
    parser.error("Set UWP_DEVICE_URL to your Xbox Device Portal URL")
manifest = ET.parse(LAYOUT / "AppxManifest.xml").getroot()
identity = manifest.find("{*}Identity").attrib["Name"]
app_id = manifest.find("{*}Applications/{*}Application").attrib["Id"]

# Use normal CA verification; credentials are supplied through the environment.
portal = DevicePortal(URL, os.environ.get("UWP_DEVICE_USER", ""),
                      os.environ.get("OPENAPPX_DEVICE_PASSWORD", ""), timeout=60)
existing = [p for p in portal.packages()
            if p["PackageFullName"].startswith(identity + "_")]
if existing:
    if not args.replace:
        raise SystemExit("Example already installed; use --replace to redeploy it.")
    for previous in existing:
        if previous.get("Publisher") != manifest.find("{*}Identity").attrib["Publisher"]:
            raise SystemExit("Installed example has a different publisher; refusing replacement")
    for previous in existing:
        print(f"Replacing {previous['PackageFullName']}", flush=True)
        portal.uninstall(previous["PackageFullName"])

print("Installing the development signing certificate", flush=True)
portal.install_certificate(CERT)
print("Installing hello-uwp", flush=True)
portal.install(PACKAGE)
state = portal.wait_for_install(timeout=120,
    on_progress=lambda s: print(f"Deployment: {s.phase}", flush=True))
if not state.done or state.failed:
    raise SystemExit(f"Deployment did not succeed: {state}")
installed = next(p for p in portal.packages()
                 if p["PackageFullName"].startswith(identity + "_"))
full_name = installed["PackageFullName"]
print(f"Launching {full_name}!{app_id}", flush=True)
portal.start_app(full_name, app_id)
time.sleep(3)
with urllib.request.urlopen(URL + "/api/resourcemanager/processes", timeout=30) as r:
    processes = json.load(r)["Processes"]
hello = [p for p in processes if p.get("ImageName", "").lower() == "hello.exe"]
print("Running hello.exe processes:", json.dumps(hello), flush=True)
with urllib.request.urlopen(URL + "/ext/screenshot?hdr=false", timeout=30) as r:
    screenshot = r.read()
if not screenshot.startswith(b"\x89PNG\r\n\x1a\n"):
    raise SystemExit("Screenshot response was not a PNG")
image = ROOT / "build-output/xbox-hello.png"
image.write_bytes(screenshot)
(ROOT / "build-output/deployment.json").write_text(
    json.dumps({"device": URL, "package": installed, "processes": hello}, indent=2))
print(f"Screenshot: {image}", flush=True)
if not hello:
    raise SystemExit("Launch returned successfully, but hello.exe was not running")
