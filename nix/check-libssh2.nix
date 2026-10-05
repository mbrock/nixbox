{ xbox }:
xbox.stdenv.mkDerivation {
  name = "libssh2-consumer";
  dontUnpack = true;
  nativeBuildInputs = [ xbox.pkgsXbox.buildPackages.pkg-config ];
  buildInputs = [ xbox.pkgsXbox.libssh2 ];
  buildPhase = ''
    $CXX -std=c++17 ${../probes/ssh/ssh-probe.cpp} -o ssh-probe.exe \
      $($PKG_CONFIG --cflags --libs --static libssh2)
  '';
  installPhase = ''
    mkdir -p "$out/bin"
    cp ssh-probe.exe "$out/bin/"
  '';
}
