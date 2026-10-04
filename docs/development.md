# Development and verification

[Back to the README](../README.md)

## Build, sign, deploy

```sh
./build
./env ./package
UWP_DEVICE_URL=https://your-xbox.example ./env python3 deploy.py --replace
```

`./build` produces an unsigned package in Nix and refreshes the writable layout.
`./package` signs locally. Deployment installs the public certificate, installs
the app, launches it, checks the running process, and saves a console screenshot.
`--replace` checks that an installed sample has the expected publisher before
removing it.

Set `UWP_DEVICE_USER` and `OPENAPPX_DEVICE_PASSWORD` if Device Portal requires
credentials. HTTPS uses normal CA verification.

| Output | Location |
| --- | --- |
| Nix build result | `build-output/result/` |
| Writable app layout | `build-output/hello-layout/` |
| Unsigned package | `build-output/hello-uwp.unsigned.msix` |
| Signed package | `build-output/hello-uwp.msix` |
| Console screenshot | `build-output/xbox-hello.png` |
| Installed package and process information | `build-output/deployment.json` |

## Incremental builds

For a faster iteration outside the Nix sandbox:

```sh
./env bash -c 'uwp-with-wine uwp-build-project \
  --project "$PWD/example/hello-uwp.vcxproj" --config Release --no-restore \
  --out "$PWD/build-output/incremental-layout" \
  --property ZlibIncludeDir="$XBOX_ZLIB_INCLUDE_DIR" \
  --property ZlibLibrary="$XBOX_ZLIB_LIBRARY" \
  --property LuauRoot="$XBOX_LUAU_ROOT" \
  --property ShaderIncludeDir="$XBOX_SHADER_INCLUDE_DIR"'
```

This writes a separate layout; the standard signing/deployment scripts use
`build-output/hello-layout/`. Run `./build` before those scripts to deploy the
normal Nix-built sample.

`uwp-with-wine` starts an authenticated temporary Xvfb display and initializes
Wine automatically. Set `WINEPREFIX` to change its cache location; the default
is `${XDG_CACHE_HOME:-$HOME/.cache}/xbox-uwp/wine`. Sandboxed Nix builds use a
fresh, disposable prefix in their build directory.

Nix flakes use tracked source files. Stage new files with `git add` before
building, so they become part of the flake source.

## Direct3D view

The sample embeds a Direct3D 12 composition swap chain in a programmatically
created XAML `SwapChainPanel`. [D3DView.cpp](../example/D3DView.cpp) owns the
hardware device, command queue, depth buffer, mesh, and swap chain, with one
command allocator per back buffer and a fence that keeps the CPU from reusing
an allocator the GPU is still reading. It renders a lit, rotating
cube on XAML frame callbacks on the UI thread, stops callbacks when unloaded,
and resizes buffers for panel size and composition-scale changes. A lost device
at presentation is recreated; other graphics failures appear in the status text.

[Cube.hlsl](../example/Shaders/Cube.hlsl) contains the vertex and pixel shaders.
Nix compiles them with the pinned SDK's `fxc` under Wine, generating headers
with embedded Shader Model 5 bytecode, which Direct3D 12 accepts alongside a
root signature built in C++ (the `Scene` constants are root constants). The console runs only the compiled
shaders; it needs no runtime shader compiler. `nix build .#cube-shaders` builds
the headers separately, and `XBOX_SHADER_INCLUDE_DIR` supplies their location
for incremental builds. Editing HLSL and rerunning `./build` recompiles it.

## Signing keys

The development certificate and private key live outside the repository:

```text
${XDG_CONFIG_HOME:-$HOME/.config}/uwp-crossbuild/dev.pfx
${XDG_CONFIG_HOME:-$HOME/.config}/uwp-crossbuild/dev.cer
```

`UWP_CERT_DIR` overrides the directory for both packaging and deployment.
`./package` reuses the key or creates one with private permissions. The publisher
is `CN=uwp-crossbuild-dev`. Keeping signing outside the Nix build prevents the
private key from entering the store or binary cache.

## Checks

```sh
nix flake check
```

The flake checks compile and link ordinary C++ headers and exception handling,
verify the UWP CRT partition and negative configure probes, inspect zlib's
archive format, and build Hello, Luau, and the packaged app. Target runtime checks
run separately under Wine or on the console.

Run GNU Hello's Wine checks:

```sh
nix build .#hello-xbox
./env uwp-with-wine python3 nix/experiments/check-hello.py result/bin/hello.exe
```

Run the Luau gameplay smoke test:

```sh
nix build --impure --file nix/experiments/luau-smoke.nix \
  --out-link build-output/luau-smoke
./env uwp-with-wine wine "$PWD/build-output/luau-smoke/bin/luau-smoke.exe"
```

The smoke test compiles a script, registers a native damage callback, executes
bytecode in a sandboxed VM, and checks that health becomes 46. The Xbox app runs
the same kind of check with three callbacks; its progress bar uses the script's
returned health.

## Verified on 2026-10-04

LLVM 23.1.0 passed the full flake build checks, GNU Hello's six Wine behavior
checks, C++ exception unwinding with destructor cleanup, and the Luau gameplay
smoke test. The rebuilt app was signed, installed, and launched on Xbox Series X.
The Direct3D addition was also built, deployed, and observed rendering on the
hardware device. Two console screenshots show different cube orientations.
The screenshot shows successful zlib compression and **health = 46, native
callbacks = 3 — PASS**.

Earlier checks also exercised relocated workspaces, fresh Wine prefixes, and
empty-store substitution with signature verification. Cache requests without
credentials returned HTTP 401. Build success alone is not a runtime check;
the Xbox result was checked through Device Portal and its screenshot.
