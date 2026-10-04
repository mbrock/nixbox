# Working in nixbox

nixbox builds Xbox UWP apps from Linux with Nix: a pinned Windows SDK,
Clang/LLD, a `pkgsXbox` Nixpkgs cross package set, and native packaging and
signing. `example/` is the sample app that runs on a real Xbox Series X in
Developer Mode. The [README](README.md) is the overview; `docs/` has the
reference material on the toolchain, development loop, and binary cache.

## Build, sign, deploy

```sh
./build                                  # Nix build → build-output/
./env ./package                          # sign with the local dev certificate
UWP_DEVICE_URL=https://xbox.whale-justice.ts.net ./env python3 deploy.py --replace
```

Deployment installs, launches, checks the process, and saves a console
screenshot to `build-output/xbox-hello.png`. Look at that screenshot to verify
a change on the console; the sample's status lines report whether each check
passed. `build-output/deployment.json` records the last device and process.

Games are built the main way, without a Visual Studio project or Wine: see
[docs/apps.md](docs/apps.md) and `templates/game/` (`nix build .#game`,
`UWP_DEVICE_URL=… nix run .#deploy-game -- --screenshot build-output/game.png`).
For quick iteration, `nix develop .#game` and then
`make -C templates/game deploy` (its build/ is ignored by git). The XAML sample in `example/` uses the `.vcxproj` route for porting Visual Studio
projects. docs/apps.md also covers reading crash dumps when a launch fails.

Nix flakes only see tracked files: `git add` new files before `./build`.
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
