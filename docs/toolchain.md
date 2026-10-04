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

The package compiler does not force-include compatibility headers, so configure
probes see only their explicit includes. The application build pipeline applies
its C++/WinRT compatibility fixes separately.

## Shared SDK header fixes

[nix/cxx-headers.nix](../nix/cxx-headers.nix) guards `cstdlib`'s `getenv` and
`system` imports with the same UWP partition condition as the C headers. The
compiler searches this directory ahead of the original SDK. Packages receive
these fixes without custom CXXFLAGS or forced includes; C headers are unchanged.

The adapter is exposed as `pkgsXbox.xboxCxxHeaders`, the `xbox-cxx-headers` flake
output, and `XBOX_CXX_HEADERS` in the development shell. Future shared SDK header
fixes belong here; project-specific changes belong in package recipes.

MSVC's CRT implements exception handling but does not supply the libunwind API.
Both `pkgsXbox.libunwind` and `pkgsXbox.llvmPackages.libunwind` are marked
unsupported through `meta.badPlatforms`. `lib.meta.availableOn` returns false,
and direct build requests fail at evaluation. Native Linux packages are unaffected.

## Existing ports

| Package | Adaptation | Validation |
| --- | --- | --- |
| zlib 1.3.2 | Upstream CMake build; static `zs.lib`; corrected pkg-config paths | AMD64 COFF archive check and Xbox compression round-trip |
| Luau 0.738 | VM and bytecode compiler only; static CRT; availability-based unwinder selection | Wine gameplay test and real Xbox script execution |
| GNU Hello 2.12.3 | UWP-compatible program name, getopt, handle, and file-opening code; NLS disabled | Six behavior checks under Wine |

[nix/luau.nix](../nix/luau.nix) inherits Nixpkgs' native CMake. It builds the VM
and compiler targets and installs their libraries and headers. CLI tools, native
code generation, and upstream executable tests are omitted.

[nix/hello-uwp.patch](../nix/hello-uwp.patch) contains GNU Hello's portability
fixes. The [configure experiment notes](../nix/experiments/hello-configure.txt)
explain the failures that led to them. Hello uses console CRT startup and is
not an Xbox-launchable UWP application.

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
header case aliases. Wine runs `midlrt` and the 32-bit `makepri` resource compiler;
MSXML6 DLLs are extracted and installed in Wine automatically.

Proprietary SDK outputs are marked unfree and explicitly allowed by this flake.
The first uncached build downloads the inputs; later builds reuse Nix store paths
or substitute outputs from the optional authenticated cache.
