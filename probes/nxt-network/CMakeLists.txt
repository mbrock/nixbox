cmake_minimum_required(VERSION 3.21)
project(nxt-network LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(SDL3 CONFIG REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(NXT REQUIRED IMPORTED_TARGET nxtrt-iocp)

# Run the upstream probe unchanged, inside an Xbox-launchable SDL host.
configure_file("${NXT_PROBE_SOURCE}" network-probe.cpp COPYONLY)
add_executable(nxt-network WIN32 main.cpp)
target_include_directories(nxt-network PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(nxt-network PRIVATE SDL3::SDL3 PkgConfig::NXT)
install(TARGETS nxt-network RUNTIME DESTINATION bin)
