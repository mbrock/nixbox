# The game, described for nixbox. `xbox` is nixbox.lib.<system>.
xbox:
xbox.mkXboxApp {
  pname = "game";
  version = "0.1.0";
  displayName = "nixbox game";
  backgroundColor = "#0E1116";
  src = xbox.pkgs.lib.fileset.toSource {
    root = ./.;
    fileset = xbox.pkgs.lib.fileset.unions [
      ./meson.build
      ./main.cpp
      ./Cube.hlsl
    ];
  };
  nativeBuildInputs = with xbox.pkgs; [
    meson
    ninja
    directx-shader-compiler
  ];
}
