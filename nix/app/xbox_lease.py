#!/usr/bin/env python3
"""Take, hold, and give back the shared Xbox's console lease.

Agents share one console. The Device Portal proxy (tools/xbox-proxy) grants
one client at a time a lease: while it is held, installs, launches, stops, and
other changing requests from anyone else are refused with 423, and other
clients wait their turn in a queue. Reads (screenshots, files, processes)
stay open. A lease lasts its ttl unless renewed.

  xbox-lease status                     who holds the console, who waits
  xbox-lease acquire [--label L] [--ttl S]   wait for the console; print the token
  xbox-lease release [TOKEN]            give it back (the saved token by default)
  xbox-lease run [--label L] [--ttl S] -- COMMAND...
                                        hold the console while COMMAND runs,
                                        with XBOX_LEASE set for it

The last token taken on this machine is kept in ~/.config/nixbox/lease, so
`xbox-lease release` needs no argument. `deploy` takes the lease itself unless
XBOX_LEASE is set, and keeps holding it after launch (its --hold).
"""
import argparse
import json
import os
from pathlib import Path
import socket
import subprocess
import sys
import threading
import time
import urllib.error
import urllib.parse
import urllib.request

HEADER = "X-Xbox-Lease"
CONFIG = Path(os.environ.get("XDG_CONFIG_HOME", str(Path.home() / ".config"))) / "nixbox"


def device_url(explicit=None):
    device = explicit or os.environ.get("UWP_DEVICE_URL")
    if not device and (CONFIG / "device").exists():
        device = (CONFIG / "device").read_text().strip()
    if not device:
        raise SystemExit(f"Set UWP_DEVICE_URL or write the Device Portal URL to {CONFIG}/device")
    return device.rstrip("/")


def default_label(what):
    return f"{what} from {os.environ.get('USER', '?')}@{socket.gethostname().split('.')[0]}"


def _call(device, method, path, params=None, timeout=40):
    url = device + path + ("?" + urllib.parse.urlencode(params) if params else "")
    request = urllib.request.Request(url, method=method, data=b"" if method == "POST" else None)
    try:
        with urllib.request.urlopen(request, timeout=timeout) as response:
            return response.status, json.load(response)
    except urllib.error.HTTPError as error:
        body = error.read()
        try:
            return error.code, json.loads(body)
        except ValueError:
            return error.code, {"error": body.decode(errors="replace")}


def describe(holder):
    if not holder:
        return "nobody"
    return f"{holder['label']!r} ({holder['who']}) until {holder['expires']}"


def acquire(device, label, ttl=600, quiet=False):
    """Wait for the console and return the lease token, or None when the
    portal has no lease service."""
    ticket, position = None, None
    while True:
        params = {"label": label, "ttl": int(ttl), "wait": 25}
        if ticket:
            params["ticket"] = ticket
        status, body = _call(device, "POST", "/lease/acquire", params)
        if status == 200:
            save(device, body["token"])
            if not quiet:
                print(f"Holding the Xbox until {body['expires']} as {label!r}", file=sys.stderr, flush=True)
            return body["token"]
        if status == 404:
            return None
        if status != 202:
            raise SystemExit(f"Lease request failed ({status}): {body}")
        ticket = body["ticket"]
        if body["position"] != position and not quiet:
            position = body["position"]
            print(f"Waiting for the Xbox: position {position}, held by {describe(body.get('holder'))}",
                  file=sys.stderr, flush=True)


def renew(device, token, ttl=600):
    status, body = _call(device, "POST", "/lease/renew", {"token": token, "ttl": int(ttl)})
    return status == 200


def release(device, token):
    _call(device, "POST", "/lease/release", {"token": token})
    if saved(device) == token:
        (CONFIG / "lease").unlink(missing_ok=True)


def save(device, token):
    CONFIG.mkdir(parents=True, exist_ok=True)
    (CONFIG / "lease").write_text(json.dumps({"device": device, "token": token}))


def saved(device):
    try:
        record = json.loads((CONFIG / "lease").read_text())
        return record["token"] if record.get("device") == device else None
    except (OSError, ValueError, KeyError):
        return None


def keep_renewed(device, token, ttl):
    """Renew in the background until the returned event is set."""
    stop = threading.Event()

    def loop():
        while not stop.wait(max(10, ttl / 3)):
            renew(device, token, ttl)

    threading.Thread(target=loop, daemon=True).start()
    return stop


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--device", help="Device Portal URL (default: $UWP_DEVICE_URL)")
    commands = parser.add_subparsers(dest="command", required=True)
    commands.add_parser("status")
    take = commands.add_parser("acquire")
    take.add_argument("--label", default=default_label("a session"))
    take.add_argument("--ttl", type=int, default=600)
    give = commands.add_parser("release")
    give.add_argument("token", nargs="?")
    run = commands.add_parser("run")
    run.add_argument("--label", default=None)
    run.add_argument("--ttl", type=int, default=600)
    run.add_argument("argv", nargs=argparse.REMAINDER)
    args = parser.parse_args(argv)
    device = device_url(args.device)

    if args.command == "status":
        status, body = _call(device, "GET", "/lease")
        if status == 404:
            print("This portal has no lease service.")
            return 0
        print(f"Held by {describe(body['holder'])}")
        for i, ticket in enumerate(body.get("queue") or [], 1):
            print(f"  {i}. {ticket['label']!r} ({ticket['who']}) waiting since {ticket['since']}")
        return 0
    if args.command == "acquire":
        token = acquire(device, args.label, args.ttl)
        print(token or "")
        return 0
    if args.command == "release":
        token = args.token or saved(device)
        if not token:
            raise SystemExit("No lease token given or saved.")
        release(device, token)
        print("Released the Xbox.", file=sys.stderr)
        return 0
    command = args.argv[1:] if args.argv[:1] == ["--"] else args.argv
    if not command:
        raise SystemExit("xbox-lease run needs a command after --")
    token = acquire(device, args.label or default_label(Path(command[0]).name), args.ttl)
    stop = keep_renewed(device, token, args.ttl) if token else None
    environment = dict(os.environ, XBOX_LEASE=token or "")
    try:
        return subprocess.call(command, env=environment)
    finally:
        if token:
            stop.set()
            release(device, token)


if __name__ == "__main__":
    sys.exit(main())
