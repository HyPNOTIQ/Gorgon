# CMakeList.txt : CMake project for Gorgon, include source and define
# project specific logic here.
#

add_executable (Gorgon "Gorgon.cpp"  "pch.h")
target_precompile_headers(Gorgon PRIVATE "pch.h")

target_include_directories(Gorgon PRIVATE "$ENV{VULKAN_SDK}/Include")
target_link_libraries(Gorgon PRIVATE fmt glfw Microsoft.GSL::GSL)

set_property(TARGET Gorgon PROPERTY CXX_STANDARD 23)

# TODO: Add tests and install targets if needed.
