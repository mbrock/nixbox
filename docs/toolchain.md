# Toolchain and package ports

[Back to the README](../README.md)

## The cross package set

`legacyPackages.x86_64-linux.pkgsXbox` targets `x86_64-pc-windows-msvc`, with
`isXbox = true`. Its custom stdenv supplies Clang, LLD, the pinned Windows SDK,
UWP API-family defines, and the static Microsoft CRT. `buildPackages` remains
native Linux, so CMake and Ninja execute on the build machine.

[flake.nix](../flake.nix) selects `llvmPackages_23` centrally: LLVM **23.1.0** in
the current lock. The same selection supplies the application tools, cross
compiler, LLVM inspection tools, and Xbox LLVM package scope.

[nix/xbox-pkgs.nix](../nix/xbox-pkgs.nix) owns the compiler wrappers and overlays.
It uses Nixpkgs' `makeStaticLibraries` adapter for Autoconf, CMake, and Meson
static-library defaults. `hostPlatform.isStatic` remains false: Windows supports
DLLs. CRT linkage and library-specific build switches are separate settings.
CMake link probes default to Release because the pinned SDK has no debug CRT.
CMake also defaults to policy CMP0091 NEW and the `MultiThreaded` static CRT;
packages may explicitly override these defaults, but must then supply their
runtime requirements.

The package compiler does not force-include compatibility headers, so configure
probes see only their explicit includes. The application build pipeline applies
its C++/WinRT compatibility fixes separately.

## Shared SDK header fixes

[nix/cxx-headers.nix](../nix/cxx-headers.nix) guards `cstdlib`'s `getenv` and
`system` imports with the same UWP partition condition as the C headers. The
compiler searches this directory ahead of the original SDK. Packages receive
these fixes without custom CXXFLAGS or forced includes; C headers are unchanged.

The adapter is exposed as `pkgsXbox.xboxCxxHeaders`, and the `xbox-cxx-headers` flake
output. Future shared SDK header
fixes belong here; project-specific changes belong in package recipes.

MSVC's CRT implements exception handling but does not supply the libunwind API.
Both `pkgsXbox.libunwind` and `pkgsXbox.llvmPackages.libunwind` are marked
unsupported through `meta.badPlatforms`. `lib.meta.availableOn` returns false,
and direct build requests fail at evaluation. Native Linux packages are unaffected.

## Existing ports

| Package | Adaptation | Validation |
| --- | --- | --- |
| zlib 1.3.2 | Upstream CMake build; static `zs.lib` plus `z.lib` discovery alias; corrected pkg-config and static CMake targets | AMD64 COFF archive check and Xbox compression round-trip |
| Luau 0.738 | VM and bytecode compiler only; static CRT; availability-based unwinder selection | Wine gameplay test and real Xbox script execution |
| GNU Hello 2.12.3 | UWP-compatible program name, getopt, handle, and file-opening code; NLS disabled | Six behavior checks under Wine |
| SDL3 3.4.16 | Nixpkgs recipe overridden with the UWP fork and C++/WinRT patch; static CRT and library | SDL template build and real Xbox rendering/gamepad input |
| Box2D 3.1.1 | Static library, scalar math, no desktop samples | Native Release physics tests and real Xbox Ricochet collisions/scoring/reset |
| ImGui | SDL3 + SDLRenderer3 backends, scalar math; no GLFW/OpenGL/GPU | Inspected native rendering and real Xbox Ricochet HUD |

[nix/arcade-libraries.nix](../nix/arcade-libraries.nix) owns the Box2D and ImGui
adaptations. [nix/supertux-libraries.nix](../nix/supertux-libraries.nix) owns the
image/text/audio/filesystem dependency ports. Both are applied by `mkPkgsXbox`,
so games and external consumers share the same recipes. See the
[game guide](games.md) for their limitations and Xbox runtime verification status.

[nix/luau.nix](../nix/luau.nix) inherits Nixpkgs' native CMake. It builds the VM
and compiler targets and installs their libraries and headers. CLI tools, native
code generation, and upstream executable tests are omitted.

[nix/sdl3.nix](../nix/sdl3.nix) uses Nixpkgs' feature switches and `overrideAttrs`,
not a separate derivation. Upstream SDL no longer includes UWP, so the source
remains pinned to XboxEmulationHub's fork. The override disables desktop-only
features and executable tests, and installs CMake metadata into the development
output alongside the headers and pkg-config file.

[nix/hello-uwp.patch](../nix/hello-uwp.patch) contains GNU Hello's portability
fixes. The [configure experiment notes](../nix/experiments/hello-configure.txt)
explain the failures that led to them. Hello uses console CRT startup and is
not an Xbox-launchable UWP application.

## NXT coroutine runtime

`pkgsXbox.nxtrt-iocp` builds the pinned [NXT](https://github.com/mbrock/nxtui)
C++23 coroutine core and its Windows/UWP IOCP backend using NXT's own package
recipe. The source input is non-flake, avoiding a dependency cycle with NXT's
own nixbox cross-build input.

```sh
nix build .#nxtrt-iocp-xbox
```

The package installs `nxtrt-iocp.lib`, runtime headers, the `nxtrt-iocp`
pkg-config target, and `iocp-tests.exe`. Its build also cross-links a consumer
against the installed headers and pkg-config metadata; `nix flake check`
includes this build. Target executables are not run on the Linux build host.

This pin contains the runtime only, not yet the DNS/HTTP/TLS transport or the
graphical UI backend. Those are being ported in NXT before integration into
an Xbox agent-chat app.

## Pinned inputs

| Component | Version |
| --- | --- |
| LLVM / Clang / LLD | 23.1.0 |
| Wine | 11.0 |
| uwp-crossbuild | 0.5.3, locked source revision |
| openappx | 0.7.0, locked source revision |
| C++/WinRT | 2.0.250303.1, with a locked winmd dependency |
| MSVC CRT | 14.44.35220; xwin selector 14.44.17.14 |
| Windows SDK headers and libraries | 10.0.26100 |
| Windows SDK tools, IDL, and metadata | 10.0.22621.0, for Xbox compatibility |

[flake.lock](../flake.lock) pins Nixpkgs and the source inputs.
[sdk-files.json](../nix/sdk-files.json) and [xwin-files.json](../nix/xwin-files.json)
record Microsoft download URLs and SHA-256 hashes.

SDK extraction runs natively on Linux using `msiextract`. xwin receives a pinned
manifest and all payloads from Nix, then runs offline inside the sandbox.
C++/WinRT builds natively with pre-fetched winmd headers. SDK derivations create
header case aliases. Wine runs only `midlrt`, for apps with a XAML `Application` class, and the
32-bit `makepri` resource compiler on the Visual Studio project route;
shaders compile with the native DXC;
MSXML6 DLLs are extracted and installed in Wine automatically.

Proprietary SDK outputs are marked unfree and explicitly allowed by this flake.
The first uncached build downloads the inputs; later builds reuse Nix store paths
or substitute outputs from the optional authenticated cache.
