# This check has no access to Ghostty's private source/build directories.
{
  lib,
  stdenv,
  pkg-config,
  ghostty-vt,
  llvmPackages,
  wine ? null,
}:
let
  windows = stdenv.hostPlatform.isWindows;
in
stdenv.mkDerivation {
  name = "ghostty-vt-installed-consumer";
  src = ../probes/ghostty-vt;
  nativeBuildInputs = [ pkg-config ] ++ lib.optional (windows && wine != null) wine;
  buildInputs = [ ghostty-vt ];
  strictDeps = true;
  dontConfigure = true;
  buildPhase = ''
    runHook preBuild
    $CC -std=c11 -Wall -Wextra -Werror consumer.c -o consumer${lib.optionalString windows ".exe"} \
      $($PKG_CONFIG --cflags --libs --static libghostty-vt)
    ${lib.optionalString windows ''
      ${llvmPackages.llvm}/bin/llvm-readobj --coff-imports consumer.exe > imports.txt
      ${llvmPackages.llvm}/bin/llvm-nm --undefined-only \
        ${ghostty-vt}/lib/ghostty-vt-static.lib > undefined.txt
      # Audit the entire archive too: a small consumer's dead stripping could
      # otherwise conceal desktop APIs in less frequently used entry points.
      awk '$1 == "U" { print $2 }' undefined.txt | sort -u > symbols.txt
      printf '%s\n' DiscardVirtualMemory VirtualAllocFromApp VirtualFree \
        __chkstk _fltused _msize ceil exp expf free log logf malloc memcpy \
        memmove memset realloc round trunc truncf | sort -u > allowed.txt
      comm -23 symbols.txt allowed.txt > unexpected.txt
      if test -s unexpected.txt; then
        echo "Unreviewed Ghostty Windows imports:"
        cat unexpected.txt
        exit 1
      fi
      ! grep -iE 'ntdll|ghostty.*dll' imports.txt
      grep -q VirtualAllocFromApp imports.txt
    ''}
    ${lib.optionalString (windows && wine != null) ''
      # Cross stdenv suppresses checkPhase, so run the Windows checks explicitly.
      export WINEPREFIX="$TMPDIR/wine"
      export WINEDEBUG=-all
      export WINEDLLOVERRIDES="mscoree,mshtml="
      mkdir -p "$WINEPREFIX"
      wine ./consumer.exe > behavior.txt
      cat behavior.txt
      grep -q 'ghostty-vt installed C consumer: all checks passed' behavior.txt
    ''}
    runHook postBuild
  '';
  doCheck = !windows;
  checkPhase = ''
    runHook preCheck
    ./consumer
    runHook postCheck
  '';
  installPhase = ''
    mkdir -p "$out/bin"
    cp consumer${lib.optionalString windows ".exe"} "$out/bin/"
    ${lib.optionalString windows ''cp imports.txt undefined.txt "$out/"''}
    ${lib.optionalString (windows && wine != null) ''cp behavior.txt "$out/"''}
  '';
}
