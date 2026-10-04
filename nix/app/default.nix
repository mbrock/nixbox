# The app layer: an ordinary pkgsXbox derivation in, an installable MSIX out.
# No Visual Studio project and no Wine: the app is compiled by the same
# compiler as every pkgsXbox library, with a few link flags only an
# executable needs, then laid out with a generated manifest and packed.
{
  pkgs,
  pkgsXbox,
  xboxCC,
  llvmPackages,
  inputs,
  python,
  tools,
}:
let
  inherit (pkgs) lib;

  # xwin's kernel32.lib routes a few CRT and unwinder imports through apisets
  # the Xbox app container lacks; the image installs and then refuses to
  # launch. These import libraries resolve exactly those names from
  # KERNELBASE.dll and ntdll.dll first. The .def files explain each name.
  reroutes = pkgs.runCommand "xbox-appcontainer-reroutes" { } ''
    mkdir -p "$out/lib"
    for def in appcontainer-pointers appcontainer-ntdll; do
      ${llvmPackages.llvm}/bin/llvm-dlltool -m i386:x86-64 \
        -d ${inputs.uwp-crossbuild}/include/$def.def -l "$out/lib/$def.lib"
    done
  '';

  # The library compiler, plus what an app executable needs. DirectX-Headers
  # comes in with -I, ahead of the SDK's -isystem directories, so d3d12.h and
  # d3dx12.h are always the same release. /appcontainer is harmless for the
  # small programs build systems link while probing the compiler.
  appCC = xboxCC.override (old: {
    extraBuildCommands = old.extraBuildCommands + ''
      echo '-I${pkgs.directx-headers}/include/directx' >> "$out/nix-support/cc-cflags"
      echo '${reroutes}/lib/appcontainer-pointers.lib ${reroutes}/lib/appcontainer-ntdll.lib WindowsApp.lib /appcontainer /ignore:4099' >> "$out/nix-support/cc-ldflags"
    '';
  });
  stdenv = pkgsXbox.stdenv.override { cc = appCC; };

  # MSIX versions have exactly four numeric parts.
  manifestVersion =
    version:
    let
      parts = lib.splitString "." version;
    in
    assert lib.all (part: builtins.match "[0-9]+" part != null) parts && builtins.length parts <= 4;
    lib.concatStringsSep "." (parts ++ lib.replicate (4 - builtins.length parts) "0");

  generateManifest =
    {
      identity,
      publisher,
      version,
      displayName,
      publisherDisplayName,
      description,
      executable,
      entryPoint,
      backgroundColor,
      capabilities,
    }:
    pkgs.writeText "AppxManifest.xml" ''
      <?xml version="1.0" encoding="utf-8"?>
      <Package
        xmlns="http://schemas.microsoft.com/appx/manifest/foundation/windows10"
        xmlns:uap="http://schemas.microsoft.com/appx/manifest/uap/windows10"
        IgnorableNamespaces="uap">
        <Identity Name="${identity}" Publisher="${publisher}"
                  Version="${manifestVersion version}" ProcessorArchitecture="x64" />
        <Properties>
          <DisplayName>${displayName}</DisplayName>
          <PublisherDisplayName>${publisherDisplayName}</PublisherDisplayName>
          <Logo>Assets\StoreLogo.png</Logo>
        </Properties>
        <Dependencies>
          <TargetDeviceFamily Name="Windows.Universal" MinVersion="10.0.19041.0"
                              MaxVersionTested="10.0.22621.0" />
        </Dependencies>
        <Resources>
          <Resource Language="en-US" />
        </Resources>
        <Applications>
          <Application Id="App" Executable="${executable}" EntryPoint="${entryPoint}">
            <uap:VisualElements DisplayName="${displayName}" Description="${description}"
              BackgroundColor="${backgroundColor}"
              Square150x150Logo="Assets\Square150x150Logo.png"
              Square44x44Logo="Assets\Square44x44Logo.png">
              <uap:SplashScreen Image="Assets\SplashScreen.png" />
            </uap:VisualElements>
          </Application>
        </Applications>
        <Capabilities>
      ${lib.concatMapStrings (name: "    <Capability Name=\"${name}\" />\n") capabilities}  </Capabilities>
      </Package>
    '';

  # CMake reads CMAKE_TOOLCHAIN_FILE from the environment too, so the dev shell
  # configures a plain `cmake -B build` exactly as the Nix build does. CMake
  # otherwise picks the DLL runtime, which the app container lacks, and links
  # desktop kernel32/ole32/user32… ahead of WindowsApp.lib, whose API-set
  # imports are the ones the console resolves.
  toolchainFile = pkgs.writeText "xbox-toolchain.cmake" ''
    set(CMAKE_SYSTEM_NAME Windows)
    set(CMAKE_SYSTEM_PROCESSOR x86_64)
    set(CMAKE_C_COMPILER ${appCC}/bin/x86_64-pc-windows-msvc-clang)
    set(CMAKE_CXX_COMPILER ${appCC}/bin/x86_64-pc-windows-msvc-clang++)
    set(CMAKE_AR ${appCC}/bin/x86_64-pc-windows-msvc-ar CACHE FILEPATH "")
    set(CMAKE_RANLIB ${appCC}/bin/x86_64-pc-windows-msvc-ranlib CACHE FILEPATH "")
    set(CMAKE_TRY_COMPILE_CONFIGURATION Release)
    list(APPEND CMAKE_MODULE_PATH ${./cmake})
    set(CMAKE_MSVC_RUNTIME_LIBRARY MultiThreaded CACHE STRING "")
    set(CMAKE_C_STANDARD_LIBRARIES "" CACHE STRING "")
    set(CMAKE_CXX_STANDARD_LIBRARIES "" CACHE STRING "")
  '';

  # Meson finds cross files by name in $XDG_DATA_DIRS/meson/cross, so the dev
  # shell's `meson setup build --cross-file xbox` needs no path. The compiler
  # defaults to the static CRT; Meson must agree (b_vscrt) and must not add
  # desktop default libraries (winlibs).
  mesonCross = pkgs.writeTextDir "share/meson/cross/xbox" ''
    [binaries]
    c = '${appCC}/bin/x86_64-pc-windows-msvc-clang'
    cpp = '${appCC}/bin/x86_64-pc-windows-msvc-clang++'
    ar = '${appCC}/bin/x86_64-pc-windows-msvc-ar'
    strip = '${appCC}/bin/x86_64-pc-windows-msvc-strip'
    # Nixpkgs' target pkg-config wrapper, from pkgsXbox.buildPackages.pkg-config.
    pkg-config = 'x86_64-pc-windows-msvc-pkg-config'

    [built-in options]
    b_vscrt = 'mt'
    c_winlibs = []
    cpp_winlibs = []

    [host_machine]
    system = 'windows'
    cpu_family = 'x86_64'
    cpu = 'x86_64'
    endian = 'little'
  '';

  packageTools = [
    python
    llvmPackages.llvm
    pkgs.file
  ];

  deployTool = pkgs.writeShellApplication {
    name = "nixbox-deploy";
    runtimeInputs = [ python ];
    text = ''exec python3 ${./deploy.py} "$@"'';
  };

  # Build an Xbox app with any build system. The build installs its
  # executable into bin/ (CMake's default) and data into share/<pname>/;
  # both become the package root. The result holds layout/ and <pname>.msix;
  # signing happens at deployment, outside Nix, with a local key. Its
  # devShell builds the same thing incrementally outside Nix.
  mkXboxApp =
    {
      pname,
      version,
      identity ? "Nixbox.${pname}",
      publisher ? "CN=nixbox-dev",
      displayName ? pname,
      publisherDisplayName ? "nixbox",
      description ? displayName,
      executable ? "${pname}.exe",
      backgroundColor ? "#000000",
      capabilities ? [ "internetClient" ],
      assets ? ./assets,
      manifest ? null,
      idl ? null,
      entryPoint ? "App",
      ...
    }@args:
    let
      # A XAML app's Application class is a runtimeclass named by EntryPoint,
      # declared in the .idl and resolved against <namespace>.winmd.
      projection =
        if idl == null then
          null
        else
          compileIdl {
            name = builtins.head (lib.splitString "." entryPoint);
            src = idl;
          };
      app = stdenv.mkDerivation (
        {
          cmakeFlags = [ "-DCMAKE_TOOLCHAIN_FILE=${toolchainFile}" ] ++ (args.cmakeFlags or [ ]);
          # After Nixpkgs' own cross file, so these settings win.
          mesonFlags = [ "--cross-file=${mesonCross}/share/meson/cross/xbox" ] ++ (args.mesonFlags or [ ]);
          # Nixpkgs defaults Meson to the unoptimized "plain".
          mesonBuildType = "release";
          buildInputs = lib.optional (projection != null) projection ++ (args.buildInputs or [ ]);
        }
        // removeAttrs args [
          "identity"
          "publisher"
          "displayName"
          "publisherDisplayName"
          "description"
          "executable"
          "backgroundColor"
          "capabilities"
          "assets"
          "manifest"
          "idl"
          "entryPoint"
          "cmakeFlags"
          "mesonFlags"
          "buildInputs"
        ]
      );
      manifestFile =
        if manifest != null then
          manifest
        else
          generateManifest {
            inherit
              identity
              publisher
              version
              displayName
              publisherDisplayName
              description
              executable
              entryPoint
              backgroundColor
              capabilities
              ;
          };
      packageEnv = {
        XBOX_PNAME = pname;
        XBOX_EXECUTABLE = executable;
        XBOX_MANIFEST = manifestFile;
        XBOX_ASSETS = assets;
        XBOX_AUDIT = "${inputs.uwp-crossbuild}/scripts/pe-import-audit.sh";
        XBOX_WINMD = lib.optionalString (projection != null) "${projection}/lib";
      };
      exports = lib.concatStrings (lib.mapAttrsToList (name: value: "export ${name}=${lib.escapeShellArg value}\n") packageEnv);

      # xbox-package [PREFIX] [OUT]: the Nix build's packaging, for a local
      # install prefix. xbox-deploy [ARGS…]: package build/install, then deploy.
      xboxPackage = pkgs.writeShellApplication {
        name = "xbox-package";
        runtimeInputs = packageTools;
        text = exports + ''
          exec bash ${./package.sh} "''${1:-build/install}" "''${2:-build/package}"
        '';
      };
      xboxDeploy = pkgs.writeShellApplication {
        name = "xbox-deploy";
        text = ''
          ${lib.getExe xboxPackage} build/install build/package
          exec ${lib.getExe deployTool} build/package "$@"
        '';
      };
      devShell = (pkgsXbox.mkShell.override { inherit stdenv; }) {
        inputsFrom = [ app ];
        packages = [
          xboxPackage
          xboxDeploy
          deployTool
        ];
        CMAKE_TOOLCHAIN_FILE = toolchainFile;
        shellHook = ''
          export XDG_DATA_DIRS=${mesonCross}/share:''${XDG_DATA_DIRS:-/usr/local/share:/usr/share}
        '';
      };
    in
    pkgs.runCommand "${pname}-${version}-msix"
      (
        packageEnv
        // {
          nativeBuildInputs = packageTools;
          passthru = { inherit app devShell; };
        }
      )
      ''
        bash ${./package.sh} ${app} "$out"
      '';

  # Run an .idl through midlrt (under Wine, the one step that needs it) and
  # cppwinrt: include/ gets the C++/WinRT projection, App.g.h, and
  # module.g.cpp; lib/<name>.winmd ships in the package. mkXboxApp's `idl`
  # argument does this and adds the result to buildInputs.
  compileIdl =
    { name, src }:
    pkgs.runCommand "${name}-projection" { nativeBuildInputs = [ tools ]; } ''
      export WINEPREFIX="$TMPDIR/wine"
      trap 'wineserver -k || true' EXIT
      cp ${src} app.idl
      uwp-with-wine uwp-gen-projection --idl app.idl --name ${name} --out gen > /dev/null
      mkdir -p "$out/include" "$out/lib"
      mv gen/${name}.winmd "$out/lib/"
      rm -r gen/stubs
      cp -r gen/. "$out/include/"
    '';

  # Compile HLSL to DXIL with the native, signing DXC. Each entry becomes
  # include/<name>.h holding a byte array called <name>; put the result in
  # buildInputs to put it on the include path.
  compileShaders =
    {
      name,
      src,
      entries,
      flags ? [ "-O3" ],
    }:
    pkgs.runCommand name { nativeBuildInputs = [ pkgs.directx-shader-compiler ]; } ''
      mkdir -p "$out/include"
      ${lib.concatMapStrings (entry: ''
        dxc ${lib.escapeShellArgs flags} -T ${entry.profile} -E ${entry.entry} \
          -Fh "$out/include/${entry.name}.h" -Vn ${entry.name} ${entry.src or src}
      '') entries}
    '';

  # `nix run` target that signs and deploys one package to the console.
  mkDeploy = package: {
    type = "app";
    program = lib.getExe (
      pkgs.writeShellApplication {
        name = "deploy-${package.name}";
        text = ''exec ${lib.getExe deployTool} ${package} "$@"'';
      }
    );
  };
in
{
  inherit
    stdenv
    mkXboxApp
    toolchainFile
    mesonCross
    compileIdl
    compileShaders
    deployTool
    mkDeploy
    ;
}
