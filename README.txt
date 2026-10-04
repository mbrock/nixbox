nixbox: Xbox UWP development from Linux with Nix

Requires Nix on an x86_64 Linux host.

Build, sign and deploy the sample:
  ./build
  ./env ./package
  UWP_DEVICE_URL=https://your-xbox.example ./env python3 deploy.py --replace

Edit the C++ app and manifest in example/, then repeat those commands.
All scripts resolve the workspace relative to themselves. No Windows machine,
SDK setup script, pip install, winetricks, or pre-existing Wine prefix is needed.
The current target is an x64 C++ UWP app with the static CRT, tested on Xbox.
This does not yet package Store VCLibs for /MD projects or restore arbitrary
NuGet dependencies declaratively.

Nix entry points:
  nix build .#hello-uwp         Unsigned MSIX and app layout in result/
  nix develop                  Interactive tool environment
  ./env COMMAND...             Run a command in that environment
  nix flake check              Build and validate the complete sample
  nix build .#toolchain         Installed tools, independent of this checkout
  nix build .#zlib-xbox         Static zlib compiled for Xbox's MSVC ABI
  nix build .#hello-xbox        GNU Hello cross-built as a Windows console EXE
  nix build .#luau-xbox         Static Luau VM and bytecode compiler libraries
  nix build .#xbox-cc           Wrapped cross compiler and target binutils
  nix build .#xbox-cxx-headers  Shared adaptations of the pinned SDK's C++ headers

legacyPackages.x86_64-linux.pkgsXbox is a Nixpkgs cross package set, modeled on
Filnix's pkgsFilc. Its host platform is x86_64-pc-windows-msvc with isXbox = true;
replaceCrossStdenv supplies an LLVM compiler with the pinned Windows SDK,
UWP API-family defines and static CRT. buildPackages remains native Linux, so
CMake and Ninja execute on the build machine. nix/xbox-pkgs.nix contains the
compiler wrappers and cross overlays.
flake.nix selects llvmPackages_23 (LLVM 23.1.0 in the current lock) for Clang,
LLD, LLVM inspection tools, and the Xbox LLVM package scope.
The cross stdenv uses Nixpkgs' makeStaticLibraries adapter to select static
libraries in Autoconf, CMake and Meson. This is a build default, not a claim
that Windows lacks DLL support: isStatic remains false. CMake link probes
default to Release across the package set because the SDK has no debug CRT.
Packages can override these settings when necessary; library-specific build
switches and MSVC CRT selection remain separate concerns.
The package compiler does not force-include application compatibility headers,
so configure probes observe their own includes. The UWP app driver still applies
its C++/WinRT compatibility fixes. See nix/experiments/hello-configure.txt for
the GNU Hello configure experiment and the resulting source adaptations.

nix/cxx-headers.nix owns the SDK C++ header adaptations. It currently guards
cstdlib's getenv/system imports with the same UWP partition condition as the
C headers. The cross compiler searches this header directory ahead of the
original SDK, so other pkgsXbox packages receive it automatically, without
package-specific CXXFLAGS or implicit includes. C headers remain unmodified.
pkgsXbox.xboxCxxHeaders and the xbox-cxx-headers flake output expose the package;
XBOX_CXX_HEADERS exposes its path in the development shell. Future SDK header
fixes belong here, while project-specific changes belong in package recipes.
nix/check-compiler.nix verifies ordinary C++ header usage, a closed desktop
CRT partition, and clean negative C/C++ configure probes.

GNU Hello 2.12.3 now configures, builds and installs using the normal cross
stdenv. nix/hello-uwp.patch adapts program-name lookup, getopt environment
handling, the handle inheritance constant and file opening through CreateFile2.
The patched getopt uses its explicit POSIX mode; POSIXLY_CORRECT is not read
from a process environment. The compiler selects LLD directly, without the
experiment's previous package-level linker overrides.

Hello is currently a console executable, validated under Wine. It is not an
Xbox-launchable UWP application: entry points, PE AppContainer flags, CRT/import
routing and MSIX packaging still need the app build path. Its desktop CRT
startup imports must not be mistaken for verified Xbox API compatibility.
To run the behavioral checks after building .#hello-xbox:
  ./env uwp-with-wine python3 nix/experiments/check-hello.py result/bin/hello.exe

Luau 0.738 (gameplay scripting).
pkgsXbox.luau builds the VM and bytecode compiler as MSVC static
libraries, using Nixpkgs' pinned source. CLI tools, unit tests and native code
generation are omitted. The recipe inherits Nixpkgs' native CMake; Ninja is
unnecessary. Luau's own static-CRT option sets the MSVC runtime. Compiler link
probes run in Release because the pinned SDK has no debug CRT. The recipe
removes LLVM libunwind, selects the VM/compiler targets, and installs their
static libraries and headers. SDK header adaptation is owned by the compiler.
Unlike Darwin, the MSVC SDK does not provide the libunwind API, so there is
no dummy compatibility package or libunwind.pc claiming otherwise. Luau's
Clang-based dependency assumption is corrected explicitly in its recipe.
Both pkgsXbox.libunwind and pkgsXbox.llvmPackages.libunwind are marked unsupported
through meta.badPlatforms. lib.meta.availableOn returns false, and direct build
requests fail at evaluation. Native Linux buildPackages is unaffected. Luau
uses this availability check when selecting its unwinder dependency.

Build and run the gameplay smoke test:
  nix build --impure --file nix/experiments/luau-smoke.nix \
    --out-link build-output/luau-smoke
  ./env uwp-with-wine wine "$PWD/build-output/luau-smoke/bin/luau-smoke.exe"
The test compiles a script, registers a native damage callback, executes the
bytecode in a sandboxed VM, and verifies that health becomes 46. This passes
under Wine. The Xbox sample links the same libraries, compiles the script on
the console, and displays health = 46 and native callbacks = 3. PASS requires
both values to match. The displayed health bar is driven by the script result.
The library recipe lives in nix/luau.nix; the experiment entry point now returns
the normal pkgsXbox.luau package.

The first port is static zlib 1.3.2, using Nixpkgs' pinned source and patches
with its upstream CMake build. The installed zs.lib archive is checked for
AMD64 COFF objects. The sample links it with the same static CRT and displays
the result of a compression/decompression round-trip on the Xbox.

This currently demonstrates static library cross compilation. Other packages
may need recipe adaptations; ordinary Windows executables still need UWP
entry points, import checks and app packaging. The sample uses uwp-crossbuild
for those steps. It does not imply every Nixpkgs package can run on Xbox.

For an incremental build outside the Nix sandbox:
  ./env bash -c 'uwp-with-wine uwp-build-project \
    --project "$PWD/example/hello-uwp.vcxproj" --config Release --no-restore \
    --out "$PWD/build-output/incremental-layout" \
    --property ZlibIncludeDir="$XBOX_ZLIB_INCLUDE_DIR" \
    --property ZlibLibrary="$XBOX_ZLIB_LIBRARY" \
    --property LuauRoot="$XBOX_LUAU_ROOT"'
uwp-with-wine starts an authenticated temporary Xvfb display and initializes
Wine automatically. WINEPREFIX overrides its cache location; the default is
${XDG_CACHE_HOME:-$HOME/.cache}/xbox-uwp/wine. Sandboxed Nix builds instead use
a fresh, disposable prefix inside their build directory.

The flake pins:
  - nixpkgs (LLVM 21 and Wine 11.0) in flake.lock
  - uwp-crossbuild 0.5.3 and openappx 0.7.0 as locked source inputs
  - native C++/WinRT 2.0.250303.1 and its winmd dependency
  - MSVC CRT 14.44.35220 (xwin selector 14.44.17.14)
  - Windows SDK 10.0.26100 headers/libraries for compilation
  - Windows SDK 10.0.22621.0 tools, IDL and metadata for Xbox compatibility
  - MSXML6 DLLs, extracted natively and installed automatically in Wine

nix/sdk-files.json and nix/xwin-files.json record exact Microsoft download URLs
and SHA-256 hashes. The SDK is extracted with msiextract on Linux. xwin runs
with a pinned manifest and all payloads supplied by Nix, inside an offline
sandbox. C++/WinRT builds with pre-fetched winmd headers, so CMake never clones
a dependency during compilation. Header case aliases are created in the SDK
derivations. Wine runs only midlrt and the 32-bit makepri resource compiler.

The first uncached build fetches these inputs from Microsoft/GitHub. Subsequent
builds reuse store paths or substitute our built outputs from the personal
cache. No command downloads an SDK into your home directory. Proprietary SDK
outputs are marked unfree and explicitly allowed within this flake.

Authenticated personal Nix cache:
  https://nix.swa.sh/xbox-cache/
  Signing key: xbox-uwp-1:YMEqS8rfsKIOyXplusBbWtNUuKTNikZqw7X/BtW3ijQ=

Caddy serves this endpoint over public HTTPS with Basic Auth. Directory listing
is disabled, and SDK outputs live separately from the public Filnix/LUV caches.
Nix also verifies signatures against the key pinned in the flake. Nothing is
uploaded to Cachix or another public cache.

Cache credentials stay outside the repository and store:
  ${XDG_CONFIG_HOME:-$HOME/.config}/xbox-nix-cache/netrc
./build, ./env and ./cache-publish use this file when present.
UWP_NIX_CACHE_NETRC overrides its location. Absence of credentials still allows
source builds with normal Nix inputs. To use the cache on a cloud agent, provide
a copy of netrc as a secret file (mode 0600), then run:
  UWP_NIX_CACHE_NETRC=/run/secrets/xbox-cache.netrc ./build
  UWP_NIX_CACHE_NETRC=/run/secrets/xbox-cache.netrc ./env
For a direct Nix command:
  nix build --accept-flake-config \
    --option netrc-file /run/secrets/xbox-cache.netrc .#hello-uwp
Only the HTTP credential is needed by cloud agents, never a signing private key.

To publish updated outputs on the cache host:
  ./cache-publish
The publisher signs and uploads the complete runtime closure, so a cloud agent
can substitute the toolchain without running SDK extraction or compiler setup.
It excludes application signing keys and Wine prefixes. UWP_NIX_CACHE_DIR and
UWP_NIX_CACHE_KEY override the destination and binary-cache signing key.
The default destination is /var/www/xbox-nix-cache and the default key is
${XDG_CONFIG_HOME:-$HOME/.config}/xbox-nix-cache/cache.secret.
Keep that secret key outside the repository and Nix store.

Application signing remains local:
  ${XDG_CONFIG_HOME:-$HOME/.config}/uwp-crossbuild/dev.pfx
  ${XDG_CONFIG_HOME:-$HOME/.config}/uwp-crossbuild/dev.cer
UWP_CERT_DIR overrides this directory consistently for packaging and deployment.
./package reuses the existing key, or generates one with private permissions.
Publisher: CN=uwp-crossbuild-dev. The Nix build is deliberately unsigned so the
private key cannot enter the store or binary cache.

Deployment requires UWP_DEVICE_URL, pointing to your Xbox Device Portal with
CA verification. Set UWP_DEVICE_USER and OPENAPPX_DEVICE_PASSWORD if the portal
requires authentication. For example:
  UWP_DEVICE_URL=https://your-xbox.example ./env python3 deploy.py --replace
--replace replaces only this sample with the matching publisher. The public
certificate is installed on the console before installing and launching it.

Local outputs:
  build-output/result/                 Nix output symlink
  build-output/hello-layout/           Writable copy of the app layout
  build-output/hello-uwp.unsigned.msix  Unsigned Nix-built package
  build-output/hello-uwp.msix           Locally signed package
  build-output/xbox-hello.png           Screenshot after deployment
  build-output/deployment.json         Installed package/process evidence

The workspace is a Git repository so Nix includes tracked project files rather
than copying SDK caches, Wine state and build outputs into the source store.
Stage new project files with git add before building them with the flake. The
original uwp-crossbuild/ and openappx/ clones are retained but ignored; builds
use locked upstream inputs, and the editable sample lives in example/.

Verified 2026-10-04:
  - Complete Nix sandbox build using a fresh Wine prefix
  - Build and local signing from a relocated workspace
  - SDK and MSIX downloaded into an empty store through the HTTPS cache
    and verified against the pinned signing key
  - Unauthenticated cache metadata requests rejected with HTTP 401
  - Nix-built app installed and launched on Xbox Series X; Device Portal
    reported hello.exe running and its screenshot shows "Built on Linux"
  - pkgsXbox.zlib built with native Linux CMake/Ninja and the custom MSVC
    stdenv; its static archive contains AMD64 COFF objects
  - Xbox screenshot shows "pkgsXbox zlib 1.3.2: compression round-trip passed"
  - Cross-built zlib and headers downloaded into an empty store through the
    authenticated public cache and verified against its pinned signing key
  - Ported GNU Hello: default/custom/traditional greetings, help, version and
    invalid-option behavior checked under Wine; flake checks pass
  - Luau VM/compiler cross-built; scripted gameplay callback test passes in Wine
  - Luau compiled and executed a gameplay script on Xbox Series X; console
    screenshot shows health = 46, native callbacks = 3 — PASS, and the health bar
