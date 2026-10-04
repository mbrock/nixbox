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
with CMake; the left stick spins it. Its [game.nix](../templates/game/game.nix)
is the whole description:

```nix
xbox.mkXboxApp {
  pname = "game";
  version = "0.1.0";
  src = ./.;
  nativeBuildInputs = [ xbox.pkgs.cmake xbox.pkgs.ninja ];
  buildInputs = [ shaders xbox.pkgsXbox.zlib ];
}
```

`xbox` is `nixbox.lib.x86_64-linux`. In this repository, `nix build .#game`
builds the template and `nix run .#deploy-game` deploys it.

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
- for CMake, selects the static CRT and drops the desktop default libraries.

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
result in `buildInputs` to put it on the include path.

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

Build with `cmakeFlags = [ "-DCMAKE_BUILD_TYPE=RelWithDebInfo" ]` and
`dontStrip = true` for `result/symbols/*.pdb`; `llvm-symbolizer --obj=game.exe`
then turns the dump's stack addresses, rebased to `0x140000000`, into source
lines.

Learned the hard way: C++/WinRT's `init_apartment()` must come before
`CoreApplication::Run`, or the run fails with `0x8001010E`.
