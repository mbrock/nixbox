{ xbox }:
xbox.stdenv.mkDerivation {
  name = "libssh2-consumer";
  dontUnpack = true;
  dontConfigure = true;
  nativeBuildInputs = [
    xbox.pkgsXbox.buildPackages.pkg-config
    xbox.pkgs.cmake
    xbox.pkgs.ninja
  ];
  buildInputs = [ xbox.pkgsXbox.libssh2 ];
  buildPhase = ''
    $CXX -std=c++17 ${../probes/ssh/ssh-probe.cpp} -o ssh-probe.exe \
      $($PKG_CONFIG --cflags --libs --static libssh2)
    mkdir cmake-source
    cat > cmake-source/CMakeLists.txt <<'EOF'
    cmake_minimum_required(VERSION 3.20)
    project(ssh_consumer LANGUAGES CXX)
    find_package(Libssh2 REQUIRED)
    add_executable(ssh-cmake ${../probes/ssh/ssh-probe.cpp})
    target_compile_features(ssh-cmake PRIVATE cxx_std_17)
    target_link_libraries(ssh-cmake PRIVATE Libssh2::libssh2)
    EOF
    cmake -S cmake-source -B cmake-build -GNinja \
      -DCMAKE_TOOLCHAIN_FILE=${xbox.toolchainFile} -DCMAKE_BUILD_TYPE=Release
    cmake --build cmake-build
  '';
  installPhase = ''
    mkdir -p "$out/bin"
    cp ssh-probe.exe cmake-build/ssh-cmake.exe "$out/bin/"
  '';
}
