# SDL3 with its UWP backend, from XboxEmulationHub's SDL3-uwp fork. Upstream
# writes that backend in C++/CX, which only MSVC compiles; sdl3-uwp.patch
# ports it to standard C++/WinRT so Clang builds it. XAML embedding, the Game
# Bar hooks, and OpenGL ES (ANGLE) are left out.
{
  sdl3,
  fetchFromGitHub,
}:
(sdl3.override {
  openglSupport = false;
  vulkanSupport = false;
  traySupport = false;
}).overrideAttrs (old: {
  pname = "sdl3-uwp";
  # The UWP backend is no longer in upstream SDL; pin the fork independently
  # of Nixpkgs' version while reusing its CMake recipe and split outputs.
  version = "3.4.16";
  src = fetchFromGitHub {
    owner = "XboxEmulationHub";
    repo = "SDL3-uwp";
    rev = "ddd5d4f99a26ac95d4620ccf489c07fcf5f08636";
    hash = "sha256-KWyLbL52kIC9cfpiaQXRemsT/eB2/vEVlH9YnayqUA0=";
  };
  patches = (old.patches or [ ]) ++ [ ./sdl3-uwp.patch ];
  cmakeFlags = (old.cmakeFlags or [ ]) ++ [
    # CMake's WindowsStore platform assumes MSVC; this selects SDL's backend.
    "-DWINDOWS_STORE=ON"
    "-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded"
    # SDL's Windows default is $out/cmake, outside Nixpkgs' dev-output fixup.
    "-DSDL_INSTALL_CMAKEDIR_ROOT=${placeholder "dev"}/lib/cmake/SDL3"
    "-DSDL_SHARED=OFF"
    "-DSDL_STATIC=ON"
    "-DSDL_TEST_LIBRARY=OFF"
    "-DSDL_TESTS=OFF"
    "-DSDL_INSTALL_TESTS=OFF"
    "-DSDL_EXAMPLES=OFF"
    # Desktop-only or unavailable in the app container.
    "-DSDL_OPENGLES=OFF"
    "-DSDL_HIDAPI=OFF"
    "-DSDL_GPU=OFF"
    "-DSDL_RENDER_GPU=OFF"
  ];
  # Target executables cannot run in the builder; no installed-test output.
  doCheck = false;
  outputs = builtins.filter (output: output != "installedTests") old.outputs;
  postInstall = "";
  meta = old.meta // {
    description = "SDL3 with a C++/WinRT UWP backend, for Xbox";
    homepage = "https://github.com/XboxEmulationHub/SDL3-uwp";
    changelog = "https://github.com/XboxEmulationHub/SDL3-uwp/commits/ddd5d4f99a26ac95d4620ccf489c07fcf5f08636";
  };
})
