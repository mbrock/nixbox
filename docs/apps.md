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
with CMake, which also compiles its HLSL with DXC; the left stick spins it.
Its [game.nix](../templates/game/game.nix) is the whole description:

```nix
xbox.mkXboxApp {
  pname = "game";
  version = "0.1.0";
  src = ./.;
  nativeBuildInputs = with xbox.pkgs; [ cmake ninja directx-shader-compiler ];
  buildInputs = [ xbox.pkgsXbox.zlib ];   # any pkgsXbox libraries
}
```

`xbox` is `nixbox.lib.x86_64-linux`. In this repository, `nix build .#game`
builds the template and `nix run .#deploy-game` deploys it.

## Hacking in a dev shell

`nix build` is the reproducible build; day to day, build incrementally in the
game's dev shell:

```sh
nix develop
make                  # cmake -B build, then cmake --build build
make deploy           # install to build/install, package, deploy, screenshot
```

The shell has the same compiler, flags, and libraries as `nix build`.
`CMAKE_TOOLCHAIN_FILE` points CMake at the Xbox, so `cmake -B build` needs no
options, and `CC`/`CXX` are the cross compilers for plain Makefiles. Two
commands package the output with the same steps as the Nix build:

- `xbox-package [PREFIX] [OUT]` packages an install prefix (default
  `build/install`) into `OUT` (default `build/package`);
- `xbox-deploy [ARGS…]` runs `xbox-package` with the defaults, then deploys
  `build/package`, taking the deploy options below.

The template's Makefile is three lines around CMake; any build system that
installs `bin/<pname>.exe` into a prefix works the same way. After an edit,
`make deploy` shows the change on the console in about fifteen seconds, most
of it installing.

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
- for CMake, uses a toolchain file (also `xbox.toolchainFile`) that selects
  the static CRT and drops the desktop default libraries.

Packaging then sets the GUI subsystem at version 6.2 and audits the
executable's imports, failing builds the console would refuse to launch.

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
systems without their own shader step; the template compiles shaders in
CMake instead, so they rebuild incrementally in the dev shell.

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

The dev shell's Makefile builds with debug info, so `build/package/symbols/`
has the PDB; for a Nix build, use `cmakeFlags = [
"-DCMAKE_BUILD_TYPE=RelWithDebInfo" ]` and `dontStrip = true`.
`llvm-symbolizer --obj=game.exe`
then turns the dump's stack addresses, rebased to `0x140000000`, into source
lines.

Learned the hard way: C++/WinRT's `init_apartment()` must come before
`CoreApplication::Run`, or the run fails with `0x8001010E`.
