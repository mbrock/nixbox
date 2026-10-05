{
  libssh2,
  buildPackages,
  openssl-uwp,
  zlib,
}:
libssh2.overrideAttrs (old: {
  # Retain Nixpkgs' pinned release and security backports.
  patches = (old.patches or [ ]) ++ [ ./libssh2-uwp.patch ];
  outputs = [ "out" ];
  nativeBuildInputs = [
    buildPackages.cmake
    buildPackages.ninja
  ];
  buildInputs = [ ];
  propagatedBuildInputs = [
    openssl-uwp
    zlib
  ];
  configureFlags = [ ];
  cmakeFlags = [
    "-DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY"
    "-DCMAKE_POLICY_DEFAULT_CMP0091=NEW"
    "-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded"
    # Upstream's exported include path already prepends INSTALL_PREFIX.
    "-DCMAKE_INSTALL_INCLUDEDIR=include"
    "-DHAVE_GETTIMEOFDAY=OFF"
    "-DOPENSSL_ROOT_DIR=${openssl-uwp}"
    "-DCRYPTO_BACKEND=OpenSSL"
    "-DBUILD_SHARED_LIBS=OFF"
    "-DBUILD_STATIC_LIBS=ON"
    "-DBUILD_EXAMPLES=OFF"
    "-DBUILD_TESTING=OFF"
    "-DENABLE_ZLIB_COMPRESSION=ON"
    "-DCMAKE_C_FLAGS=-DLIBSSH2_WINDOWS_UWP"
  ];
  postInstall = ''
    # Clang's MSVC linker resolves -lfoo to foo.lib, not libfoo.lib.
    substituteInPlace "$out/lib/pkgconfig/libssh2.pc" \
      --replace-fail '-lssh2' '-llibssh2'
  '';
  doCheck = false;
  doInstallCheck = false;
})
