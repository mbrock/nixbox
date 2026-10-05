{ xbox }:
xbox.mkXboxApp {
  pname = "ghostty-vt-probe";
  version = "0.1.0";
  displayName = "Ghostty VT on Xbox";
  description = "Zig-built terminal engine: Unicode, cursor, erase, scroll and resize checks";
  src = xbox.pkgs.lib.fileset.toSource {
    root = ./.;
    fileset = xbox.pkgs.lib.fileset.unions [
      ./CMakeLists.txt
      ./main.cpp
      ./consumer.c
    ];
  };
  nativeBuildInputs = [
    xbox.pkgs.cmake
    xbox.pkgs.ninja
    xbox.pkgsXbox.buildPackages.pkg-config
  ];
  buildInputs = [
    xbox.pkgsXbox.SDL3
    xbox.pkgsXbox.ghostty-vt
  ];
  capabilities = [ ];
}
