{
  xbox,
  nxtuiSource,
  websocket ? false,
}:
let
  name = if websocket then "nxt-websocket" else "nxt-network";
in
xbox.mkXboxApp {
  pname = name;
  version = "0.1.0";
  displayName = if websocket then "NXT Xbox WebSocket" else "NXT Xbox networking";
  description =
    if websocket then
      "Coroutine WebSocket framing, validation and ws/wss transport checks"
    else
      "DNS, sockets, TLS and streamed GPT-6 Luna transport checks";
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
  cmakeFlags = [
    "-DNXT_PROBE_SOURCE=${nxtuiSource}/test/${if websocket then "websocket" else "network"}-probe.cpp"
    "-DNXT_WEBSOCKET=${if websocket then "ON" else "OFF"}"
  ];
  postInstall = ''
    mkdir -p "$out/share/${name}"
    cp ${xbox.pkgs.cacert}/etc/ssl/certs/ca-bundle.crt "$out/share/${name}/ca-bundle.pem"
  '';
}
