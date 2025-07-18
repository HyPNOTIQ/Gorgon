﻿# CMakeList.txt : CMake project for Gorgon, include source and define
# project specific logic here.
#

add_executable (Gorgon "Gorgon.cpp" "pch.h" "gltf/model.cpp" "gltf/model.h" "gltf/loader.h" "gltf/loader_tinygltf.cpp" "gltf/loader.cpp" "vk/vma.cpp" "vk/vma.h")

target_precompile_headers(Gorgon PRIVATE "pch.h")

target_compile_definitions(Gorgon PRIVATE
	VMA_STATIC_VULKAN_FUNCTIONS=0
	VMA_DYNAMIC_VULKAN_FUNCTIONS=0
	GLM_FORCE_RADIANS
	GLM_FORCE_DEPTH_ZERO_TO_ONE 
)


target_include_directories(Gorgon PRIVATE
	$ENV{VULKAN_SDK}/Include # TODO why it's needed if we have vulkan-hpp?
	${CMAKE_CURRENT_SOURCE_DIR}
)
#target_include_directories(Gorgon PRIVATE "$<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/third_party/cgltf>") // TODO: check what is BUILD_INTERFACE

if (DEFINED ENV{RENDERDOC_INCLUDE}) 
	message(STATUS "Building Gorgon with RenderDoc integration. "
		"RENDERDOC_INCLUDE environment variable is defined: $ENV{RENDERDOC_INCLUDE}")

	target_include_directories(Gorgon PRIVATE $ENV{RENDERDOC_INCLUDE})
	target_compile_definitions(Gorgon PRIVATE RENDERDOC_INCLUDE)
else()
	message(STATUS "Building Gorgon without RenderDoc integration. "
		"To enable RenderDoc, please define the RENDERDOC_INCLUDE environment "
		"variable pointing to your RenderDoc include directory.")
endif()

target_link_libraries(Gorgon PRIVATE
	Vulkan::Hpp
	fmt
	Microsoft.GSL::GSL
	CLI11
	glm
	glfw
	tinygltf
	VulkanMemoryAllocator
)

set_property(TARGET Gorgon PROPERTY CXX_STANDARD 23)

set(SLANGC_EXECUTABLE "slangc")

set(SHADER_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/shaders")
set(SHADER_BIN_DIR "${CMAKE_CURRENT_BINARY_DIR}/shaders")

file(GLOB SHADER_FILES "${SHADER_SRC_DIR}/*.slang")

set(COMPILED_SHADERS)

foreach(SHADER ${SHADER_FILES})
	get_filename_component(SHADER_NAME ${SHADER} NAME_WE)
	set(OUTPUT_SPV "${SHADER_BIN_DIR}/${SHADER_NAME}.spv")
	set(DEP_FILE "${SHADER_BIN_DIR}/${SHADER_NAME}.d")

	add_custom_command(
		OUTPUT ${OUTPUT_SPV}
		COMMAND ${SLANGC_EXECUTABLE}
				${SHADER}
				-o ${OUTPUT_SPV}
				-depfile ${DEP_FILE}
				-D SLANG_SOURCE_FILE
		DEPENDS ${SHADER}
		DEPFILE ${DEP_FILE}
		COMMENT "Compiling ${SHADER_NAME}.slang with slangc"
		VERBATIM
	)

	list(APPEND COMPILED_SHADERS ${OUTPUT_SPV})
endforeach()

add_custom_target(Shaders ALL DEPENDS ${COMPILED_SHADERS})

add_dependencies(Gorgon Shaders)