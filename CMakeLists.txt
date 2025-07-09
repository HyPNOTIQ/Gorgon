# CMakeList.txt : Top-level CMake project file, do global configuration
# and include sub-projects here.
#
cmake_minimum_required (VERSION 3.16)

# Enable Hot Reload for MSVC compilers if supported.
if (POLICY CMP0141)
  cmake_policy(SET CMP0141 NEW)
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
endif()

project ("Gorgon")

# Include sub-projects.
add_subdirectory ("Gorgon")

set(VULKAN_HPP_HANDLES_MOVE_EXCHANGE ON)
set(VULKAN_HPP_BUILD_WITH_LOCAL_VULKAN_HPP OFF)
set(VK_NO_PROTOTYPES ON)
set(VULKAN_HPP_NO_CONSTRUCTORS ON)
add_subdirectory ("third_party/Vulkan-Hpp")
add_subdirectory ("third_party/glfw")
add_subdirectory ("third_party/fmt")
add_subdirectory ("third_party/GSL")
add_subdirectory ("third_party/VulkanMemoryAllocator")

set(CLI11_PRECOMPILED ON)
add_subdirectory ("third_party/CLI11")
add_subdirectory ("third_party/glm")

set(TINYGLTF_BUILD_LOADER_EXAMPLE OFF)
set(TINYGLTF_INSTALL OFF)
add_subdirectory ("third_party/tinygltf")

add_subdirectory ("third_party/dylib")