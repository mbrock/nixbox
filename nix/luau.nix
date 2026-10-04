# Nixpkgs already supplies the pinned source and native CMake. Keep only the
# adaptations for a static, library-only build targeting the Xbox MSVC ABI.
{ lib, stdenv, luau }:
luau.overrideAttrs (old: {
  # Nixpkgs assumes every Clang target needs LLVM libunwind. This target uses
  # MSVC's CRT exception runtime instead; it does not provide libunwind's API.
  buildInputs = builtins.filter (
    dependency:
    lib.getName dependency != "libunwind" || lib.meta.availableOn stdenv.hostPlatform dependency
  ) (
    old.buildInputs or [ ]
  );
  cmakeFlags = (old.cmakeFlags or [ ]) ++ [
    "-DLUAU_STATIC_CRT=ON"
    "-DLUAU_BUILD_CLI=OFF"
    "-DLUAU_BUILD_TESTS=OFF"
  ];
  buildPhase = ''
    runHook preBuild
    cmake --build . --target Luau.VM Luau.Compiler -j "$NIX_BUILD_CORES"
    runHook postBuild
  '';
  installPhase = ''
    runHook preInstall
    mkdir -p "$out/lib" "$out/include"
    cp *.lib "$out/lib/"
    for component in VM Common Ast Bytecode Compiler; do
      cp -r "../$component/include/." "$out/include/"
    done
    runHook postInstall
  '';
  meta = builtins.removeAttrs old.meta [ "mainProgram" ];
  doCheck = false;
})
