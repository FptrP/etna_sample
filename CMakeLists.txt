cmake_minimum_required(VERSION 3.22)
project(etna-sample)

set(CMAKE_BUILD_TYPE Debug)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

find_package(glfw3 3.3 REQUIRED)

add_subdirectory(etna)

add_executable(etna-sample src/main.cpp)

target_link_libraries(etna-sample etna glfw)