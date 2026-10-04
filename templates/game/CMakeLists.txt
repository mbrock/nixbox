cmake_minimum_required(VERSION 3.24)
project(game LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
include(NixboxShaders)

add_executable(game WIN32 main.cpp)
# nixbox's CMake module: each entry point becomes a header with a byte array.
nixbox_shader(game Cube.hlsl cubeVertexShader vertexMain vs_6_0)
nixbox_shader(game Cube.hlsl cubePixelShader pixelMain ps_6_0)
target_link_libraries(game PRIVATE d3d12 dxgi)
install(TARGETS game)
# Packaging keeps the PDB out of the package, in symbols/ for crash dumps.
install(FILES $<TARGET_PDB_FILE:game> DESTINATION bin OPTIONAL)
