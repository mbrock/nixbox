# SDL3 with its UWP backend, from XboxEmulationHub's SDL3-uwp fork. Upstream
# writes that backend in C++/CX, which only MSVC compiles; sdl3-uwp.patch
# ports it to standard C++/WinRT so Clang builds it. XAML embedding, the Game
# Bar hooks, and OpenGL ES (ANGLE) are left out.
{
  lib,
  stdenv,
  fetchFromGitHub,
  cmake,
  ninja,
}:
stdenv.mkDerivation {
  pname = "sdl3-uwp";
  version = "3.4.16";
  src = fetchFromGitHub {
    owner = "XboxEmulationHub";
    repo = "SDL3-uwp";
    rev = "ddd5d4f99a26ac95d4620ccf489c07fcf5f08636";
    hash = "sha256-KWyLbL52kIC9cfpiaQXRemsT/eB2/vEVlH9YnayqUA0=";
  };
  patches = [ ./sdl3-uwp.patch ];
  nativeBuildInputs = [
    cmake
    ninja
  ];
  cmakeFlags = [
    # CMake's WindowsStore platform assumes MSVC; this selects SDL's backend.
    "-DWINDOWS_STORE=ON"
    "-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded"
    "-DSDL_SHARED=OFF"
    "-DSDL_STATIC=ON"
    "-DSDL_TESTS=OFF"
    "-DSDL_EXAMPLES=OFF"
    # Desktop-only or unavailable in the app container.
    "-DSDL_OPENGL=OFF"
    "-DSDL_OPENGLES=OFF"
    "-DSDL_VULKAN=OFF"
    "-DSDL_HIDAPI=OFF"
    "-DSDL_GPU=OFF"
    "-DSDL_RENDER_GPU=OFF"
  ];
  meta = {
    description = "SDL3 with a C++/WinRT UWP backend, for Xbox";
    homepage = "https://github.com/XboxEmulationHub/SDL3-uwp";
    license = lib.licenses.zlib;
  };
}
