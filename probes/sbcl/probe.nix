{ xbox, sbcl }:
xbox.mkXboxApp {
  pname = "sbcl-probe";
  version = "0.1.0";
  displayName = "Common Lisp on Xbox";
  description = "SBCL native compilation, garbage collection, threads and file IO";
  src = xbox.pkgs.lib.fileset.toSource {
    root = ./.;
    fileset = xbox.pkgs.lib.fileset.unions [ ./CMakeLists.txt ./main.cpp ./probe.lisp ];
  };
  nativeBuildInputs = [ xbox.pkgs.cmake xbox.pkgs.ninja xbox.pkgs.python3 ];
  buildInputs = [ xbox.pkgsXbox.SDL3 ];
  cmakeFlags = [ "-DSBCL_ROOT=${sbcl}" ];
  postInstall = ''
    python3 ${./check-exports.py} "$out/bin/sbcl-probe.exe"
  '';
  capabilities = [ "codeGeneration" ];
}
