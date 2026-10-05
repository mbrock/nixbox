{ xbox }:
xbox.mkXboxApp {
  pname = "ssh-probe";
  version = "0.1.0";
  displayName = "Xbox SSH probe";
  description = "libssh2 pinned host keys, public-key authentication, exec and SFTP";
  src = xbox.pkgs.lib.fileset.toSource {
    root = ./.;
    fileset = xbox.pkgs.lib.fileset.unions [
      ./CMakeLists.txt
      ./main.cpp
      ./ssh-probe.cpp
    ];
  };
  nativeBuildInputs = [
    xbox.pkgs.cmake
    xbox.pkgs.ninja
    xbox.pkgsXbox.buildPackages.pkg-config
  ];
  buildInputs = [
    xbox.pkgsXbox.SDL3
    xbox.pkgsXbox.libssh2
  ];
  capabilities = [
    "internetClient"
    "privateNetworkClientServer"
  ];
}
