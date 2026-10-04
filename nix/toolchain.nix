{
  pkgs,
  llvmPackages,
  inputs,
  pkgsXbox,
}:
let
  inherit (pkgs) lib;
  # Proprietary inputs are explicit, hash-pinned downloads from Microsoft.
  # These are intended for the owner's authenticated cache, not public upload.
  sdkFiles = builtins.fromJSON (builtins.readFile ./sdk-files.json);
  xwinFiles = builtins.fromJSON (builtins.readFile ./xwin-files.json);
  fetch =
    file:
    pkgs.fetchurl {
      inherit (file) url hash;
      name = builtins.baseNameOf (lib.replaceStrings [ " " ] [ "-" ] (file.name or file.path));
    };
  channel = builtins.fromJSON (builtins.readFile ./xwin-channel.json);
  manifestPayload = builtins.head (builtins.head channel.channelItems).payloads;
  vsManifest = pkgs.fetchurl {
    inherit (manifestPayload) url;
    # Pin the actual JSON bytes (also checked against the original xwin cache).
    # The channel's advertised checksum differs from Microsoft's served file.
    hash = "sha256-8KUOoVciLCmr1epv8Bv8PDOwTgEcXkXuLKOO8HeOVkM=";
    name = "VisualStudio.vsman";
  };
  xwinRoot =
    pkgs.runCommand "uwp-xwin"
      {
        nativeBuildInputs = [
          pkgs.xwin
          pkgs.python3
        ];
        meta.license = lib.licenses.unfree;
      }
      ''
            mkdir -p cache/dl
            cp ${./xwin-channel.json} cache/dl/manifest_17.json
            ln -s ${vsManifest} cache/dl/pkg_manifest_${lib.toLower manifestPayload.sha256}.vsman
            ${lib.concatMapStringsSep "\n" (file: ''
              mkdir -p "cache/dl/$(dirname '${file.path}')"
              ln -s ${fetch file} 'cache/dl/${file.path}'
            '') xwinFiles}
            # All downloads are pre-populated. The normal Nix sandbox keeps this offline.
        xwin --accept-license --json --log-level warn --arch x86_64 --sdk-version 10.0.26100 \
          --crt-version 14.44.17.14 --cache-dir cache \
          --manifest ${./xwin-channel.json} splat --copy --output "$out"
            bash ${inputs.uwp-crossbuild}/scripts/fix-header-case.sh \
              "$out/sdk/include/cppwinrt/winrt" --canonical
            grep -q '#define CPPWINRT_VERSION "2.0.250303.1"' \
              "$out/sdk/include/cppwinrt/winrt/base.h"
      '';
  sdk =
    pkgs.runCommand "uwp-sdk"
      {
        nativeBuildInputs = [ pkgs.msitools ];
        meta.license = lib.licenses.unfree;
      }
      ''
        mkdir -p installers extracted "$out"
        ${lib.concatMapStringsSep "\n" (file: "ln -s ${fetch file} 'installers/${file.name}' ") sdkFiles}
        for msi in installers/*.msi; do
          msiextract -C extracted "$msi" > /dev/null
        done
        cp -a 'extracted/Program Files/Windows Kits' "$out/"
        for dir in winrt shared um; do
          cd "$out/Windows Kits/10/Include/10.0.22621.0/$dir"
          for file in *; do
            lower="''${file,,}"
            if [[ "$file" != "$lower" && ! -e "$lower" ]]; then
              ln -s "$file" "$lower"
            fi
          done
        done
        test -f "$out/Windows Kits/10/bin/10.0.22621.0/x86/makepri.exe"
        test -f "$out/Windows Kits/10/UnionMetadata/10.0.22621.0/Windows.winmd"
      '';
  msxmlInstaller = pkgs.fetchurl {
    url = "https://download.microsoft.com/download/2/7/7/277681BE-4048-4A58-ABBA-259C465B1699/msxml6-KB2957482-enu-amd64.exe";
    sha256 = "260cd870851ffc3c6d10b71691f134e20d8d03ac26073bb36951eacb7aa85897";
  };
  msxml =
    pkgs.runCommand "uwp-msxml6"
      {
        nativeBuildInputs = [ pkgs.cabextract ];
        meta.license = lib.licenses.unfree;
      }
      ''
        cabextract -q ${msxmlInstaller}
        mkdir dlls
        cabextract -q -d dlls msxml6.msi
        mkdir -p "$out/x86" "$out/x64"
        for dll in msxml6 msxml6r; do
          cp "dlls/$dll.dll.86F857F6_A743_463D_B2FE_98CB5F727E09" "$out/x86/$dll.dll"
          cp "dlls/$dll.dll.1ECC0691_D2EB_4A33_9CBF_5487E5CB17DB" "$out/x64/$dll.dll"
        done
      '';
  cppwinrt = pkgs.stdenv.mkDerivation {
    pname = "cppwinrt";
    version = "2.0.250303.1";
    src = inputs.cppwinrt;
    nativeBuildInputs = [
      pkgs.cmake
      pkgs.ninja
    ];
    cmakeFlags = [
      "-DCPPWINRT_BUILD_VERSION=2.0.250303.1"
      "-DEXTERNAL_WINMD_INCLUDE_DIR=${inputs.winmd}/src"
    ];
    meta.license = lib.licenses.mit;
  };
  openappx = pkgs.python3Packages.buildPythonPackage {
    pname = "openappx";
    version = "0.7.0";
    src = inputs.openappx;
    pyproject = true;
    build-system = [ pkgs.python3Packages.setuptools ];
    dependencies = [ pkgs.python3Packages.cryptography ];
    pythonImportsCheck = [
      "openappx"
      "openappx.deploy"
    ];
    meta.license = lib.licenses.mit;
  };
  python = pkgs.python3.withPackages (_: [ openappx ]);
  wine = pkgs.wineWow64Packages.stable;
  runtime = [
    wine
    python
    pkgs.coreutils
    pkgs.findutils
    pkgs.gnugrep
    pkgs.gnused
    pkgs.gawk
    pkgs.diffutils
    pkgs.file
    pkgs.which
    llvmPackages.clang-unwrapped
    llvmPackages.lld
    llvmPackages.llvm
  ];
  uwp = pkgs.stdenvNoCC.mkDerivation {
    pname = "uwp-crossbuild";
    version = "0.5.3";
    src = inputs.uwp-crossbuild;
    nativeBuildInputs = [
      pkgs.makeWrapper
      python
    ];
    dontBuild = true;
    installPhase = ''
      make install PREFIX="$out"
      patchShebangs "$out/lib/uwp-crossbuild/scripts"
      for command in "$out"/bin/*; do
        target=$(readlink "$command")
        rm "$command"
        makeWrapper "$target" "$command" \
          --prefix PATH : ${lib.makeBinPath runtime} \
          --set UWP_XWIN_ROOT ${xwinRoot} \
          --set UWP_SDK_ROOT ${sdk} \
          --set UWP_CPPWINRT_EXE ${cppwinrt}/bin/cppwinrt \
          --set UWP_MAKEPRI_X86 1
      done
    '';
    meta.license = lib.licenses.mit;
  };
  withDisplay = pkgs.writeShellApplication {
    name = "uwp-with-display";
    runtimeInputs = [
      pkgs.xorg-server
      pkgs.xauth
      pkgs.openssl
      pkgs.coreutils
    ];
    text = builtins.readFile ../bin/with-display;
  };
  withWine = pkgs.writeShellApplication {
    name = "uwp-with-wine";
    runtimeInputs = [
      wine
      withDisplay
      pkgs.coreutils
    ];
    text = ''
      export WINEPREFIX="''${WINEPREFIX:-''${XDG_CACHE_HOME:-$HOME/.cache}/xbox-uwp/wine}"
      export WINEARCH=wow64 WINEDEBUG=-all
      export WINEDLLOVERRIDES="''${WINEDLLOVERRIDES:-mscoree,mshtml=;msxml6=n,b}"
      if [[ "''${UWP_DISPLAY_READY:-}" != 1 ]]; then
        export UWP_DISPLAY_READY=1
        exec uwp-with-display "$0" "$@"
      fi
      mkdir -p "$WINEPREFIX"
      stamp="$WINEPREFIX/.uwp-runtime"
      if [[ ! -f "$stamp" ]] || [[ "$(cat "$stamp")" != '${wine}:${msxml}' ]]; then
        wineboot -u
        cp ${msxml}/x86/*.dll "$WINEPREFIX/drive_c/windows/syswow64/"
        cp ${msxml}/x64/*.dll "$WINEPREFIX/drive_c/windows/system32/"
        printf '%s\n' '${wine}:${msxml}' > "$stamp"
      fi
      "$@"
    '';
  };
  tools = pkgs.symlinkJoin {
    name = "xbox-uwp-tools";
    paths = [
      uwp
      python
      withWine
      withDisplay
      cppwinrt
    ]
    ++ runtime;
  };
  # The .vcxproj route takes headers by explicit path; DXC's native build
  # writes the same headers the sample's CMake build generates.
  shaders = pkgs.runCommand "nixbox-cube-shaders" { nativeBuildInputs = [ pkgs.directx-shader-compiler ]; } ''
    mkdir -p "$out/include"
    for shader in cubeVertexShader:vertexMain:vs_6_0 cubePixelShader:pixelMain:ps_6_0; do
      IFS=: read -r name entry profile <<< "$shader"
      dxc -O3 -T "$profile" -E "$entry" -Fh "$out/include/$name.h" -Vn "$name" ${../example/Shaders/Cube.hlsl}
    done
  '';
  hello =
    pkgs.runCommand "hello-vcxproj"
      {
        nativeBuildInputs = [ tools ];
      }
      ''
        export WINEPREFIX="$TMPDIR/wine"
        trap 'wineserver -k || true' EXIT
        mkdir -p "$out"
        uwp-with-wine uwp-build-project --project ${../example}/hello-uwp.vcxproj \
          --config Release --no-restore --out "$TMPDIR/layout" \
          --property ZlibIncludeDir=${lib.getDev pkgsXbox.zlib}/include \
          --property ZlibLibrary=${lib.getLib pkgsXbox.zlib}/lib/zs.lib \
          --property LuauRoot=${pkgsXbox.luau} \
          --property ShaderIncludeDir=${shaders}/include \
          --property DirectXHeadersIncludeDir=${pkgs.directx-headers}/include/directx
        openappx validate --root "$TMPDIR/layout"
        openappx pack --root "$TMPDIR/layout" --out "$out/hello.msix"
        cp -a "$TMPDIR/layout" "$out/layout"
        openappx inspect --package "$out/hello.msix"
      '';
in
{
  inherit tools hello shaders python;
  UWP_XWIN_ROOT = xwinRoot;
  UWP_SDK_ROOT = sdk;
  UWP_CPPWINRT_EXE = "${cppwinrt}/bin/cppwinrt";
  packages = {
    inherit openappx cppwinrt sdk;
    cube-shaders = shaders;
    xwin-sdk = xwinRoot;
    msxml6 = msxml;
    uwp-crossbuild = uwp;
    toolchain = tools;
    cache-roots = pkgs.linkFarm "xbox-cache-roots" (
      map
        (package: {
          name = builtins.unsafeDiscardStringContext (builtins.baseNameOf (toString package));
          path = package;
        })
        [
          xwinRoot
          sdk
          msxml
          cppwinrt
          openappx
          python
          uwp
          withDisplay
          withWine
          tools
          shaders
          pkgsXbox.zlib
          pkgsXbox.hello
          pkgsXbox.luau
          pkgsXbox.xboxCxxHeaders
          (lib.getDev pkgsXbox.zlib)
          pkgsXbox.stdenv.cc
          hello
        ]
    );
  };
}
