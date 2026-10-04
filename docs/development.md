# Development and verification

[Back to the README](../README.md)

## Build, sign, deploy

```sh
./build                                   # nix build .#hello, with the optional cache
UWP_DEVICE_URL=https://your-xbox.example nix run .#deploy-hello
```

The sample is built by `mkXboxApp` from [hello.nix](../example/hello.nix), like
any game: see [building apps](apps.md). Deployment signs a copy of the package
with a local development certificate, installs it, launches it, checks the
running process, and saves a console screenshot (`--screenshot PATH`; default
`xbox-screenshot.png`).

Set `UWP_DEVICE_USER` and `OPENAPPX_DEVICE_PASSWORD` if Device Portal requires
credentials. HTTPS uses normal CA verification.

## Incremental builds

```sh
nix develop .#hello
make -C example            # CMake build in example/build/
make -C example deploy     # package example/build/install and deploy it
```

The development shell has the same compiler, flags, and `pkgsXbox` libraries as
the Nix build, through the normal Nix dependency mechanism: `buildInputs`
supply include and library paths, and CMake finds zlib and Luau on
`CMAKE_PREFIX_PATH`. A full build takes about ten seconds with the precompiled
header; an edit reaches the console in about fifteen.

Nix flakes use tracked source files. Stage new files with `git add` before
`nix build`.

## The XAML sample

`App` derives from `Windows.UI.Xaml.Application`, so it is a runtimeclass:
[app.idl](../example/app.idl) declares it, and `mkXboxApp`'s `idl` argument
runs it through `midlrt` (under Wine, the one step that needs it) and
`cppwinrt`. The generated `App.g.h` and `module.g.cpp` arrive through
`buildInputs`, and `hello.winmd` goes into the package, where activation
resolves the manifest's `EntryPoint="hello.App"`. `MainPage` is a plain C++
class, so no XAML compiler is involved.

## Direct3D view

The sample embeds a Direct3D 12 composition swap chain in a programmatically
created XAML `SwapChainPanel`. [D3DView.cpp](../example/D3DView.cpp) owns the
hardware device, command queue, depth buffer, mesh, and swap chain, with one
command allocator per back buffer and a fence that keeps the CPU from reusing
an allocator the GPU is still reading. The `d3d12.h` and `d3dx12.h` headers
come from Nixpkgs' `directx-headers` (Microsoft's DirectX-Headers), not the
Windows SDK, which has never shipped `d3dx12.h`. It renders a lit, rotating
cube on XAML frame callbacks on the UI thread, stops callbacks when unloaded,
and resizes buffers for panel size and composition-scale changes. A lost device
at presentation is recreated; other graphics failures appear in the status text.

[Cube.hlsl](../example/Shaders/Cube.hlsl) contains the vertex and pixel shaders.
CMake compiles them with the native DXC (`nixbox_shader`) into headers with
embedded, signed Shader Model 6 bytecode; the `Scene` constants are root
constants of a root signature built in C++. The console needs no runtime
shader compiler.

## Visual Studio projects

[hello-uwp.vcxproj](../example/hello-uwp.vcxproj) describes the same sample
the way Visual Studio keeps it. `nix build .#hello-vcxproj` builds it through
uwp-crossbuild's project reader, as a check that porting a Visual Studio
project still works. Such a project names its dependencies by path, so that
derivation passes them as MSBuild properties; nothing else needs them.

## Signing keys

The deploy tool keeps one development certificate per publisher outside the
repository and the Nix store:

```text
${XDG_CONFIG_HOME:-$HOME/.config}/nixbox/certs/CN_nixbox-dev.pfx
${XDG_CONFIG_HOME:-$HOME/.config}/nixbox/certs/CN_nixbox-dev.cer
```

It creates one with private permissions on first use. Keeping signing outside
the Nix build prevents the private key from entering the store or binary cache.

## Checks

```sh
nix flake check
```

The flake checks compile and link ordinary C++ headers and exception handling,
verify the UWP CRT partition and negative configure probes, inspect zlib's
archive format, and build Hello, Luau, the sample both ways, and the game
template. Target runtime checks run separately under Wine or on the console.

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
smoke test. The sample, built by `mkXboxApp` with CMake, was signed, installed,
and launched on Xbox Series X; its screenshot shows the Direct3D 12 cube,
successful zlib compression, and **health = 46, native callbacks = 3 — PASS**.
The game template was deployed the same way and from its development shell.

Earlier checks also exercised relocated workspaces, fresh Wine prefixes, and
empty-store substitution with signature verification. Cache requests without
credentials returned HTTP 401. Build success alone is not a runtime check;
the Xbox result was checked through Device Portal and its screenshot.
