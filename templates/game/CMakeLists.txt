cmake_minimum_required(VERSION 3.24)
project(game LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(game WIN32 main.cpp)
target_link_libraries(game PRIVATE d3d12 dxgi)
install(TARGETS game)
