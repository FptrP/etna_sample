cmake_minimum_required(VERSION 3.22)
project(etna-sample)

set(CMAKE_BUILD_TYPE Debug)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

find_package(SDL2 CONFIG REQUIRED COMPONENTS SDL2)
add_subdirectory(etna)

set(TINYGLTF_HEADER_ONLY ON CACHE INTERNAL "" FORCE)
set(TINYGLTF_INSTALL OFF CACHE INTERNAL "" FORCE)

add_subdirectory(lib/tinygltf)

add_subdirectory(lib/imgui-1.89.9)

add_executable(etna-sample 
  src/main.cpp
  src/init.cpp
  src/events/events.cpp
  src/scene/GLTFScene.cpp
  src/scene/SceneRenderer.cpp
  src/scene/ABufferRenderer.cpp
  src/renderer/TAA.cpp)

target_include_directories(etna-sample PRIVATE src)
target_link_libraries(etna-sample etna tinygltf imgui SDL2::SDL2) 