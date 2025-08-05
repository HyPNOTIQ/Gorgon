option(SHADER_OUT_DIR "Shaders output directory")
option(ENABLE_GLSL_OUTPUT "If enabled, the shader compiler will generate GLSL output." OFF)
option(ENABLE_SHADER_REFLECTION "If enabled, the shader compiler will generate reflection data." OFF)
# add option for optimization level

set(SLANGC_EXECUTABLE "slangc")

set(SHADER_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
#set(SHADER_OUT_DIR "${CMAKE_CURRENT_BINARY_DIR}")

file(GLOB SHADER_FILES "${SHADER_SRC_DIR}/*.slang")

set(COMPILED_SHADERS)

foreach(SHADER ${SHADER_FILES})
	get_filename_component(SHADER_NAME ${SHADER} NAME_WE)
	set(OUTPUT_SPV "${SHADER_OUT_DIR}/${SHADER_NAME}.spv")
	set(DEP_FILE "${SHADER_OUT_DIR}/${SHADER_NAME}.d")
	set(DEFINES -D SLANG_SOURCE_FILE)
	set(SLANG_STD -std 2026)
	# set(PROFILE -profile glsl_460+spirv_latest)

	if(ENABLE_SHADER_REFLECTION)
		set(JSON_REFLECTION -reflection-json "${SHADER_OUT_DIR}/${SHADER_NAME}.json")
	endif()

	set(SHARED_CMD
		${SLANGC_EXECUTABLE}
		${SHADER}
		${DEFINES}
		${PROFILE}
		${SLANG_STD}
		-depfile ${DEP_FILE}
		-O0 #TODO
	)
	
	add_custom_command(
		OUTPUT ${OUTPUT_SPV}
		COMMAND ${SHARED_CMD}
				-o ${OUTPUT_SPV}
				${JSON_REFLECTION}
		DEPENDS ${SHADER}
		DEPFILE ${DEP_FILE}
		COMMENT "${SHADER_NAME}.slang -> SPIR-V"
		VERBATIM
	)

	list(APPEND COMPILED_SHADERS ${OUTPUT_SPV})
	
	if(ENABLE_GLSL_OUTPUT)
		set(OUTPUT_GLSL "${SHADER_OUT_DIR}/${SHADER_NAME}.glsl")

		add_custom_command(
			OUTPUT ${OUTPUT_GLSL}
			COMMAND ${SHARED_CMD}
					-target glsl
					> ${OUTPUT_GLSL}
			DEPENDS ${SHADER}
			DEPFILE ${DEP_FILE}
			COMMENT "${SHADER_NAME}.slang -> GLSL"
			VERBATIM
		)

		list(APPEND COMPILED_SHADERS ${OUTPUT_GLSL})
	endif()

endforeach()

add_custom_target(Shaders ALL DEPENDS ${COMPILED_SHADERS})