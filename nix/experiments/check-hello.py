#!/usr/bin/env python3
"""Exercise the cross-built console program through Wine, not Xbox activation."""
from pathlib import Path
import subprocess
import sys

exe = str(Path(sys.argv[1]).resolve())
cases = [
    ([], 0, "Hello, world!"),
    (["--greeting=Hello from pkgsXbox"], 0, "Hello from pkgsXbox"),
    (["--traditional"], 0, "hello, world"),
    (["--help"], 0, "--greeting"),
    (["--version"], 0, "2.12.3"),
    (["--not-an-option"], 1, "--not-an-option"),
]
for args, code, expected in cases:
    result = subprocess.run(["wine", exe, *args], capture_output=True, text=True, timeout=30)
    assert result.returncode == code, (args, result.returncode, result.stderr)
    assert expected in result.stdout + result.stderr, (args, result.stdout, result.stderr)
    print(f"PASS {args or ['default greeting']}", flush=True)
