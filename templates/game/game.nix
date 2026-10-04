# The game, described for nixbox. `xbox` is nixbox.lib.x86_64-linux.
xbox:
xbox.mkXboxApp {
  pname = "game";
  version = "0.1.0";
  displayName = "nixbox game";
  backgroundColor = "#0E1116";
  src = xbox.pkgs.lib.fileset.toSource {
    root = ./.;
    fileset = xbox.pkgs.lib.fileset.unions [
      ./CMakeLists.txt
      ./main.cpp
      ./Cube.hlsl
    ];
  };
  nativeBuildInputs = with xbox.pkgs; [
    cmake
    ninja
    directx-shader-compiler
  ];
}
