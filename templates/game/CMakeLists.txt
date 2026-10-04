cmake_minimum_required(VERSION 3.24)
project(game LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Each shader entry point becomes shaders/<name>.h, a byte array named <name>.
find_program(DXC dxc REQUIRED)
set(SHADER_DIR ${CMAKE_CURRENT_BINARY_DIR}/shaders)
function(add_shader name source entry profile)
  set(header ${SHADER_DIR}/${name}.h)
  add_custom_command(
    OUTPUT ${header}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${SHADER_DIR}
    COMMAND ${DXC} -O3 -T ${profile} -E ${entry} -Fh ${header} -Vn ${name}
            ${CMAKE_CURRENT_SOURCE_DIR}/${source}
    DEPENDS ${source}
    VERBATIM)
  list(APPEND SHADER_HEADERS ${header})
  set(SHADER_HEADERS ${SHADER_HEADERS} PARENT_SCOPE)
endfunction()
add_shader(cubeVertexShader Cube.hlsl vertexMain vs_6_0)
add_shader(cubePixelShader Cube.hlsl pixelMain ps_6_0)

add_executable(game WIN32 main.cpp ${SHADER_HEADERS})
target_include_directories(game PRIVATE ${SHADER_DIR})
target_link_libraries(game PRIVATE d3d12 dxgi)
install(TARGETS game)
# Packaging keeps the PDB out of the package, in symbols/ for crash dumps.
install(FILES $<TARGET_PDB_FILE:game> DESTINATION bin OPTIONAL)
