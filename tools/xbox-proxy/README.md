# Xbox portal proxy

The source of the proxy that runs on temple, with the console lease that keeps
agents from replacing each other's apps. See [The console lease](#the-console-lease).

Private address: https://xbox.whale-justice.ts.net/

This Go program runs on the Mac Mini (`ssh temple`). It embeds an independent
Tailscale node named `xbox` and serves HTTPS on that node's port 443. Its reverse
proxy supplies the Xbox login automatically and forwards through
`https://temple.whale-justice.ts.net:8443`, which reaches the Xbox at
`https://192.168.88.7:11443`. It also supports WebSocket upgrades. The embedded
node uses its own Tailscale connection for the outgoing proxy hop.

The new proxy validates temple's public HTTPS certificate. The existing
Tailscale Serve configuration on temple handles the Xbox's self-signed
certificate. If the Xbox's local IP changes, update that Serve configuration:

```sh
/Applications/Tailscale.app/Contents/MacOS/Tailscale serve --bg --https=8443 https+insecure://NEW-IP:11443
```

Both the embedded proxy and temple's port-8443 Serve must remain running.

Installed on temple:

- Program: `~/.local/bin/xbox-proxy`
- Source and launcher: `~/.local/share/xbox-proxy/`
- Private Tailscale identity: `~/.local/state/xbox-proxy/tsnet`
- Logs: `~/.local/state/xbox-proxy/stderr.log`
- Launch agent: `~/Library/LaunchAgents/se.brockman.xbox-proxy.plist`

The launch agent starts when the Mac account logs in and restarts the proxy
after failures. The Xbox must be awake in Developer Mode. The Mac Mini must
remain running with its user session available.

Restart on temple:

```sh
launchctl kickstart -k gui/$(id -u)/se.brockman.xbox-proxy
```

Stop and unload:

```sh
launchctl bootout gui/$(id -u) ~/Library/LaunchAgents/se.brockman.xbox-proxy.plist
```

Build and install (from this directory, on any Apple silicon Mac):

```sh
go test ./... && go build -o xbox-proxy .
scp xbox-proxy temple:.local/bin/xbox-proxy.new
ssh temple 'mv .local/bin/xbox-proxy.new .local/bin/xbox-proxy &&
  launchctl kickstart -k gui/$(id -u)/se.brockman.xbox-proxy'
```

To try a change without replacing it, run a copy in front of the installed
proxy, which still supplies the login:

```sh
go run . -local 127.0.0.1:8099 -backend https://xbox.whale-justice.ts.net
UWP_DEVICE_URL=http://127.0.0.1:8099 nix run .#xbox-lease -- status
```

The first enrollment used a single-use auth key. After enrollment the program
reuses its saved identity, so no API key or bootstrap auth key is retained by
the proxy. It does not change temple's normal Tailscale identity.

Verified authenticated HTTP 200 and valid TLS through the hostname, including
a restart after removing and revoking the bootstrap auth key.

The private `xbox-login.json` file in `~/.local/state/xbox-proxy/` stores the
backend credentials with owner-only access. Clients do not supply an Xbox
login: any Tailnet client permitted to reach this node on port 443 can use the
portal. Authentication remains enabled on the Xbox itself.

Verified HTTP 200 for both the portal and package-management API without
client credentials.

## The console lease

Several agents share one console. An install or launch from one replaces the
app another is watching, so the proxy grants one client at a time a lease:

- While a lease is held, every request that changes the console (any method
  but GET, HEAD, and OPTIONS: installs, uninstalls, launches, stops, restarts,
  settings) must carry the holder's token in `X-Xbox-Lease`, or it is refused
  with `423 Locked` and a JSON body naming the holder, its expiry, and the queue.
  The proxy strips the header before forwarding.
- Reads (screenshots, files, processes, dumps) stay open to everyone.
- Clients wait in a first-come queue. `POST /lease/acquire?label=L&ttl=S`
  answers `200` with a token once granted, or `202` with a ticket and a queue
  position; polling again with `ticket=T` within 45 seconds keeps the place.
  `POST /lease/renew?token=T&ttl=S` extends, `POST /lease/release?token=T`
  gives the console back, and `GET /lease` shows the holder and the queue.
- Leases last their ttl (default 10 minutes, at most 30) unless renewed. The
  holder is recorded with its Tailscale login and machine.
- When no one holds the lease, requests pass as they always did, so older
  tools keep working until someone takes it. State is in memory; restarting
  the proxy clears it.

`nix run .#xbox-lease` is the client (`status`, `acquire`, `release`, and `run
-- COMMAND`), and nixbox's deploy takes the lease itself; see
[the development loop](../../docs/development.md).
