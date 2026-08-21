# Extract SHIP resources from SPECIES*.LFD using the runtime model converter
# and Aeron's cgltf writer.

add_executable(lfd2gltf
    main.c
    gltf_export.c
    ${CMAKE_SOURCE_DIR}/src/tie_runtime/flight_assets/ship_model_converter.c)

target_include_directories(lfd2gltf PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/aeron/include
    ${CMAKE_SOURCE_DIR}/aeron/third_party/cgltf)

target_compile_definitions(lfd2gltf PRIVATE TIE_LFD2GLTF_STANDALONE=1)

# Math + standard libc only.
target_link_libraries(lfd2gltf PRIVATE tie_tool_lfd aeron tie_formats tie_libm)

set_target_properties(lfd2gltf PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
