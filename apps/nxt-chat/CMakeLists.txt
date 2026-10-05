cmake_minimum_required(VERSION 3.21)
project(nxt-chat LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(SDL3 CONFIG REQUIRED)
find_package(SDL3_ttf CONFIG REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(NXT REQUIRED IMPORTED_TARGET nxtrt-iocp nxtui-sdl)

# Reuse NXT's transport-independent sample view, not its fixture controller.
configure_file("${NXT_CHAT_VIEW}" view.hpp COPYONLY)
add_executable(nxt-chat WIN32 main.cpp)
target_include_directories(nxt-chat PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(nxt-chat PRIVATE SDL3::SDL3 SDL3_ttf::SDL3_ttf PkgConfig::NXT)
install(TARGETS nxt-chat RUNTIME DESTINATION bin)
