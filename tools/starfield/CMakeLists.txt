# starfield — procedural HDR star cubemap toolchain.
#
# Renders a Star-Wars-style starfield directly into the 6 cube faces as
# linear-HDR float and writes a BC6H_UFLOAT KTX2 cubemap for the HD
# flight skybox loader (aeron/scene/ktx2_reader.h). No
# equirectangular intermediate — the output is the final game asset.
#
#   starfield_core   — shared render + bake + .tune.yaml preset I/O.
#   starfield        — CLI baker.
#   starfield_tune   — ImGui artist tool: live tweak + WYSIWYG preview.
#
# Reuses Aeron's imgbake KTX2/BC6H backend, configuration/VFS APIs,
# shared Dear ImGui target, and vendored stb image writer.

# ---------- shared core ----------
add_library(starfield_core STATIC
    starfield_core.c
    starfield_elements.c
    starfield_preset.c)
target_include_directories(starfield_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(starfield_core PRIVATE
    ${CMAKE_SOURCE_DIR}/aeron/third_party/stb) # stb_image.h
target_link_libraries(starfield_core PUBLIC
    imgbake aeron tie_libm)
set_target_properties(starfield_core PROPERTIES C_STANDARD 11)

# ---------- CLI baker ----------
add_executable(starfield starfield.c)
target_include_directories(starfield PRIVATE
    ${CMAKE_SOURCE_DIR}/aeron/third_party/stb) # stb_image_write.h
target_link_libraries(starfield PRIVATE starfield_core)
set_target_properties(starfield PROPERTIES C_STANDARD 11)

# ---------- ImGui artist tool ----------
enable_language(CXX)
add_executable(starfield_tune starfield_tune.cpp)
target_compile_definitions(starfield_tune PRIVATE
    TIE_SHADER_RELATIVE_DIR="${TIE_SHADER_RELATIVE_DIR}")
target_link_libraries(starfield_tune PRIVATE
    starfield_core aeron_scene PkgConfig::SDL3)
if(NOT TIE_ENABLE_DEBUG_UI)
    # Keep the target buildable so it can report the missing Aeron UI host.
    target_link_libraries(starfield_tune PRIVATE aeron_imgui)
endif()
target_compile_features(starfield_tune PRIVATE cxx_std_17)
add_custom_command(TARGET starfield_tune POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${TIE_SHADER_BINARY_DIR}"
            "$<TARGET_FILE_DIR:starfield_tune>/shaders"
    COMMENT "Staging starfield_tune shaders"
    VERBATIM)
