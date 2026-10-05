# The same game and physics tests, runnable on the build host for iteration.
pkgs:
let
  libraries = pkgs.extend (import ../../nix/arcade-libraries.nix);
in
pkgs.stdenv.mkDerivation {
  pname = "ricochet-native";
  version = "0.1.0";
  src = pkgs.lib.fileset.toSource {
    root = ./.;
    fileset = pkgs.lib.fileset.unions [
      ./CMakeLists.txt
      ./src
      ./tests
    ];
  };
  nativeBuildInputs = [
    pkgs.cmake
    pkgs.ninja
  ];
  buildInputs = [
    libraries.sdl3
    libraries.box2d
    libraries.imgui
  ];
  cmakeFlags = [ "-DBUILD_TESTING=ON" ];
  doCheck = true;
  checkPhase = ''
    runHook preCheck
    ctest --output-on-failure
    runHook postCheck
  '';
  meta.mainProgram = "ricochet";
}
