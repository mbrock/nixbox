# Building apps

[Back to the README](../README.md)

A game is an ordinary Nix derivation built by the same cross compiler as the
`pkgsXbox` libraries. nixbox adds the few link settings an Xbox executable
needs, writes the package manifest, and packs an MSIX. No Visual Studio
project, Wine, or Windows machine is involved: shaders are compiled by the
native Linux DXC, which signs its DXIL.

```sh
mkdir my-game && cd my-game
nix flake init -t github:mbrock/nixbox#game
git init && git add .
nix build                                 # result/game.msix and result/layout/
UWP_DEVICE_URL=https://your-xbox.example nix run .#deploy
```

The template is a full-screen Direct3D 12 cube on `CoreApplication`, built
with Meson, which also compiles its HLSL with DXC; the left stick spins it.
Its [game.nix](../templates/game/game.nix) is the whole description:

```nix
xbox.mkXboxApp {
  pname = "game";
  version = "0.1.0";
  src = ./.;
  nativeBuildInputs = with xbox.pkgs; [ meson ninja directx-shader-compiler ];
  buildInputs = [ xbox.pkgsXbox.zlib ];   # any pkgsXbox libraries
}
```

`xbox` is `nixbox.lib.<system>`, for `x86_64-linux` or `aarch64-darwin`;
the template's flake provides both. Everything here works the same on an Apple
silicon Mac except `idl`, which runs `midlrt` under Wine and so needs Linux. In this repository, `nix build .#game`
builds the template and `nix run .#deploy-game` deploys it.

## Hacking in a dev shell

`nix build` is the reproducible build; day to day, build incrementally in the
game's dev shell:

```sh
nix develop
make                  # meson setup build --cross-file xbox, then meson compile
make deploy           # install to build/install, package, deploy, screenshot
```

The shell has the same compiler, flags, and libraries as `nix build`, set up
for each build system (see below). Two commands package the output with the
same steps as the Nix build:

- `xbox-package [PREFIX] [OUT]` packages an install prefix (default
  `build/install`) into `OUT` (default `build/package`);
- `xbox-deploy [ARGS…]` runs `xbox-package` with the defaults, then deploys
  `build/package`, taking the deploy options below.

The template's Makefile is three lines around Meson; any build system that
installs `bin/<pname>.exe` into a prefix works the same way. After an edit,
`make deploy` shows the change on the console in about fifteen seconds, most
of it installing.

## Build systems

nixbox configures each build system for the Xbox, in `nix build` and in the
dev shell alike. In all of them, `pkgsXbox` libraries in `buildInputs` are on
the compiler's include and library paths.

**Meson** (the template). The dev shell has a cross file named `xbox`, so
`meson setup build --cross-file xbox` targets the console; Nix builds pass the
same file, as `release`. It keeps Meson on the compiler's static CRT and away
from desktop default libraries. Link SDK libraries by name
(`link_args: ['-ld3d12']`). For `dependency()`, add
`xbox.pkgsXbox.buildPackages.pkg-config` to `nativeBuildInputs`; zlib, for
one, is then found through its `.pc` file. Compile shaders with a
`custom_target` around `dxc`, as the template's `meson.build` does.

**CMake** ([the XAML sample](../example/CMakeLists.txt)).
`CMAKE_TOOLCHAIN_FILE` (also `xbox.toolchainFile`) points `cmake -B build` at
the console, selects the static CRT, and drops the desktop default
libraries; `find_library` and `find_package` search `buildInputs`. It also
puts nixbox's CMake modules on the module path, for `nixbox_shader` below.

**Anything else.** `CC` and `CXX` are the cross compilers, already carrying
the SDK, DirectX-Headers, and link settings.

## mkXboxApp

Takes any `mkDerivation` arguments, plus:

| Argument | Default | Meaning |
| --- | --- | --- |
| `identity` | `Nixbox.<pname>` | Package name; installs replace the same name |
| `publisher` | `CN=nixbox-dev` | Signing certificate subject |
| `displayName` | `pname` | Name in the console's app list |
| `executable` | `<pname>.exe` | The executable the package launches |
| `capabilities` | `[ "internetClient" ]` | Manifest capabilities |
| `backgroundColor` | `#000000` | Tile and splash colour |
| `assets` | placeholder logos | Directory with the four logo PNGs |
| `manifest` | generated | Your own `AppxManifest.xml` instead |
| `idl` | none | `.idl` declaring a XAML `Application` runtimeclass |
| `entryPoint` | `App` | Manifest entry point, e.g. `hello.App` with `idl` |

With `idl`, nixbox runs it through `midlrt` (under Wine, the one step that
needs it) and `cppwinrt`; the generated `App.g.h`, `module.g.cpp`, and
projection headers arrive through `buildInputs`, and `<namespace>.winmd` goes
into the package. [example/hello.nix](../example/hello.nix) is a XAML app
built this way.

The build installs its executable into `bin/` (CMake's default) and any data
into `share/<pname>/`; both become the package root. PDB files are moved to
`result/symbols/` rather than packed. The result's `app` attribute is the
underlying build.

Compared with the library compiler, the app compiler:

- puts [DirectX-Headers](https://github.com/microsoft/DirectX-Headers) first on
  the include path, so `d3d12.h` and `d3dx12.h` match;
- links `WindowsApp.lib` and the AppContainer image flag, plus two small import
  libraries that resolve a few CRT and unwinder functions from DLLs the Xbox
  app container actually has;
- configures CMake and Meson as described above.

Packaging then sets the GUI subsystem at version 6.2 and audits the
executable's imports, failing builds the console would refuse to launch.

## Shaders in CMake

The toolchain file puts nixbox's CMake modules on `CMAKE_MODULE_PATH`:

```cmake
include(NixboxShaders)
nixbox_shader(game Cube.hlsl cubeVertexShader vertexMain vs_6_0)
```

compiles one entry point with DXC into a header defining the byte array
`cubeVertexShader`, on the target's include path. Add
`directx-shader-compiler` to `nativeBuildInputs`.

## compileShaders

```nix
xbox.compileShaders {
  name = "game-shaders";
  src = ./Cube.hlsl;
  entries = [ { name = "cubeVertexShader"; entry = "vertexMain"; profile = "vs_6_0"; } ];
}
```

Each entry becomes `include/<name>.h` with a byte array named `<name>`. Put the
result in `buildInputs` to put it on the include path. This suits build
systems without their own shader step; with CMake, `nixbox_shader` rebuilds
them incrementally in the dev shell.

## Deploying

`mkDeploy package` makes a `nix run` app; `nix run github:mbrock/nixbox#deploy
-- PATH` deploys any built package. Either one:

1. creates a development certificate for the package's publisher on first use,
   in `~/.config/nixbox/certs/`, and signs a copy of the package;
2. replaces an installed package of the same name and publisher;
3. installs, launches, checks the process, and saves a screenshot
   (`--screenshot`, default `xbox-screenshot.png`); `--crash-dumps` also has
   the console keep a dump if the app crashes.

The console comes from `--device`, `UWP_DEVICE_URL`, or
`~/.config/nixbox/device`. `UWP_DEVICE_USER` and `OPENAPPX_DEVICE_PASSWORD`
supply Device Portal credentials.

## When a launch fails

Device Portal reports `0x8027025B` for any app that dies during startup. The
template writes an error escaping the game to `LocalState/error.txt`, which
Device Portal's file explorer shows. For a crash dump, deploy with
`--crash-dumps`, then list and download the dumps:

```sh
nix run .#deploy -- --crash-dumps
P=Nixbox.game_0.1.0.0_x64__…            # PackageFullName from the deploy output
curl "$UWP_DEVICE_URL/api/debug/dump/usermode/dumps"
curl -o game.dmp "$UWP_DEVICE_URL/api/debug/dump/usermode/crashdump?packageFullName=$P&fileName=…"
```

LLDB opens the dump (`lldb -c game.dmp`).

The template's and the sample's Makefiles build with debug info, so `build/package/symbols/`
has the PDB; for a Nix build, use `mesonBuildType = "debugoptimized"` (or
`cmakeFlags = [ "-DCMAKE_BUILD_TYPE=RelWithDebInfo" ]`) and `dontStrip = true`.
`llvm-symbolizer --obj=game.exe`
then turns the dump's stack addresses, rebased to `0x140000000`, into source
lines.

Learned the hard way: C++/WinRT's `init_apartment()` must come before
`CoreApplication::Run`, or the run fails with `0x8001010E`.
