# The XAML sample, described for nixbox. `xbox` is nixbox.lib.x86_64-linux.
xbox:
xbox.mkXboxApp {
  pname = "hello";
  version = "0.1.0";
  displayName = "hello-uwp";
  description = "A UWP application compiled on Linux";
  backgroundColor = "#0E1116";
  # App derives from Windows.UI.Xaml.Application, so it is a runtimeclass.
  idl = ./app.idl;
  entryPoint = "hello.App";
  src = xbox.pkgs.lib.fileset.toSource {
    root = ./.;
    fileset = xbox.pkgs.lib.fileset.unions [
      ./CMakeLists.txt
      (xbox.pkgs.lib.fileset.fileFilter (file: file.hasExt "cpp" || file.hasExt "h") ./.)
      ./Shaders
    ];
  };
  nativeBuildInputs = with xbox.pkgs; [
    cmake
    ninja
    directx-shader-compiler
  ];
  buildInputs = with xbox.pkgsXbox; [
    zlib
    luau
  ];
}
