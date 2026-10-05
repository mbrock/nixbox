# A D3D12 capability report for the console, described for nixbox.
xbox:
xbox.mkXboxApp {
  pname = "d3d12caps";
  version = "0.1.0";
  displayName = "nixbox D3D12 capabilities";
  src = xbox.pkgs.lib.fileset.toSource {
    root = ./.;
    fileset = xbox.pkgs.lib.fileset.unions [
      ./meson.build
      ./main.cpp
      ./report.h
      ./formats.cpp
      ./gpu.cpp
      ./display.cpp
      ./probe.hlsl
    ];
  };
  nativeBuildInputs = [
    xbox.pkgs.meson
    xbox.pkgs.ninja
    xbox.pkgs.directx-shader-compiler
    xbox.pkgsXbox.buildPackages.pkg-config
  ];
  buildInputs = [ xbox.pkgsXbox.SDL3 ];
}
