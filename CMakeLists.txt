cmake_minimum_required(VERSION 3.22)
project(etna-sample)

set(CMAKE_BUILD_TYPE Debug)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

find_package(glfw3 3.3 REQUIRED)

add_subdirectory(etna)


set(TINYGLTF_HEADER_ONLY ON CACHE INTERNAL "" FORCE)
set(TINYGLTF_INSTALL OFF CACHE INTERNAL "" FORCE)
add_subdirectory(lib/tinygltf)

add_executable(etna-sample 
  src/main.cpp
  src/init.cpp
  src/scene/GLTFScene.cpp
  src/scene/SceneRenderer.cpp)

target_link_libraries(etna-sample etna glfw tinygltf) 