{ xbox, nxtuiSource }:
xbox.mkXboxApp {
  pname = "nxt-chat";
  version = "0.1.0";
  displayName = "NXT Agent chat";
  description = "Live GPT-6 Luna chat with character rhythm and graphical paint";
  src = xbox.pkgs.lib.fileset.toSource {
    root = ./.;
    fileset = xbox.pkgs.lib.fileset.unions [ ./CMakeLists.txt ./main.cpp ];
  };
  nativeBuildInputs = [
    xbox.pkgs.cmake
    xbox.pkgs.ninja
    xbox.pkgsXbox.buildPackages.pkg-config
  ];
  buildInputs = [ xbox.pkgsXbox.nxtui-sdl ];
  cmakeFlags = [ "-DNXT_CHAT_VIEW=${nxtuiSource}/demo/agent_chat/view.hpp" ];
  postInstall = ''
    mkdir -p "$out/share/nxt-chat"
    cp ${xbox.pkgs.cacert}/etc/ssl/certs/ca-bundle.crt "$out/share/nxt-chat/ca-bundle.pem"
    cp ${xbox.pkgs.dejavu_fonts}/share/fonts/truetype/DejaVuSans.ttf "$out/share/nxt-chat/font.ttf"
    cp ${xbox.pkgs.dejavu_fonts.full-ttf.src}/LICENSE "$out/share/nxt-chat/FONT-LICENSE"
  '';
}
