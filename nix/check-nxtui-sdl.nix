# An isolated downstream consumer: no private SDL/FreeType/HarfBuzz paths.
{ xbox }:
xbox.stdenv.mkDerivation {
  name = "nxtui-sdl-consumer";
  dontUnpack = true;
  nativeBuildInputs = [ xbox.pkgsXbox.buildPackages.pkg-config ];
  buildInputs = [ xbox.pkgsXbox.nxtui-sdl ];
  buildPhase = ''
    cat > consumer.cpp <<'EOF'
    #include <nxtui/sdl.hpp>
    #include <nxtai/responses_transport.hpp>
    int main() {
      nxtai::responses_transport transport{{.ca_file = "ca-bundle.pem"}};
      nxtui::ui::SdlPainter painter{nullptr, "font.ttf"};
      auto frame = nxtui::ui::paint(nxtui::ui::text("fractional rhythm"),
                                   painter, painter.viewport());
      painter.draw(frame);
    }
    EOF
    $CXX -std=c++23 consumer.cpp -o consumer.exe \
      $($PKG_CONFIG --cflags --libs --static nxtrt-iocp nxtui-sdl)
  '';
  installPhase = ''
    mkdir -p "$out/bin"
    cp consumer.exe "$out/bin/"
  '';
}
