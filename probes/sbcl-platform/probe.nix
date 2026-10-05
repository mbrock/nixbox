{ xbox, codeGeneration ? true }:
xbox.mkXboxApp {
  pname = "sbcl-platform-probe";
  version = "0.1.0";
  identity = if codeGeneration then "Nixbox.sbcl-platform-probe" else "Nixbox.sbcl-platform-control";
  displayName = if codeGeneration then "SBCL platform probe" else "SBCL platform control";
  description = "Generated x64 code, page faults, unwind tables and thread TLS";
  src = xbox.pkgs.lib.fileset.toSource {
    root = ./.;
    fileset = xbox.pkgs.lib.fileset.unions [
      ./CMakeLists.txt
      ./main.cpp
      ./probe.c
      ./probe.def
    ];
  };
  nativeBuildInputs = [ xbox.pkgs.cmake xbox.pkgs.ninja xbox.pkgs.llvmPackages.llvm ];
  buildInputs = [ xbox.pkgsXbox.SDL3 ];
  cmakeFlags = [ "-DPROBE_CODEGEN=${if codeGeneration then "1" else "0"}" ];
  capabilities = xbox.pkgs.lib.optional codeGeneration "codeGeneration";
}
