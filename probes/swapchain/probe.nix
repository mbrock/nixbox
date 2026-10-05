# Swapchain sizes, formats, colour spaces, and present cadence on the console.
xbox:
xbox.mkXboxApp {
  pname = "swapchain";
  version = "0.1.0";
  displayName = "nixbox swapchain";
  backgroundColor = "#0E1116";
  src = xbox.pkgs.lib.fileset.toSource {
    root = ./.;
    fileset = xbox.pkgs.lib.fileset.unions [
      ./meson.build
      ./main.cpp
      ./pattern.hlsl
    ];
  };
  nativeBuildInputs = with xbox.pkgs; [
    meson
    ninja
    directx-shader-compiler
  ];
}
