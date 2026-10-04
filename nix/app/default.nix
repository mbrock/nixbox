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
      echo '${reroutes}/lib/appcontainer-pointers.lib ${reroutes}/lib/appcontainer-ntdll.lib WindowsApp.lib /appcontainer' >> "$out/nix-support/cc-ldflags"
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
          <Application Id="App" Executable="${executable}" EntryPoint="App">
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

  # Build an Xbox app with any build system. The build installs its
  # executable into bin/ (CMake's default) and data into share/<pname>/;
  # both become the package root. The result holds layout/ and <pname>.msix;
  # signing happens at deployment, outside Nix, with a local key.
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
      ...
    }@args:
    let
      # CMake otherwise picks the DLL runtime, which the app container lacks,
      # and links desktop kernel32/ole32/user32… ahead of WindowsApp.lib, whose
      # API-set imports are the ones the console resolves.
      app = stdenv.mkDerivation (
        {
          cmakeFlags = [
            "-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded"
            "-DCMAKE_C_STANDARD_LIBRARIES="
            "-DCMAKE_CXX_STANDARD_LIBRARIES="
          ]
          ++ (args.cmakeFlags or [ ]);
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
          "cmakeFlags"
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
              backgroundColor
              capabilities
              ;
          };
    in
    pkgs.runCommand "${pname}-${version}-msix"
      {
        nativeBuildInputs = [
          python
          llvmPackages.llvm
          pkgs.file
        ];
        passthru = { inherit app; };
      }
      ''
        mkdir layout symbols
        cp -r ${app}/bin/. layout/
        # Debug symbols stay out of the package, beside it for crash dumps.
        find layout -name '*.pdb' -exec mv -t symbols {} +
        if [[ -d ${app}/share/${pname} ]]; then
          cp -r ${app}/share/${pname}/. layout/
        fi
        cp ${manifestFile} layout/AppxManifest.xml
        mkdir -p layout/Assets
        cp -r ${assets}/. layout/Assets/
        chmod -R u+w layout
        [[ -f layout/${executable} ]] || {
          echo "The build installed no bin/${executable}" >&2
          exit 1
        }
        # The app container wants the GUI subsystem at version 6.02 or later;
        # set it here so the build may link an ordinary main() or wWinMain().
        llvm-objcopy --subsystem windows:6.2 layout/${executable}
        bash ${inputs.uwp-crossbuild}/scripts/pe-import-audit.sh --allow-kernel32 layout/${executable}
        openappx validate --root layout
        mkdir "$out"
        openappx pack --root layout --out "$out/${pname}.msix"
        cp -r layout "$out/layout"
        if [[ -n "$(ls symbols)" ]]; then
          cp -r symbols "$out/symbols"
        fi
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

  deployTool = pkgs.writeShellApplication {
    name = "nixbox-deploy";
    runtimeInputs = [ python ];
    text = ''exec python3 ${./deploy.py} "$@"'';
  };

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
    compileShaders
    deployTool
    mkDeploy
    ;
}
