cmake_minimum_required(VERSION 3.20)
project(sbcl_probe LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
find_package(SDL3 CONFIG REQUIRED)
add_executable(sbcl-probe WIN32 main.cpp)
target_compile_definitions(sbcl-probe PRIVATE _CRT_SECURE_NO_WARNINGS)
target_compile_options(sbcl-probe PRIVATE -Wall -Wextra)
# Object-file directives bypass LLD's fuzzy .def matching, which otherwise
# selects C++ math overloads that jump back to themselves during CRT init.
file(STRINGS "${SBCL_ROOT}/lib/sbcl/sbcl.def" sbcl_exports)
list(REMOVE_ITEM sbcl_exports "EXPORTS")
set(export_source "${CMAKE_CURRENT_BINARY_DIR}/sbcl-exports.c")
file(WRITE "${export_source}" "/* Exact C exports for embedded SBCL. */\n")
foreach(symbol IN LISTS sbcl_exports)
  file(APPEND "${export_source}" "#pragma comment(linker, \"/export:${symbol}\")\n")
endforeach()
target_sources(sbcl-probe PRIVATE "${export_source}")
target_link_libraries(sbcl-probe PRIVATE SDL3::SDL3
  "${SBCL_ROOT}/lib/sbcl/sbcl-runtime.lib"
  "${SBCL_ROOT}/lib/sbcl/nixbox-kernelbase.lib" ws2_32 advapi32 shell32)
install(TARGETS sbcl-probe RUNTIME DESTINATION bin)
install(FILES "${SBCL_ROOT}/lib/sbcl/sbcl.core" probe.lisp
  DESTINATION share/sbcl-probe)
install(FILES "${SBCL_ROOT}/share/sbcl/COPYING" "${SBCL_ROOT}/share/sbcl/CREDITS"
  "${SBCL_ROOT}/share/sbcl/TLSF-LICENSE"
  DESTINATION share/sbcl-probe/sbcl-notices)
