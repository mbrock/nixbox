# Existing-game port. `xbox` is the nixbox.lib.<system> interface.
xbox:
xbox.mkXboxApp {
  pname = "supertux";
  # MSIX requires numeric version components; the precise commit is pinned in
  # the source fetch rather than encoded in this package version.
  version = "0.7.0.0";
  displayName = "SuperTux";
  description = "Classic 2D jump'n run sidescroller game";
  executable = "supertux2.exe";
  backgroundColor = "#000000";
  capabilities = [ "internetClient" ];

  src = xbox.pkgs.fetchFromGitHub {
    owner = "SuperTux";
    repo = "supertux";
    rev = "00673d1dfefeedf39aaf502ac0cfb1fd005174d6";
    hash = "sha256-4P0xEKtlKR0Ot2vkctI2Tkt+UiYuFWGxurBodxUTLdI=";
    fetchSubmodules = true;
  };
  patches = [ ./uwp.patch ];
  postPatch = ''
    # The fork's standard C++/WinRT SDL entry point requires C++20.
    substituteInPlace CMakeLists.txt \
      --replace-fail 'set(CMAKE_CXX_STANDARD 17)' 'set(CMAKE_CXX_STANDARD 20)'
    substituteInPlace src/supertux/main.cpp \
      --replace-fail 'u8"/console.out"' '"/console.out"' \
      --replace-fail 'u8"/console.err"' '"/console.err"'
    # SDL already supplies the absolute package path. canonical() opens the
    # directory through desktop filesystem APIs, which Xbox denies to apps.
    substituteInPlace src/supertux/main.cpp \
      --replace-fail 'std::filesystem::canonical(m_datadir).string()' 'm_datadir'
    # This object target does not inherit the game's WIN32 define.
    substituteInPlace external/findlocale/findlocale.c \
      --replace-fail 'WIN32' '_WIN32'
    # Desktop DbgHelp/SEH diagnostics cannot run in the Xbox app container;
    # retain SDL's error reporting and use Device Portal for crash dumps.
    substituteInPlace src/supertux/error_handler.cpp src/supertux/error_handler.hpp \
      --replace-fail '#ifdef WIN32' '#if defined(WIN32) && !defined(SDL_PLATFORM_WINRT)'
    substituteInPlace src/supertux/error_handler.hpp \
      --replace-fail '#include <iostream>' '#include <iostream>
      #include <SDL3/SDL_platform.h>'
    substituteInPlace external/simplesquirrel/libs/squirrel/sqstdlib/sqstdsystem.cpp \
      --replace-fail '#define scgetenv _wgetenv' '#define scgetenv(name) nullptr' \
      --replace-fail '#define scgetenv getenv' '#define scgetenv(name) nullptr' \
      --replace-fail 'sq_pushinteger(v,scsystem(s));' 'return sq_throwerror(v, _SC("Shell commands are unavailable on Xbox"));'
  '';
  nativeBuildInputs = [
    xbox.pkgs.cmake
    xbox.pkgs.ninja
    xbox.pkgsXbox.buildPackages.pkg-config
  ];
  buildInputs = with xbox.pkgsXbox; [
    SDL3
    SDL3_image
    SDL3_ttf
    openal
    physfs
    libogg
    libvorbis
    fmt
    glm
    libpng
    zlib
    freetype
    harfbuzz
  ];
  cmakeFlags = [
    "-DENABLE_OPENGL=OFF"
    "-DENABLE_NETWORKING=OFF"
    "-DENABLE_DISCORD=OFF"
    "-DIS_SUPERTUX_RELEASE=ON"
    "-DUSE_STATIC_SIMPLESQUIRREL=ON"
    "-DSSQ_BUILD_INSTALL=OFF"
    "-DSQ_DISABLE_INSTALLER=ON"
    "-DSUPERTUX_LTO=OFF"
    "-DINSTALL_SUBDIR_BIN=bin"
    "-DINSTALL_SUBDIR_SHARE=share/supertux"
    "-DINSTALL_SUBDIR_DOC=share/supertux/doc"
    "-DBUILD_TESTING=OFF"
  ];
  doCheck = false;

  # SuperTux's executable is naturally built with main(), not wWinMain().
  # The app packaging helper sets the GUI subsystem on the finished PE file.
}
