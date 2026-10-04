# nix build --impure --file nix/experiments/luau-smoke.nix -o build-output/luau-smoke
# ./env uwp-with-wine wine "$PWD/build-output/luau-smoke/bin/luau-smoke.exe"
let
  flake = builtins.getFlake (toString ../..);
  p = flake.legacyPackages.x86_64-linux.pkgsXbox;
  luau = import ./luau.nix;
in
p.stdenv.mkDerivation {
  pname = "luau-gameplay-smoke";
  version = luau.version;
  dontUnpack = true;
  buildPhase = ''
    $CXX -std=c++17 $CXXFLAGS -I${luau}/include ${./luau-smoke.cpp} \
      ${luau}/lib/*.lib -o luau-smoke.exe
  '';
  installPhase = ''
    mkdir -p "$out/bin"
    cp luau-smoke.exe "$out/bin/"
  '';
}
