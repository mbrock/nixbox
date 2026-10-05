# Working in nixbox

nixbox builds Xbox UWP apps from Linux with Nix: a pinned Windows SDK,
Clang/LLD, a `pkgsXbox` Nixpkgs cross package set, and native packaging and
signing. `example/` is the sample app that runs on a real Xbox Series X in
Developer Mode. The [README](README.md) is the overview; `docs/` has the
reference material on the toolchain, development loop, and binary cache.

## Build, sign, deploy

```sh
export UWP_DEVICE_URL=https://xbox.whale-justice.ts.net
./build                                  # nix build .#hello, with the cache
nix run .#deploy-hello -- --screenshot build-output/hello.png
nix develop .#hello -c make -C example deploy   # incremental
```

The console is shared with other agents, so changing it goes through a lease
(`tools/xbox-proxy`): deploy waits its turn, takes the console, and keeps it for
`--hold` seconds (default 300) after launching so you can watch your app.
Release it as soon as you are done with `nix run .#xbox-lease -- release`, wrap
a longer session in `nix run .#xbox-lease -- run --ttl 900 -- COMMAND...`
(which sets `XBOX_LEASE` for the deploys inside it), and check who has it with
`nix run .#xbox-lease -- status`. A `423 Locked` answer means someone else
holds the console: wait for it rather than working around the lease.

Deployment signs, installs, launches, checks the process, and saves a console
screenshot. Look at that screenshot to verify a change on the console; the
sample's status lines report whether each check passed. The game template
works the same way (`.#game`, `deploy-game`, `make -C templates/game deploy`).
Both are built by `mkXboxApp` ([docs/apps.md](docs/apps.md)), which also
covers reading crash dumps when a launch fails. `nix build .#hello-vcxproj`
checks the Visual Studio project route.

Nix flakes only see tracked files: `git add` new files before `nix build`.
`nix flake check` runs the compiler, library, and app checks.

## Working rhythm

Prefer small experiments and useful save points to ceremony. Preserve
unrelated work. Run the quick relevant checks (a build, and a deploy when the
console behavior is what changed), then commit and push `origin main` without
asking unless the work is explicitly on another branch. A pushed commit is a
durable iteration, not a claim that the experiment is finished.

Keep the README and `docs/` in step with what the sample actually does, and
refresh `docs/assets/xbox-demo.png` from a new deployment screenshot when the
visible app changes.
