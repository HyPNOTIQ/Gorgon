# CMakeList.txt : CMake project for Gorgon, include source and define
# project specific logic here.
#

# Add source to this project's executable.
add_executable (Gorgon "Gorgon.cpp" )

target_include_directories(Gorgon PRIVATE "$ENV{VULKAN_SDK}/Include")
target_link_libraries(Gorgon PRIVATE fmt glfw Microsoft.GSL::GSL)

if (CMAKE_VERSION VERSION_GREATER 3.12)
  set_property(TARGET Gorgon PROPERTY CXX_STANDARD 23)
endif()

# TODO: Add tests and install targets if needed.
