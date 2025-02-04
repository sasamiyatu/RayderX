cmake_minimum_required (VERSION 3.8 )

project ("RayderX" LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)

set(VULKAN_SDK_DIR $ENV{VULKAN_SDK})
set(SDL2_DIR ${VULKAN_SDK_DIR}/cmake)

find_package(SDL2 REQUIRED)
find_package(Vulkan REQUIRED volk dxc)

file(GLOB_RECURSE CPP_SOURCE_FILES "src/*.h" "src/*.cpp")

add_executable(rayderx
  ${CPP_SOURCE_FILES}
  
  external/SPIRV-Reflect/spirv_reflect.h
  external/SPIRV-Reflect/spirv_reflect.c
  
  external/imgui/imgui.h
  external/imgui/imgui.cpp
  external/imgui/imgui_tables.cpp
  external/imgui/imgui_widgets.cpp
  external/imgui/imgui_draw.cpp
  external/imgui/backends/imgui_impl_sdl2.h
  external/imgui/backends/imgui_impl_sdl2.cpp
  external/imgui/backends/imgui_impl_vulkan.h
  external/imgui/backends/imgui_impl_vulkan.cpp
)

target_include_directories(rayderx PRIVATE external/SPIRV-Reflect external/imgui)

target_link_libraries(rayderx
  PRIVATE
    SDL2::SDL2 
    Vulkan::volk
    Vulkan::dxc_lib
    )

target_include_directories(rayderx PRIVATE external/cgltf external/stb)

if (MSVC)
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX)
endif()

#add_compile_definitions(
#  IMGUI_IMPL_VULKAN_NO_PROTOTYPES
#  VK_NO_PROTOTYPES
#)

set_target_properties(
    rayderx
    PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)
