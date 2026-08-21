# Shared filesystem owner around the memory-only LFD parser in tie_formats.
add_library(tie_tool_lfd STATIC common/lfd_file.c)
target_include_directories(tie_tool_lfd PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/common)
target_link_libraries(tie_tool_lfd PUBLIC tie_formats)
set_target_properties(tie_tool_lfd PROPERTIES C_STANDARD 11)
tie_enable_warnings(tie_tool_lfd)

add_subdirectory(film)
add_subdirectory(cockpit)
add_subdirectory(font)
add_subdirectory(lfd2gltf)
add_subdirectory(species_billboard)
add_subdirectory(starfield)

# opt2gltf and aeron_gltf_cook are provided by the Aeron submodule.
add_executable(opt_dump opt/opt_dump.c)
target_link_libraries(opt_dump PRIVATE lucas_opt tie_libm)
set_target_properties(opt_dump PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable(opt_textures opt/opt_textures.c)
target_include_directories(opt_textures PRIVATE ${CMAKE_SOURCE_DIR}/aeron/third_party/stb)
target_link_libraries(opt_textures PRIVATE lucas_opt tie_libm)
set_target_properties(opt_textures PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

# Convert PNG alpha coverage into a compact rectangle mesh.
add_executable(coverage_mesh coverage_mesh.c)
target_include_directories(coverage_mesh PRIVATE ${CMAKE_SOURCE_DIR}/aeron/third_party/stb)
target_link_libraries(coverage_mesh PRIVATE tie_libm)
set_target_properties(coverage_mesh PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
tie_enable_warnings(coverage_mesh)

# Compare the classic model contract with the TIE98 OPT selected for each species.
add_executable(tie_model_compat tie_model_compat.c)
target_include_directories(tie_model_compat PRIVATE
    ${CMAKE_SOURCE_DIR}/src/tie_runtime/flight_assets
    ${CMAKE_SOURCE_DIR}/aeron/include)
target_compile_definitions(tie_model_compat PRIVATE
    TIE_MODEL_COMPAT_DEFAULT_CATALOG_ROOT="${CMAKE_SOURCE_DIR}/resources")
target_link_libraries(tie_model_compat PRIVATE
    tie_tool_lfd
    tie_core
    aeron
    lucas_opt
    tie_formats
    tie_libm)
set_target_properties(tie_model_compat PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
tie_enable_warnings(tie_model_compat)

add_executable(mission_dump mission_dump.c)
target_link_libraries(mission_dump PRIVATE tie_core)
set_target_properties(mission_dump PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
tie_enable_warnings(mission_dump)

# Standalone PNG to BC7/RGBA KTX2 converter.
add_executable(bc7enc bc7enc.c)
target_include_directories(bc7enc PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/aeron/third_party/stb)
target_link_libraries(bc7enc PRIVATE imgbake)

# Equirectangular PNG/HDR to mipmapped KTX2 cubemap converter.
add_executable(skybox_pack skybox_pack.c)
target_include_directories(skybox_pack PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/aeron/third_party/stb)
target_link_libraries(skybox_pack PRIVATE imgbake tie_libm)
