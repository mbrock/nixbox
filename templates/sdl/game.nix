# An SDL3 game, described for nixbox. `xbox` is nixbox.lib.<system>.
xbox:
xbox.mkXboxApp {
  pname = "sdlgame";
  version = "0.1.0";
  displayName = "nixbox SDL game";
  src = xbox.pkgs.lib.fileset.toSource {
    root = ./.;
    fileset = xbox.pkgs.lib.fileset.unions [
      ./meson.build
      ./main.cpp
    ];
  };
  nativeBuildInputs = [
    xbox.pkgs.meson
    xbox.pkgs.ninja
    xbox.pkgsXbox.buildPackages.pkg-config
  ];
  buildInputs = [ xbox.pkgsXbox.SDL3 ];
}
