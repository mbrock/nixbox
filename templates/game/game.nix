# The game, described for nixbox. `xbox` is nixbox.lib.x86_64-linux.
xbox:
let
  shaders = xbox.compileShaders {
    name = "game-shaders";
    src = ./Cube.hlsl;
    entries = [
      { name = "cubeVertexShader"; entry = "vertexMain"; profile = "vs_6_0"; }
      { name = "cubePixelShader"; entry = "pixelMain"; profile = "ps_6_0"; }
    ];
  };
in
xbox.mkXboxApp {
  pname = "game";
  version = "0.1.0";
  displayName = "nixbox game";
  backgroundColor = "#0E1116";
  src = xbox.pkgs.lib.fileset.toSource {
    root = ./.;
    fileset = xbox.pkgs.lib.fileset.unions [ ./CMakeLists.txt ./main.cpp ];
  };
  nativeBuildInputs = [ xbox.pkgs.cmake xbox.pkgs.ninja ];
  buildInputs = [ shaders ];
}
