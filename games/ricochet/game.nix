# An SDL3 + Box2D + ImGui game, described for nixbox. `xbox` is
# nixbox.lib.<system>.
xbox:
xbox.mkXboxApp {
  pname = "ricochet";
  version = "0.1.0";
  displayName = "Ricochet";
  description = "A tiny physics-powered cannon arena";
  src = xbox.pkgs.lib.fileset.toSource {
    root = ./.;
    fileset = xbox.pkgs.lib.fileset.unions [
      ./CMakeLists.txt
      ./src
      ./tests
    ];
  };
  nativeBuildInputs = [
    xbox.pkgs.cmake
    xbox.pkgs.ninja
    xbox.pkgsXbox.buildPackages.pkg-config
  ];
  buildInputs = [
    xbox.pkgsXbox.SDL3
    xbox.pkgsXbox.box2d
    xbox.pkgsXbox.imgui
  ];
  cmakeFlags = [ "-DBUILD_TESTING=OFF" ];
}
