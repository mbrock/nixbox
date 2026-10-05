# Experimental Win32 runtime/core for the Xbox probe, built through pkgsXbox.
# nix build .#sbcl-runtime --keep-failed -L
{ pkgs, pkgsXbox }:
let
  sbcl = pkgsXbox.sbcl.override {
    bootstrapLisp = "${pkgs.sbcl}/bin/sbcl --disable-debugger --no-userinit --no-sysinit";
    coreutils = pkgs.coreutils;
  };
in
sbcl.overrideAttrs (old: {
  # Avoid unrelated zstd/grep/PCRE2 cross builds while probing SBCL itself.
  coreCompression = false;
  # Start with the Windows backend's conventional generational collector and
  # direct runtime objects, before adding optional library/GC configurations.
  markRegionGC = false;
  linkableRuntime = false;
  nativeBuildInputs = old.nativeBuildInputs ++ [ pkgs.wineWow64Packages.stable ];
  patches = (old.patches or [ ]) ++ [ ./sbcl-msvc-headers.patch ./sbcl-msvc-runtime.patch ];
  postPatch = (if old.postPatch == null then "" else old.postPatch) + ''
    substituteInPlace make-config.sh \
      --replace-fail 'case `uname` in' 'case WindowsNT in' \
      --replace-fail '|| tools-for-build/avx2 ;' '|| wine tools-for-build/avx2 ;' \
      --replace-fail 'tools-for-build/determine-endianness >>' 'wine tools-for-build/determine-endianness >>'
    substituteInPlace tools-for-build/grovel-features.sh \
      --replace-fail './$bin>' 'wine ./$bin>'
    substituteInPlace make-target-1.sh \
      --replace-fail '    tools-for-build/grovel-headers >' '    wine tools-for-build/grovel-headers >'
    substituteInPlace make-target-2.sh \
      --replace-fail './src/runtime/sbcl ' 'wine ./src/runtime/sbcl.exe '
    substituteInPlace src/runtime/Config.x86-64-win32 \
      --replace-fail 'CC = gcc' 'CC = $(SBCL_CC)'
  '';
  buildPhase = ''
    runHook preBuild
    export WINEPREFIX="$TMPDIR/wine" WINEDEBUG=-all
    # Wine's Unix-drive filenames need a UTF-8 host locale, including in Nix.
    export LANG=C.UTF-8
    export WINEDLLOVERRIDES="mscoree,mshtml="
    # Collect independent compiler failures without treating them as success.
    export CC="$CC -include $PWD/src/runtime/nixbox-win32.h"
    export SBCL_CC="$CC" SBCL_MAKE_JOBS="-k -j$NIX_BUILD_CORES"
    # Xbox's CPU is not the build machine: do not select AVX-512 under Wine.
    sh make-config.sh ${pkgs.lib.concatStringsSep " " old.buildArgs} \
      --without-avx512 --without-sb-simd-pack-512 --check-host-lisp
    # This generator is invoked by the native bootstrap Lisp, not the target.
    make -C tools-for-build perfecthash CC=${pkgs.stdenv.cc}/bin/cc
    sh make-host-1.sh
    sh make-target-1.sh
    sh make-host-2.sh
    export SBCL_MAKE_TARGET_2_OPTIONS="--disable-ldb --disable-debugger"
    sh make-target-2.sh
    # stdenv skips target checks when cross-compiling; Wine can run ours.
    eval "$checkPhase"
    runHook postBuild
  '';
  doCheck = false;
  checkPhase = ''
    runHook preCheck
    wine src/runtime/sbcl.exe --core output/sbcl.core \
      --disable-ldb --disable-debugger --no-userinit --no-sysinit \
      --load "$(winepath -w ${../../probes/sbcl/probe.lisp})" \
      --eval '(assert (zerop (nixbox-sbcl:run "probe-results.txt")))' --quit
    cat probe-results.txt
    runHook postCheck
  '';
  installPhase = ''
    runHook preInstall
    mkdir -p "$out/bin" "$out/lib/sbcl" "$out/share/sbcl"
    cp src/runtime/sbcl.exe "$out/bin/"
    cp output/sbcl.core "$out/lib/sbcl/"
    # Embed the runtime in a GUI app, sharing its static CRT, without SBCL main.
    for object in src/runtime/*.o; do
      if [[ "$object" != src/runtime/main.o ]]; then
        $AR r "$out/lib/sbcl/sbcl-runtime.lib" "$object"
      fi
    done
    $AR r "$out/lib/sbcl/sbcl-runtime.lib" tlsf-bsd/tlsf/tlsf.o
    $AR s "$out/lib/sbcl/sbcl-runtime.lib"
    sed '/^main$/d' src/runtime/sbcl.def > "$out/lib/sbcl/sbcl.def"
    cp src/runtime/nixbox-kernelbase.lib "$out/lib/sbcl/"
    cp COPYING CREDITS "$out/share/sbcl/"
    cp tlsf-bsd/LICENSE "$out/share/sbcl/TLSF-LICENSE"
    runHook postInstall
  '';
  # Stripping COFF archive members invalidates their .llvm_addrsig indices.
  # Keep the objects intact for the embedding link and its debug symbols.
  dontStrip = true;
  postFixup = "";
  doInstallCheck = false;
  # Permit this one experiment, without declaring Windows support globally.
  meta = old.meta // { platforms = [ "x86_64-windows" ]; };
})
