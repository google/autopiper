project(autopiper)
cmake_minimum_required(VERSION 2.6)
enable_language(CXX)
enable_testing()

set(autopiper_VERSION "0.1")

set(CMAKE_CXX_FLAGS "-Wall -Werror -std=c++11 -Wno-sign-compare")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g -O0")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3")

add_subdirectory(src/backend)
add_subdirectory(src/frontend)
