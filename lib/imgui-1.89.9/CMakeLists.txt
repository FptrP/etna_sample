cmake_minimum_required(VERSION 3.22)
project(imgui-vk-sdl2)

add_library(imgui 
  imgui_draw.cpp
  imgui_tables.cpp 
  imgui_widgets.cpp
  imgui.cpp
  backends/imgui_impl_sdl2.cpp
  backends/imgui_impl_vulkan.cpp
)

find_package(SDL2 CONFIG REQUIRED COMPONENTS SDL2)

target_include_directories(imgui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(imgui PRIVATE VK_NO_PROTOTYPES)
target_compile_definitions(imgui PUBLIC IMGUI_IMPL_VULKAN_NO_PROTOTYPES)

target_link_libraries(imgui PRIVATE SDL2::SDL2)