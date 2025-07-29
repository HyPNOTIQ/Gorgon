add_executable (Gorgon "Gorgon.cpp" "pch.h" "gltf/model.cpp" "gltf/model.h" "gltf/loader.h" "gltf/loader_tinygltf.cpp" "gltf/loader.cpp" "vk/vma.cpp" "vk/vma.h" "gltf/tinygltf_impl.cpp" "vk/vma_impl.cpp")

set_property(TARGET Gorgon PROPERTY CXX_STANDARD 23)

target_precompile_headers(Gorgon PRIVATE "pch.h")

target_compile_definitions(Gorgon PRIVATE
	# VMA
	VMA_STATIC_VULKAN_FUNCTIONS=0
	VMA_DYNAMIC_VULKAN_FUNCTIONS=0
	# GLM
	GLM_FORCE_RADIANS
	GLM_FORCE_DEPTH_ZERO_TO_ONE
	# VULKAN-HPP
	VULKAN_HPP_HANDLES_MOVE_EXCHANGE
	VK_NO_PROTOTYPES
	VULKAN_HPP_NO_CONSTRUCTORS
)

find_package(fmt CONFIG REQUIRED)
find_package(CLI11 CONFIG REQUIRED)
find_package(glfw3 CONFIG REQUIRED)
find_package(boost_pfr CONFIG REQUIRED)
find_package(Boost REQUIRED COMPONENTS scope)
find_package(Boost REQUIRED COMPONENTS container_hash)

find_path(TINYGLTF_INCLUDE_DIRS "tiny_gltf.h")
target_include_directories(Gorgon PRIVATE ${TINYGLTF_INCLUDE_DIRS})

target_include_directories(Gorgon PRIVATE
	"$ENV{VULKAN_SDK}/Include/"
	${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(Gorgon PRIVATE
	fmt::fmt
	CLI11::CLI11
	glfw
	Boost::pfr
	Boost::scope
	Boost::container_hash
)

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

set(SHADER_OUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/shaders")

if(CMAKE_CONFIGURATION_TYPES)
    # Multi-config generator
    set(ENABLE_SHADER_REFLECTION $<CONFIG:Debug>)
else()
    # Single-config generator
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        set(ENABLE_SHADER_REFLECTION ON)
    else()
        set(ENABLE_SHADER_REFLECTION OFF)
    endif()
endif()

set(ENABLE_SHADER_REFLECTION ON)
add_subdirectory("shaders")

add_dependencies(Gorgon Shaders)