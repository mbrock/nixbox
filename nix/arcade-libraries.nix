# Libraries shared by the SDL3 arcade games. Keep these on Nixpkgs' pinned
# sources; the Xbox builds use the same upstream code with desktop features off.
final: prev:
let
  box2d = (
    prev.box2d.overrideAttrs (old: {
      buildInputs = [ ];
      propagatedBuildInputs = [ ];
      cmakeFlags = (old.cmakeFlags or [ ]) ++ [
        "-DBOX2D_UNIT_TESTS:BOOL=OFF"
        "-DBOX2D_SAMPLES:BOOL=OFF"
        "-DBOX2D_DOCS:BOOL=OFF"
        "-DBOX2D_INSTALL:BOOL=ON"
        "-DBOX2D_DISABLE_SIMD:BOOL=ON"
        "-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded"
      ];
      doCheck = false;
      doInstallCheck = false;
      meta = old.meta // {
        platforms = prev.lib.platforms.all;
      };
    })
  );
in
{
  inherit box2d;
  imgui =
    (prev.imgui.override {
      IMGUI_BUILD_GLFW_BINDING = false;
      IMGUI_BUILD_OPENGL3_BINDING = false;
      IMGUI_BUILD_SDL3_BINDING = true;
      IMGUI_BUILD_SDL3_RENDERER_BINDING = true;
      IMGUI_BUILD_SDLGPU3_BINDING = false;
      IMGUI_BUILD_VULKAN_BINDING = false;
    }).overrideAttrs
      (old: {
        cmakeFlags = (old.cmakeFlags or [ ]) ++ [
          "-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded"
          # The Xbox SDK's x86 intrinsic declarations are not implementations;
          # ImGui's scalar math is preferable to unresolved SSE helper symbols.
          "-DCMAKE_CXX_FLAGS=-DIMGUI_DISABLE_SSE"
        ];
      });
}
