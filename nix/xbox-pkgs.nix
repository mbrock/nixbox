{
  pkgs,
  llvmPackages,
  llvmPackageSet,
  inputs,
  toolchain,
}:
let
  inherit (pkgs) lib;
  platform = lib.systems.elaborate {
    config = "x86_64-pc-windows-msvc";
    isXbox = true;
  };
  compilerStdenv = pkgs.stdenvNoCC.override { targetPlatform = platform; };
  sdk = toolchain.UWP_XWIN_ROOT;
  cxxHeaders = import ./cxx-headers.nix { inherit pkgs sdk; };
  targetBintools =
    pkgs.runCommand "xbox-binutils-${llvmPackages.llvm.version}"
      {
        passthru.isLLVM = true;
      }
      ''
        mkdir -p "$out/bin"
        for tool in ar ranlib nm objcopy objdump readelf strip strings size; do
          ln -s ${llvmPackages.llvm}/bin/llvm-$tool "$out/bin/${platform.config}-$tool"
        done
        ln -s ${llvmPackages.lld}/bin/lld-link "$out/bin/${platform.config}-ld"
        ln -s ${llvmPackages.llvm}/bin/llvm-rc "$out/bin/${platform.config}-rc"
      '';
  bintools = pkgs.wrapBintoolsWith {
    stdenvNoCC = compilerStdenv;
    bintools = targetBintools;
    libc = null;
    defaultHardeningFlags = [ ];
  };
  xboxCC = pkgs.wrapCCWith {
    name = "xbox-clang";
    stdenvNoCC = compilerStdenv;
    cc = llvmPackages.clang-unwrapped;
    inherit bintools;
    libc = null;
    gccForLibs = null;
    extraBuildCommands = ''
      echo '-DWINAPI_FAMILY=WINAPI_FAMILY_APP -D__WRL_NO_DEFAULT_LIB__ -fms-runtime-lib=static -mcx16 -fuse-ld=lld' >> "$out/nix-support/cc-cflags"
      # The MSVC driver searches for this spelling, independently of GNU ld.
      ln -s ${llvmPackages.lld}/bin/lld-link "$out/bin/lld-link"
      echo '-B${llvmPackages.lld}/bin' >> "$out/nix-support/cc-cflags"
      # Header overrides must precede the unmodified SDK's system includes.
      # This directory contains C++ headers only; nothing is force-included.
      echo '-isystem ${cxxHeaders}/include' >> "$out/nix-support/cc-cflags"
      # Keep implicit headers out of the package compiler: configure probes
      # must see only the declarations they explicitly include. The app's
      # uwp-crossbuild driver supplies its own C++/WinRT compatibility header.
      for dir in crt/include sdk/include/ucrt sdk/include/um sdk/include/shared sdk/include/winrt sdk/include/cppwinrt; do
        echo "-isystem ${sdk}/$dir" >> "$out/nix-support/cc-cflags"
      done
      echo '-L${sdk}/crt/lib/x86_64 -L${sdk}/sdk/lib/ucrt/x86_64 -L${sdk}/sdk/lib/um/x86_64' >> "$out/nix-support/cc-ldflags"
    '';
  };
  # The SDK implements MSVC exception handling, not the libunwind API. Keep
  # these packages inspectable for availableOn checks, but reject builds.
  unsupportedUnwinder =
    package:
    package.overrideAttrs (old: {
      meta = old.meta // {
        badPlatforms = (old.meta.badPlatforms or [ ]) ++ [ platform.system ];
      };
    });
  xboxOverlay = final: prev: {
    xboxCxxHeaders = cxxHeaders;
    libunwind = unsupportedUnwinder prev.libunwind;
    llvmPackages = prev.${llvmPackageSet}.overrideScope (
      llvmFinal: llvmPrev: {
        libunwind = unsupportedUnwinder llvmPrev.libunwind;
      }
    );
    # Reuse Nixpkgs' SDL3 recipe with the fork's C++/WinRT UWP backend.
    sdl3 = final.callPackage ./sdl3.nix { sdl3 = prev.sdl3; };
    SDL3 = final.sdl3;
    # NXT owns the portable runtime's build and installed consumer check.
    nxtrt-iocp = final.callPackage (inputs.nxtui + "/nix/iocp.nix") { };
    luau = import ./luau.nix {
      inherit lib;
      inherit (final) stdenv;
      luau = prev.luau;
    };
    hello = prev.hello.overrideAttrs (old: {
      patches = (old.patches or [ ]) ++ [ ./hello-uwp.patch ];
      configureFlags = (old.configureFlags or [ ]) ++ [ "--disable-nls" ];
      doCheck = false; # Windows target binaries cannot run in the Linux builder.
      doInstallCheck = false;
      postInstallCheck = "";
    });
    # Keep Nixpkgs' pinned source and patches; adapt its Unix configure recipe
    # to the upstream CMake build, which understands the MSVC ABI.
    zlib = (prev.zlib.override { shared = false; }).overrideAttrs (old: {
      nativeBuildInputs = [
        final.buildPackages.cmake
        final.buildPackages.ninja
      ];
      env = { };
      preConfigure = "";
      configureFlags = [ ];
      makeFlags = [ ];
      installFlags = [ ];
      postPatch = (old.postPatch or "") + ''
        substituteInPlace zlib.pc.cmakein \
          --replace-fail '${"$"}{exec_prefix}/@CMAKE_INSTALL_LIBDIR@' '@CMAKE_INSTALL_FULL_LIBDIR@' \
          --replace-fail '${"$"}{exec_prefix}/@CMAKE_INSTALL_INCLUDEDIR@' '@CMAKE_INSTALL_FULL_INCLUDEDIR@' \
          --replace-fail '-lz' '-lzs'
        # Unqualified find_package(ZLIB CONFIG) must not import a shared
        # target we deliberately do not build.
        substituteInPlace zlibConfig.cmake.in \
          --replace-fail 'set(_ZLIB_supported_components "shared" "static")' 'set(_ZLIB_supported_components "static")' \
          --replace-fail 'endif(ZLIB_FIND_COMPONENTS)' 'endif(ZLIB_FIND_COMPONENTS)
        if(NOT TARGET ZLIB::ZLIB)
          add_library(ZLIB::ZLIB ALIAS ZLIB::ZLIBSTATIC)
        endif()'
      '';
      cmakeFlags = (old.cmakeFlags or [ ]) ++ [
        "-DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY"
        "-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded"
        "-DZLIB_BUILD_SHARED=OFF"
        "-DZLIB_BUILD_STATIC=ON"
        "-DZLIB_BUILD_TESTING=OFF"
      ];
      doCheck = false; # The target compression/decompression check runs on Xbox.
      # FindZLIB searches for z.lib, not upstream's Windows static name zs.lib.
      postInstall = ''
        ln -s zs.lib "$out/lib/z.lib"
      '';
      postFixup = ''
        test -f "$out/lib/zs.lib"
        ${llvmPackages.llvm}/bin/llvm-readobj --file-headers "$out/lib/zs.lib" > "$TMPDIR/zlib-headers"
        grep -q IMAGE_FILE_MACHINE_AMD64 "$TMPDIR/zlib-headers"
        ! grep -q 'Format: elf' "$TMPDIR/zlib-headers"
      '';
    });
  };
  mkPkgsXbox =
    {
      crossOverlays ? [ ],
    }:
    import inputs.nixpkgs {
      localSystem = pkgs.stdenv.hostPlatform.system;
      crossSystem = platform;
      config.replaceCrossStdenv =
        { buildPackages, baseStdenv }:
        pkgs.stdenvAdapters.overrideMkDerivationArgs
          (args: {
            # The SDK has release CRT libraries only. Apply this to every
            # CMake package, while allowing an explicit package override.
            cmakeFlags = [
              "-DCMAKE_TRY_COMPILE_CONFIGURATION=Release"
              "-DCMAKE_POLICY_DEFAULT_CMP0091=NEW"
              "-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded"
            ]
            ++ (args.cmakeFlags or [ ]);
          })
          (
            pkgs.stdenvAdapters.makeStaticLibraries (
              # Some recipes use optionalDrvAttr and thus supply null here.
              # makeStaticLibraries expects a Boolean, even for spliced packages
              # that are evaluated but will actually build on the Linux side.
              pkgs.stdenvAdapters.overrideMkDerivationArgs (args: {
                dontAddStaticConfigureFlags = (args.dontAddStaticConfigureFlags or false) == true;
              }) (baseStdenv.override { cc = xboxCC; })
            )
          );
      crossOverlays = [
        xboxOverlay
        (import ./arcade-libraries.nix)
        (import ./supertux-libraries.nix)
      ]
      ++ crossOverlays;
    };
  pkgsXbox = mkPkgsXbox { };
in
{
  inherit
    xboxCC
    pkgsXbox
    mkPkgsXbox
    cxxHeaders
    ;
}
