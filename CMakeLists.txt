cmake_minimum_required (VERSION 3.8)

project ("RayderX")

set(CMAKE_CXX_STANDARD 20)

set(VULKAN_SDK_DIR $ENV{VULKAN_SDK})
set(SDL2_DIR ${VULKAN_SDK_DIR}/cmake)

find_package(SDL2 REQUIRED)
find_package(Vulkan REQUIRED volk)

file(GLOB_RECURSE CPP_SOURCE_FILES "src/*.h" "src/*.cpp")

add_executable(rayderx
  ${CPP_SOURCE_FILES})

target_link_libraries(rayderx
  PRIVATE
    SDL2::SDL2 
    Vulkan::volk)