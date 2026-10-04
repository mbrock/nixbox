cmake_minimum_required(VERSION 3.24)
project(hello LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
include(NixboxShaders)

# pkgsXbox libraries in buildInputs are on CMAKE_PREFIX_PATH.
find_library(ZLIB_LIBRARY zs REQUIRED)
foreach(component VM Compiler Ast Bytecode Common)
  find_library(LUAU_${component} Luau.${component} REQUIRED)
  list(APPEND LUAU_LIBRARIES ${LUAU_${component}})
endforeach()

# module.cpp compiles the generated module.g.cpp; App.g.h and hello.winmd come
# from app.idl, through mkXboxApp's idl argument.
add_executable(hello WIN32 App.cpp MainPage.cpp D3DView.cpp module.cpp)
target_precompile_headers(hello PRIVATE pch.h)
target_include_directories(hello PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
nixbox_shader(hello Shaders/Cube.hlsl cubeVertexShader vertexMain vs_6_0)
nixbox_shader(hello Shaders/Cube.hlsl cubePixelShader pixelMain ps_6_0)
target_link_libraries(hello PRIVATE d3d12 dxgi ${ZLIB_LIBRARY} ${LUAU_LIBRARIES})
install(TARGETS hello)
# Packaging keeps the PDB out of the package, in symbols/ for crash dumps.
install(FILES $<TARGET_PDB_FILE:hello> DESTINATION bin OPTIONAL)
