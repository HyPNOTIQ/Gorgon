# CMakeList.txt : CMake project for Gorgon, include source and define
# project specific logic here.
#

add_executable (Gorgon "Gorgon.cpp"  "pch.h" "gltf_loader.h" "gltf_loader.cpp" "cgltf_include.h")
target_precompile_headers(Gorgon PRIVATE "pch.h")

# TODO
target_include_directories(Gorgon PRIVATE "$ENV{VULKAN_SDK}/Include")
#target_include_directories(Gorgon PRIVATE "$<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/third_party/cgltf>") // TODO: check what is BUILD_INTERFACE
target_link_libraries(Gorgon PRIVATE fmt)
target_link_libraries(Gorgon PRIVATE Microsoft.GSL::GSL)
target_link_libraries(Gorgon PRIVATE CLI11 )
target_link_libraries(Gorgon PRIVATE glm)
target_link_libraries(Gorgon PRIVATE glfw)

option(VULKAN_HPP_NO_CONSTRUCTORS ON)
target_link_libraries(Gorgon PRIVATE VulkanHpp)

option(TINYGLTF_BUILD_LOADER_EXAMPLE OFF)
option(TINYGLTF_INSTALL OFF)
option(TINYGLTF_INSTALL_VENDOR OFF)
target_link_libraries(Gorgon PRIVATE tinygltf)

set_property(TARGET Gorgon PROPERTY CXX_STANDARD 23)

if(WIN32)
	set(EXE_EXT ".exe")
else()
	set(EXE_EXT "")
endif()

# TODO: add support for multimple shader files
set(SLANG_EXECUTABLE "$ENV{VULKAN_SDK}/Bin/slangc${EXE_EXT}")
set(SHADER_INPUT "${CMAKE_CURRENT_SOURCE_DIR}/shaders/combined.slang")
set(SHADER_OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/shaders/combined")

add_custom_command(
	OUTPUT ${SHADER_OUTPUT}
	COMMAND ${SLANG_EXECUTABLE} ${SHADER_INPUT} -o ${SHADER_OUTPUT} -target spirv
	DEPENDS ${SHADER_INPUT}
	COMMENT "Compiling shader with slang"
	VERBATIM
)

add_custom_target(CompileShaders ALL DEPENDS ${SHADER_OUTPUT})
add_dependencies(Gorgon CompileShaders)