# Library-only Ghostty, with no GUI, SIMD C++ dependencies or image decoder.
{
  lib,
  stdenv,
  fetchFromGitHub,
  fetchurl,
  runCommand,
  zig_0_16,
  sdk ? null,
}:
let
  windows = stdenv.hostPlatform.isWindows;
  # --system uses unpacked, hash-named packages and never fetches remotely.
  # translate-c and aro are required by the build graph even without C sources.
  deps = runCommand "ghostty-vt-zig-deps" { } ''
    mkdir -p "$out"
    mkdir "$out/uucode-0.2.0-ZZjBPuuFVgC8YZ8eld4fOKsZANLIhTFMzULQxhkLi1C7"
    tar -xf ${
      fetchurl {
        url = "https://github.com/jacobsandlund/uucode/archive/9d55524551411b493cca41ca06363625d90aff1e.tar.gz";
        hash = "sha256-aNI1Q8ssbF39vfOLODdeDG1BKlxftyzga9E596TSUTI=";
      }
    } --strip-components=1 -C "$out/uucode-0.2.0-ZZjBPuuFVgC8YZ8eld4fOKsZANLIhTFMzULQxhkLi1C7"
    mkdir "$out/translate_c-0.0.0-Q_BUWhVNBwDOEcIqub4VFPJPB6D9dgwzUMHTX5KWr8Xr"
    tar -xf ${
      fetchurl {
        url = "https://codeberg.org/vancluever/translate-c/archive/4e879eb8aba615de112eabd1231ea6e01920cead.tar.gz";
        hash = "sha256-j7D1xqPos78ONSJdOrwSMLz28f3xYkZ/H3R+wvpe2gc=";
      }
    } --strip-components=1 -C "$out/translate_c-0.0.0-Q_BUWhVNBwDOEcIqub4VFPJPB6D9dgwzUMHTX5KWr8Xr"
    mkdir "$out/aro-0.0.0-JSD1Qk6lNgDdcDV4Vh7Sfy-34m2TluIVOdPzMmj_0BjX"
    tar -xf ${
      fetchurl {
        url = "https://github.com/vancluever/arocc/archive/f97cdfc3779aec4b242299e2fc9a1c828c3547c6.tar.gz";
        hash = "sha256-QsjKuRNbElGOavfcXkYfBVx4uGOtyo7Xd08vt/6ad4Q=";
      }
    } --strip-components=1 -C "$out/aro-0.0.0-JSD1Qk6lNgDdcDV4Vh7Sfy-34m2TluIVOdPzMmj_0BjX"
  '';
in
assert windows -> sdk != null;
stdenv.mkDerivation {
  pname = "ghostty-vt";
  version = "0.1.0-dev-35a81a9";
  src = fetchFromGitHub {
    owner = "ghostty-org";
    repo = "ghostty";
    rev = "35a81a980bb9fce09a1ea762a68b55f8eb3477ed";
    hash = "sha256-aL4Vz+HzEqRQwUcO2rybXh+RSBtcJ4NQCHRW479TAds=";
  };
  patches = [ ./ghostty-vt-static.patch ] ++ lib.optional windows ./ghostty-vt-uwp.patch;
  nativeBuildInputs = [ zig_0_16 ];
  strictDeps = true;
  dontConfigure = true;
  dontUseZigBuild = true;
  dontUseZigCheck = true;
  dontUseZigInstall = true;
  dontStrip = true; # Zig already emits release code; keep the COFF archive intact.
  buildPhase = ''
    runHook preBuild
    export ZIG_GLOBAL_CACHE_DIR="$TMPDIR/zig-cache"
    mkdir -p "$ZIG_GLOBAL_CACHE_DIR/tmp"
    ${lib.optionalString windows ''
      cat > libc.conf <<EOF
      include_dir=${sdk}/sdk/include/ucrt
      sys_include_dir=${sdk}/crt/include
      crt_dir=${sdk}/sdk/lib/ucrt/x86_64
      msvc_lib_dir=${sdk}/crt/lib/x86_64
      kernel32_lib_dir=${sdk}/sdk/lib/um/x86_64
      gcc_dir=
      EOF
    ''}
    zig build --system ${deps} -Demit-lib-vt -Dsimd=false \
      -Demit-themes=false -Dvt-features=-kitty-graphics -Doptimize=ReleaseFast \
      -Dcpu=baseline \
      ${lib.optionalString windows "-Dtarget=x86_64-windows-msvc --libc libc.conf"} \
      --prefix "$out" -j1
    runHook postBuild
  '';
  installPhase = ''
    runHook preInstall
    mkdir -p "$out/include" "$out/lib/pkgconfig"
    cp -r include/ghostty "$out/include/"
    cat > "$out/lib/pkgconfig/libghostty-vt.pc" <<EOF
    prefix=$out
    includedir=$out/include
    libdir=$out/lib
    Name: libghostty-vt
    Description: Ghostty virtual terminal engine (static, no SIMD or Kitty graphics)
    Version: 0.1.0
    Cflags: -I$out/include -DGHOSTTY_STATIC
    Libs: $out/lib/${if windows then "ghostty-vt-static.lib" else "libghostty-vt.a"}
    Libs.private: ${if windows then "-lwindowsapp" else "-lm"}
    EOF
    runHook postInstall
  '';
  meta = {
    description = "Ghostty virtual terminal engine, static C ABI";
    homepage = "https://github.com/ghostty-org/ghostty";
    license = lib.licenses.mit;
  };
}
