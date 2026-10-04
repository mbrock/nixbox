# nixbox

**Build Xbox UWP apps from Linux with Nix.**

nixbox brings together a pinned Windows SDK, Clang/LLD, and native packaging
and signing tools. Nix handles the downloads and setup; you write C++ and deploy
the result to an Xbox in Developer Mode.

The included app runs **Luau gameplay scripts on a real Xbox Series X**, calls
back into C++, checks a zlib compression round-trip, and renders a **rotating Direct3D 12 cube**
inside a XAML `SwapChainPanel`. Built with LLVM 23,
packaged and signed on Linux.

![The Linux-built app running on Xbox, with a Direct3D cube and successful zlib and Luau checks](docs/assets/xbox-demo.png)

## Try it

You'll need:

- An **x86_64 Linux machine** with Nix and flakes enabled.
- An Xbox with **Developer Mode enabled** and Device Portal reachable from your
  machine. Building the app doesn't require a console.

```sh
git clone https://github.com/mbrock/nixbox.git
cd nixbox

# Build the app and its unsigned MSIX package.
./build

# Create a local development certificate and sign the package.
./env ./package

# Install and launch it on your Xbox.
export UWP_DEVICE_URL=https://your-xbox.example
./env python3 deploy.py --replace
```

Use your console's Device Portal URL. HTTPS certificate verification is enabled;
if the portal requires authentication, set `UWP_DEVICE_USER` and
`OPENAPPX_DEVICE_PASSWORD` too. `--replace` replaces the installed sample after
checking its publisher.

The first build downloads the pinned SDK and toolchain inputs. No Windows
machine, manual SDK installation, pip setup, or existing Wine prefix is needed.
The optional [authenticated binary cache](docs/cache.md) speeds up subsequent
builds on other machines; you can build from source without access to it.

Edit [example/MainPage.cpp](example/MainPage.cpp), then repeat the build, sign,
and deploy steps. Packages, app layouts, and deployment screenshots go into
`build-output/`. See [development](docs/development.md) for incremental builds
and signing details.

## Make your own game

```sh
nix flake init -t github:mbrock/nixbox#game
nix build
UWP_DEVICE_URL=https://your-xbox.example nix run .#deploy
```

The template is a full-screen Direct3D 12 game built with CMake. `mkXboxApp`
takes an ordinary derivation, using any build system and any `pkgsXbox`
libraries, and produces an installable package. No Visual Studio project or
Wine is involved. See [building apps](docs/apps.md).

## Nixpkgs, targeting Xbox

`pkgsXbox` is a Nixpkgs cross package set with a custom MSVC/UWP toolchain.
Libraries compile into Windows COFF archives while build tools run on Linux.
Static libraries are the default, and SDK header fixes are shared by the compiler.

You can use the package set in another flake:

```nix
# inputs.nixbox.url = "github:mbrock/nixbox";
pkgsXbox = inputs.nixbox.legacyPackages.x86_64-linux.pkgsXbox;

# Use normal Nixpkgs package recipes with the Xbox stdenv.
myLibrary = pkgsXbox.callPackage ./my-library.nix { };
```

The sample links `pkgsXbox.zlib` and `pkgsXbox.luau`. These are real cross-built
libraries, using Nixpkgs' pinned sources with small target-specific adaptations.
See [the toolchain](docs/toolchain.md) for how the package set works and where
to add ports.

| Command | Result |
| --- | --- |
| `./env` | Development shell |
| `./env COMMAND…` | Run a tool in that environment |
| `nix flake check` | Compiler, library, and app build checks |
| `nix build .#toolchain` | Standalone build and packaging tools |
| `nix build .#luau-xbox` | Static Luau VM and bytecode compiler |
| `nix build .#zlib-xbox` | Static zlib and headers |
| `nix build .#game` | The game template's package |
| `nix run .#deploy-game` | Deploy it to the console |
| `nix build .#cube-shaders` | HLSL compiled into embedded shader bytecode |
| `nix build .#xbox-cc` | Wrapped Clang cross compiler and binutils |

## What's working—and what's next

Verified on Xbox Series X: the C++/WinRT app launches, Direct3D renders an
animated cube on the hardware device, zlib round-trips data,
and Luau compiles and executes a script with three native callbacks. The script
returns **health = 46**, which drives the on-screen health bar. Wine checks also
cover C++ exception unwinding and a port of GNU Hello.

This is an experimental **x64 UWP toolchain using the static Microsoft CRT**.
New Nixpkgs packages may need portability fixes. A cross-built Windows executable
still needs UWP entry points and packaging to become an Xbox app; GNU Hello is
currently a Wine-tested console executable. Dynamic-CRT VCLibs packaging and
arbitrary NuGet dependency restoration aren't implemented yet.

## Built on

- [uwp-crossbuild](https://github.com/gianlucamazza/uwp-crossbuild) for the UWP
  build pipeline and the original C++ example.
- [openappx](https://github.com/gianlucamazza/openappx) for MSIX packaging,
  signing, and Device Portal deployment.
- [Nixpkgs](https://github.com/NixOS/nixpkgs), LLVM, C++/WinRT, and xwin for the
  compiler, cross package set, and SDK preparation.

The repo contains source and download metadata; Microsoft SDK binaries are
fetched separately by Nix. See [third-party notices](THIRD_PARTY_NOTICES).
