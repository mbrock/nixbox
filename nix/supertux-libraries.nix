# Dependencies needed by the SDL3-based SuperTux port. Apply this as a
# cross-overlay after xbox-pkgs' SDL3 overlay.
final: prev: {
  # These recipes are already SDL3-native in pinned Nixpkgs. Replace their SDL
  # dependency with nixbox's UWP static build and turn off optional plugins
  # whose runtime loading is unavailable in the app container.
  sdl3-image =
    (prev."sdl3-image".override {
      sdl3 = final.SDL3;
      enableTests = false;
      enableSTB = true;
    }).overrideAttrs
      (old: {
        buildInputs = [
          final.SDL3
          final.libpng
        ];
        outputs = [ "out" ];
        cmakeFlags = (old.cmakeFlags or [ ]) ++ [
          # The current MSVC intrinsic headers leave unresolved SSE helpers.
          "-DCMAKE_C_FLAGS=-DSTBI_NO_SIMD"
          "-DSDLIMAGE_BACKEND_STB=ON"
          "-DSDLIMAGE_AVIF=OFF"
          "-DSDLIMAGE_JXL=OFF"
          "-DSDLIMAGE_TIF=OFF"
          "-DSDLIMAGE_WEBP=OFF"
          "-DSDLIMAGE_TESTS=OFF"
          "-DSDLIMAGE_SAMPLES=OFF"
          "-DSDLIMAGE_DEPS_SHARED=OFF"
        ];
        doCheck = false;
      });
  SDL3_image = final.sdl3-image;

  sdl3-ttf =
    (prev."sdl3-ttf".override {
      sdl3 = final.SDL3;
    }).overrideAttrs
      (old: {
        buildInputs = [ ]; # Drop disabled upstream SVG/GLib dependencies.
        # The static installed CMake target and .pc require these downstream.
        propagatedBuildInputs = [
          final.SDL3
          final.freetype
          final.harfbuzz
        ];
        postPatch = (old.postPatch or "") + ''
          # Use the scalar glyph renderer with the current SDK/Clang headers.
          substituteInPlace src/SDL_ttf.c \
            --replace-fail '#if defined(__SSE2__)' '#if 0 /* UWP scalar renderer */'
        '';
        cmakeFlags = (old.cmakeFlags or [ ]) ++ [
          "-DSDLTTF_HARFBUZZ=ON"
          "-DSDLTTF_PLUTOSVG=OFF"
          "-DSDLTTF_SAMPLES=OFF"
          "-DSDLTTF_DEPS_SHARED=OFF"
        ];
        doCheck = false;
      });
  SDL3_ttf = final.sdl3-ttf;

  # Use upstream CMake builds instead of Unix configure scripts. Keep the
  # pinned sources and security patches, with only the runtime dependencies.
  libpng = prev.libpng.overrideAttrs (old: {
    nativeBuildInputs = [ final.buildPackages.cmake ];
    # Keep Windows metadata with the static archive to avoid an out/dev
    # reference cycle after the multiple-output hook moves the metadata.
    outputs = [ "out" ];
    outputBin = "out";
    postPatch =
      (old.postPatch or "")
      + "\n"
      + ''
        substituteInPlace CMakeLists.txt \
          --replace-fail 'if(NOT WIN32 OR CYGWIN OR MINGW)
          set(prefix' 'if(TRUE)
          set(prefix' \
          --replace-fail 'set(PNG_STATIC_OUTPUT_NAME "libpng''${PNGLIB_ABI_VERSION}_static")' 'set(PNG_STATIC_OUTPUT_NAME "png''${PNGLIB_ABI_VERSION}")' \
          --replace-fail 'set(LIBS "-lz -lm")' 'set(LIBS "")'
      '';
    cmakeFlags = (old.cmakeFlags or [ ]) ++ [
      "-DPNG_HARDWARE_OPTIMIZATIONS=OFF"
      "-DPNG_SHARED=OFF"
      "-DPNG_TESTS=OFF"
      "-DPNG_TOOLS=OFF"
    ];
    doCheck = false;
  });
  freetype = prev.freetype.overrideAttrs (old: {
    nativeBuildInputs = [ final.buildPackages.cmake ];
    propagatedBuildInputs = [
      final.zlib
      final.libpng
    ];
    postPatch =
      (old.postPatch or "")
      + "\n"
      + ''
        # App containers have no process environment; font options remain
        # configurable through FreeType's normal property API.
        substituteInPlace include/freetype/config/ftoption.h \
          --replace-fail '#define FT_CONFIG_OPTION_ENVIRONMENT_PROPERTIES' '/* No environment properties on UWP. */'
        # Use the portable stdio/allocator backend, not desktop CreateFileA or
        # Windows DLL version resources, for a statically linked UWP library.
        substituteInPlace CMakeLists.txt \
          --replace-fail 'builds/windows/ftsystem.c' 'src/base/ftsystem.c' \
          --replace-fail 'enable_language(RC)' "" \
          --replace-fail 'builds/windows/ftdebug.c' 'src/base/ftdebug.c' \
          --replace-fail 'src/base/ftver.rc' ""
      '';
    configureFlags = [ ];
    cmakeFlags = (old.cmakeFlags or [ ]) ++ [
      "-DFT_DISABLE_BZIP2=ON"
      "-DFT_DISABLE_BROTLI=ON"
      "-DFT_DISABLE_HARFBUZZ=ON"
    ];
    postInstall = ''
      # CMake's Nix install directories are already absolute.
      substituteInPlace "$out/lib/pkgconfig/freetype2.pc" \
        --replace-fail '${"$"}{prefix}/' ""
    '';
    doCheck = false;
  });
  harfbuzz = prev.harfbuzz.overrideAttrs (old: {
    nativeBuildInputs = [
      final.buildPackages.cmake
      final.buildPackages.pkg-config
    ];
    buildInputs = [ final.freetype ];
    propagatedBuildInputs = [ ];
    outputs = [
      "out"
      "dev"
    ];
    postPatch =
      (old.postPatch or "")
      + "\n"
      + ''
        # Clang's MSVC target diagnoses unused member templates differently;
        # retain the warning without promoting it to an error via -Wunused.
        substituteInPlace src/hb.hh \
          --replace-fail '#pragma GCC diagnostic error   "-Wunused"' '#pragma GCC diagnostic error   "-Wunused"
          #pragma GCC diagnostic warning "-Wunused-template"'
        # MSVC's CRT supplies math; pkg-config must not request Unix libm.
        substituteInPlace src/harfbuzz.pc.in src/harfbuzz-vector.pc.in src/harfbuzz-raster.pc.in \
          --replace-fail 'Libs.private: -lm ' 'Libs.private: '
      '';
    cmakeFlags = (old.cmakeFlags or [ ]) ++ [
      "-DHB_HAVE_FREETYPE=ON"
      "-DHB_HAVE_GLIB=OFF"
      "-DHB_BUILD_UTILS=OFF"
    ];
    doCheck = false;
  });
  libvorbis = prev.libvorbis.overrideAttrs (old: {
    nativeBuildInputs = [ final.buildPackages.cmake ];
    postPatch = (old.postPatch or "") + ''
      # Select the portable float-to-integer conversion, not SSE intrinsics.
      substituteInPlace lib/os.h \
        --replace-fail '#if (defined(_MSC_VER) && defined(_M_X64)) || (defined(__GNUC__) && defined (__SSE2_MATH__))' '#if 0 /* UWP scalar conversion */'
    '';
    cmakeFlags = (old.cmakeFlags or [ ]) ++ [ "-DCMAKE_POLICY_VERSION_MINIMUM=3.5" ];
    outputs = [
      "out"
      "dev"
    ];
    preConfigure = "";
    doCheck = false;
  });
  physfs = prev.physfs.overrideAttrs (old: {
    patches = (old.patches or [ ]) ++ [ ./supertux/physfs-winrt.patch ];
    cmakeFlags = (old.cmakeFlags or [ ]) ++ [
      "-DPHYSFS_BUILD_STATIC=ON"
      "-DPHYSFS_BUILD_SHARED=OFF"
      "-DPHYSFS_BUILD_TEST=OFF"
      "-DCMAKE_CXX_STANDARD=20"
    ];
    doCheck = false;
  });
  glm = prev.glm.overrideAttrs (old: {
    doCheck = false;
    meta = old.meta // {
      platforms = final.lib.platforms.all;
    };
  });
  fmt = prev.fmt.overrideAttrs (old: {
    cmakeFlags = (old.cmakeFlags or [ ]) ++ [ "-DFMT_TEST=OFF" ];
    doCheck = false;
  });

  # OpenAL Soft has a native Windows audio backend. Its Nixpkgs recipe marks
  # the package Unix-only and enables Linux desktop backends by default on
  # every non-Darwin target, so select only the built-in Windows backend.
  openal-soft =
    (prev.openal-soft.override {
      alsaSupport = false;
      dbusSupport = false;
      pipewireSupport = false;
      pulseSupport = false;
    }).overrideAttrs
      (old: {
        postPatch = (old.postPatch or "") + ''
          substituteInPlace CMakeLists.txt \
            --replace-fail 'CXX_STANDARD 17' 'CXX_STANDARD 20'
          substituteInPlace common/dynload.cpp \
            --replace-fail 'LoadLibraryW(wname.c_str())' 'LoadPackagedLibrary(wname.c_str(), 0)'
          # The pinned SDK exposes GetModuleFileNameW to apps. The alternative
          # UWP code assumes desktop CRT globals absent from our static UWP CRT.
          substituteInPlace core/helpers.cpp \
            --replace-fail '#if !ALSOFT_UWP
                  DWORD pathlen' '#if 1
                  DWORD pathlen'
          substituteInPlace common/strutils.cpp \
            --replace-fail '#ifdef _GAMING_XBOX' '#if ALSOFT_UWP
              const char *str = nullptr;
            #elif defined(_GAMING_XBOX)' \
            --replace-fail 'const WCHAR *str{_wgetenv(envname)};' '#if ALSOFT_UWP
              const WCHAR *str = nullptr;
            #else
              const WCHAR *str{_wgetenv(envname)};
            #endif'
        '';
        cmakeFlags = (old.cmakeFlags or [ ]) ++ [
          "-DLIBTYPE=STATIC"
          "-DALSOFT_UWP=ON"
          # As with ImGui, the SDK intrinsic declarations do not provide
          # linkable SSE helpers. Use OpenAL's supported scalar paths.
          "-DALSOFT_CPUEXT_SSE=OFF"
          "-DCMAKE_CXX_FLAGS=-DPFFFT_SIMD_DISABLE"
          "-DALSOFT_UTILS=OFF"
          "-DALSOFT_EXAMPLES=OFF"
          "-DALSOFT_TESTS=OFF"
          "-DALSOFT_BACKEND_WASAPI=ON"
          "-DALSOFT_BACKEND_WINMM=OFF"
          "-DALSOFT_BACKEND_DSOUND=OFF"
        ];
        meta = old.meta // {
          platforms = final.lib.platforms.windows;
          description = "OpenAL Soft using its native Windows audio backend";
        };
      });
}
