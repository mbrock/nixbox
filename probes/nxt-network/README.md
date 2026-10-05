# NXT WebSocket on Xbox

`pkgsXbox.nxtrt-iocp` installs `<nxtrt/websocket.hpp>` alongside NXT's HTTP
client. Link the `nxtrt-iocp` pkg-config target; it propagates the UWP
libcrypto, zlib and Boost dependencies. `nxt-websocket` hosts the upstream
portable probe unchanged in the same SDL3 harness as `nxt-network`, with a
separate package identity and LocalState directory.

```sh
./build nxtrt-iocp-xbox
./build nxt-websocket
nix run .#deploy-nxt-websocket
```

## Coroutine API and boundaries

From a task running on an NXT deck:

```cpp
#include <nxtrt/websocket.hpp>

auto client = co_await nxtrt::websocket::connect(
    "wss://example.org/echo",
    {.ca_file = public_ca_path, .max_message_size = 1024 * 1024});
co_await client->send(nxtrt::websocket::message_type::text, "hello");
auto reply = co_await client->receive();
co_await client->close(1000, "done");
// Continue receiving until the Close event; use a deadline for the handshake.
```

The client owns its connection and returns owned text, binary, Pong and Close
events. Binary strings may contain NULs. Incoming Ping is answered automatically.
Upgrade validation, fresh client masks, UTF-8 validation, bounded fragmented
messages and interleaved controls are implemented. The default incoming and
outgoing message cap is 1 MiB; Upgrade headers are bounded by 16 KiB and control
payloads by 125 bytes. `wss` requires explicit PEM roots and validates both the
chain and peer identity using NXT's TLS 1.3 implementation.

**Operations must not overlap, including send versus receive.** The client is
deck-confined and must outlive its tasks. TLS KeyUpdate replies and WebSocket
Ping/Close handling can write during a receive operation, so this first API
does not support a permanent background reader plus interactive sends.
Sequential request/reply and receive-only phases work. Cancellation drains the
pending runtime operation, then aborts the connection; reconnect rather than
trying to continue a partially transmitted or received frame. Apply NXT
deadlines where a peer could wait indefinitely.

There are no subprotocols, extensions/compression, redirects or automatic
reconnect. URLs support DNS/IPv4 authorities, not bracketed IPv6, userinfo or
fragments; a query must follow `/`. This is not an SSH adapter or terminal app.

## Verification status

The installed Windows probe passed 90 localhost ws/wss fixture cases under
Wine, with zero server assertion failures. The first exact-package Xbox run
on 2026-10-05 executed 89 remote cases: **88 passed and one failed**, with the
localhost-only wrong-SAN case explicitly skipped. The failed case was plain
`ws` send cancellation (`send did not cancel`); the corresponding `wss` case
and all other cases passed.

The diagnostic pin adds separate send/timer outcomes and elapsed time without
changing the runtime or strict assertion. Paired Xbox runs against the original
and receive-buffer-constrained fixtures each still returned **89/1**. In both,
the 16 MiB plaintext send completed in 90 ms and cancelled its 2000 ms timer;
the encrypted send cancelled in 2005 ms after its timer completed. The failure
is therefore an unmet pending-send precondition in these runs, not evidence of
ignored cancellation. The underlying buffering behavior is not established,
and plaintext send-cancellation remains unverified on hardware. This is not a
green hardware suite. The
[verification thread](https://ampcode.com/threads/T-01a10a83-643e-70c7-9fad-afd92b717fc2)
contains the exact-package report and preserved failure evidence.

## Fixture and runtime provisioning

Use `test/websocket-fixture.py` from the NXT revision pinned in `flake.lock`.
It requires Python 3 and `openssl`, creates disposable one-day TLS certificates,
and serves both valid exchanges and intentionally malformed/stalled peers.
For a local installed CLI/Wine run, pass the installed `websocket-probe.exe`
with the script's `--wine PATH_TO_WINE` option; the script manages the fixture
and checks server-side assertions.

For Xbox, choose an IPv4 address actually reachable from the console and two
already-permitted TCP ports. Runner tailnet access does not imply console
tailnet access. For example, on that server, using a new private directory:

```sh
python3 nxtui/test/websocket-fixture.py --serve \
  --bind SERVER_IP --advertise SERVER_IP --ws-port 8765 --wss-port 8766 \
  --directory /private/new-websocket-fixture
```

Run it through the environment's supervised service manager (in an orb,
`amp orb service start`), or in the foreground. It prints `WS_BASE`, `WSS_BASE`
and the public PEM paths. No firewall/infrastructure changes are needed when
the selected ports are already reachable.

Copy **only** `server.pem` and `wrong.pem` into the app's LocalState directory
returned by `SDL_GetPrefPath("nixbox", "nxt-websocket")`, via Device Portal.
Never copy fixture private `*.key` files. Create `probe-args.txt` beside them,
one argument per line:

```text
ws://SERVER_IP:8765
wss://SERVER_IP:8766
ABSOLUTE_LOCALSTATE_PATH\server.pem
ABSOLUTE_LOCALSTATE_PATH\wrong.pem
2000
```

Bases have no trailing slash. The final argument is the cancellation delay in
milliseconds (default 100, allowed 20–5000); the probe uses a 15-second deadline
per case. Launch the app after provisioning. It shows RUNNING/PASSED/FAILED and
the start of its report; **read the full `network.txt` for the summary and any
failure details**, since 90 case lines do not fit on one screen.

The localhost suite includes wrong-SAN rejection. A remote advertised-IP run
explicitly skips that localhost-only case and executes 89 cases; untrusted-root
rejection still runs. Do not count a SKIP as a PASS or ignore a nonzero failure
summary. Stop the fixture and remove its private directory and console
certificates/argument file after testing. The report contains no credentials.

For the existing DNS/socket/TLS and real Luna `nxt-network` diagnostic, see the
[chat app's transport checks](../../apps/nxt-chat/README.md#diagnostics-and-build-checks).
