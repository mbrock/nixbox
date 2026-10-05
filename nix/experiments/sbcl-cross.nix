# Win32 bootstrap experiment, not yet a supported Xbox SBCL package.
# nix build --impure --file nix/experiments/sbcl-cross.nix --keep-failed -L
let
  flake = builtins.getFlake (toString ../..);
  pkgs = flake.lib.x86_64-linux.pkgs;
  pkgsXbox = flake.legacyPackages.x86_64-linux.pkgsXbox;
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
  patches = (old.patches or [ ]) ++ [ ./sbcl-msvc-headers.patch ];
  postPatch = (if old.postPatch == null then "" else old.postPatch) + ''
    substituteInPlace make-config.sh \
      --replace-fail 'case `uname` in' 'case WindowsNT in' \
      --replace-fail '|| tools-for-build/avx2 ;' '|| wine tools-for-build/avx2 ;' \
      --replace-fail 'tools-for-build/determine-endianness >>' 'wine tools-for-build/determine-endianness >>'
    substituteInPlace tools-for-build/grovel-features.sh \
      --replace-fail './$bin>' 'wine ./$bin>'
    substituteInPlace make-target-1.sh \
      --replace-fail '    tools-for-build/grovel-headers >' '    wine tools-for-build/grovel-headers >'
    substituteInPlace src/runtime/Config.x86-64-win32 \
      --replace-fail 'CC = gcc' 'CC = $(SBCL_CC)'
  '';
  buildPhase = ''
    runHook preBuild
    export WINEPREFIX="$TMPDIR/wine" WINEDEBUG=-all
    export WINEDLLOVERRIDES="mscoree,mshtml="
    # Collect independent compiler failures without treating them as success.
    export SBCL_CC="$CC" SBCL_MAKE_JOBS="-k -j$NIX_BUILD_CORES"
    sh make-config.sh ${pkgs.lib.concatStringsSep " " old.buildArgs} --check-host-lisp
    # This generator is invoked by the native bootstrap Lisp, not the target.
    make -C tools-for-build perfecthash CC=${pkgs.stdenv.cc}/bin/cc
    sh make-host-1.sh
    sh make-target-1.sh
    sh make-host-2.sh
    runHook postBuild
  '';
  # Permit this one experiment, without declaring Windows support globally.
  meta = old.meta // { platforms = [ "x86_64-windows" ]; };
})
