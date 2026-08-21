# Film toolchain: filmdump (CLI), filmextract (CLI), filmview (Aeron+ImGui).
enable_language(CXX)

#
# Layout:
#   filmlib            — C library: FILM/DELT/ANIM/player.
#   filmdump,
#   filmextract,
#   filmview           — executables.
#
# The root CMakeLists.txt provides the shared tool dependencies before
# pulling this directory in.
#
# tools/CMakeLists.txt pulls this directory into the tools build.

# ---------- shared C library ----------
add_library(filmlib STATIC
    util.c
    film.c
    delt.c
    anim.c
    film_player.c
    stream_player.c
    play1_streams.c
    # Block-based FMV decoder shared with the engine's tie_core.
    # Freestanding (only stdint+string), so re-using the source here
    # avoids drift; the file lives under src/tie/ because it's also
    # part of the runtime cutscene pipeline (play1.c → drawstrm).
    ${CMAKE_SOURCE_DIR}/src/tie/drawstrm.c)

target_include_directories(filmlib
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
           ${CMAKE_SOURCE_DIR}/src/tie)      # drawstrm.h
set_target_properties(filmlib PROPERTIES C_STANDARD 11)
target_link_libraries(filmlib PUBLIC tie_tool_lfd tie_formats imgbake)

# ---------- CLI tools ----------
add_executable(filmdump filmdump.c)
target_link_libraries(filmdump PRIVATE filmlib)

add_executable(filmextract filmextract.c)
target_link_libraries(filmextract PRIVATE filmlib imgbake)

# ---------- filmview executable ----------
add_executable(filmview filmview.cpp)
target_link_libraries(filmview
        PRIVATE filmlib tie_remaster_scene2d imgbake aeron_imgui PkgConfig::SDL3)
target_include_directories(filmview PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_features(filmview PRIVATE cxx_std_17)
target_compile_definitions(filmview PRIVATE
        TIE_SHADER_RELATIVE_DIR="${TIE_SHADER_RELATIVE_DIR}")
add_dependencies(filmview flight_shaders)
add_custom_command(TARGET filmview POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${TIE_SHADER_BINARY_DIR}"
                "$<TARGET_FILE_DIR:filmview>/shaders"
        COMMENT "Staging filmview shaders"
        VERBATIM)
