if(NOT DEFINED TIE_BUNDLE_CONTENT_DIR OR NOT DEFINED TIE_SHADER_SOURCE_DIR)
    message(FATAL_ERROR "TIE bundle staging paths are incomplete")
endif()

set(resource_dir "${TIE_BUNDLE_CONTENT_DIR}/Resources")
file(MAKE_DIRECTORY "${resource_dir}/shaders")
file(GLOB shaders "${TIE_SHADER_SOURCE_DIR}/*.msl")
file(COPY ${shaders} DESTINATION "${resource_dir}/shaders")
