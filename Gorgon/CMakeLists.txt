# CMakeList.txt : CMake project for Gorgon, include source and define
# project specific logic here.
#

add_executable (Gorgon "Gorgon.cpp"  "pch.h")
target_precompile_headers(Gorgon PRIVATE "pch.h")

target_include_directories(Gorgon PRIVATE "$ENV{VULKAN_SDK}/Include")
target_link_libraries(Gorgon PRIVATE fmt glfw Microsoft.GSL::GSL)

set_property(TARGET Gorgon PROPERTY CXX_STANDARD 23)

if(WIN32)
    set(EXE_EXT ".exe")
else()
    set(EXE_EXT "")
endif()

set(SLANG_EXECUTABLE "$ENV{VULKAN_SDK}/Bin/slangc${EXE_EXT}")
set(SHADER_INPUT "${CMAKE_CURRENT_SOURCE_DIR}/shaders/combined.slang")
set(SHADER_OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/shaders/combined")

add_custom_command(
	OUTPUT ${SHADER_OUTPUT}
	COMMAND ${SLANG_EXECUTABLE} ${SHADER_INPUT} -o ${SHADER_OUTPUT} -profile glsl_450 -target spirv
	DEPENDS ${SHADER_INPUT}
	COMMENT "Compiling shader with slang"
	VERBATIM
)

add_custom_target(CompileShaders ALL DEPENDS ${SHADER_OUTPUT})
add_dependencies(Gorgon CompileShaders)