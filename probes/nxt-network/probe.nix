{ xbox, nxtuiSource }:
xbox.mkXboxApp {
  pname = "nxt-network";
  version = "0.1.0";
  displayName = "NXT Xbox networking";
  description = "DNS, sockets, TLS and streamed GPT-6 Luna transport checks";
  src = xbox.pkgs.lib.fileset.toSource {
    root = ./.;
    fileset = xbox.pkgs.lib.fileset.unions [
      ./CMakeLists.txt
      ./main.cpp
    ];
  };
  nativeBuildInputs = [
    xbox.pkgs.cmake
    xbox.pkgs.ninja
    xbox.pkgsXbox.buildPackages.pkg-config
  ];
  buildInputs = [
    xbox.pkgsXbox.SDL3
    xbox.pkgsXbox.nxtrt-iocp
  ];
  cmakeFlags = [ "-DNXT_PROBE_SOURCE=${nxtuiSource}/test/network-probe.cpp" ];
  postInstall = ''
    mkdir -p "$out/share/nxt-network"
    cp ${xbox.pkgs.cacert}/etc/ssl/certs/ca-bundle.crt "$out/share/nxt-network/ca-bundle.pem"
  '';
}
