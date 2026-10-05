cmake_minimum_required(VERSION 3.20)
project(sbcl_platform_probe LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
find_package(SDL3 CONFIG REQUIRED)
# Deliberate experimental imports: the SDK hides VEH from the App partition.
# Keep these out of the shared toolchain; installing/launching tests availability.
find_program(LLVM_DLLTOOL llvm-dlltool REQUIRED)
add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/probe-api.lib"
  COMMAND "${LLVM_DLLTOOL}" -m i386:x86-64 -d "${CMAKE_CURRENT_SOURCE_DIR}/probe.def"
    -l "${CMAKE_CURRENT_BINARY_DIR}/probe-api.lib"
  DEPENDS probe.def VERBATIM)
add_executable(sbcl-platform-probe WIN32 main.cpp probe.c "${CMAKE_CURRENT_BINARY_DIR}/probe-api.lib")
target_compile_definitions(sbcl-platform-probe PRIVATE PROBE_CODEGEN=${PROBE_CODEGEN} _CRT_SECURE_NO_WARNINGS)
target_compile_options(sbcl-platform-probe PRIVATE -Wall -Wextra)
target_link_libraries(sbcl-platform-probe PRIVATE SDL3::SDL3 "${CMAKE_CURRENT_BINARY_DIR}/probe-api.lib")
install(TARGETS sbcl-platform-probe RUNTIME DESTINATION bin)
