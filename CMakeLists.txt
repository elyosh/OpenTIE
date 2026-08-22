cmake_minimum_required(VERSION 3.23)
if(APPLE AND NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "13.0" CACHE STRING "Minimum supported macOS version")
endif()
project(tie C)

# Enable C++ for transitive CXX dependencies; the engine remains C.
enable_language(CXX)
set(CMAKE_CXX_STANDARD 17)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

function(tie_enable_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4)
    else()
        target_compile_options(${target} PRIVATE
            -Wall -Wextra -Wpedantic -Werror=implicit-function-declaration)
    endif()
endfunction()

option(ENABLE_ASAN "Build with AddressSanitizer + UBSan" OFF)
if (ENABLE_ASAN AND NOT MSVC)
    message(STATUS "AddressSanitizer + UBSan enabled")
    add_compile_options(-fsanitize=address,undefined -fno-sanitize=alignment -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=address,undefined)
endif()

set(TIE_VERSION "0.0.0-dev" CACHE STRING "OpenTIE application version")
option(TIE_BUILD_TOOLS "Build TIE and Aeron asset tools" ON)

add_library(tie_libm INTERFACE)
if(NOT MSVC)
    target_link_libraries(tie_libm INTERFACE m)
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL3 REQUIRED IMPORTED_TARGET sdl3)
pkg_check_modules(ZSTD REQUIRED IMPORTED_TARGET libzstd)

# Top-level aliases for libimuse backend options.
option(ENABLE_FLUIDSYNTH "Enable FluidSynth MIDI backend" ON)
set(IMUSE_ENABLE_FLUIDSYNTH ${ENABLE_FLUIDSYNTH})

option(ENABLE_FM4_OPL3 "Enable FM4 Nuked-OPL3 MIDI backend" ON)
set(IMUSE_ENABLE_FM4_OPL3 ${ENABLE_FM4_OPL3})

option(ENABLE_NUKED_SC55 "Enable Nuked SC-55 MIDI backend" ON)
set(IMUSE_ENABLE_NUKED_SC55 ${ENABLE_NUKED_SC55})

# Enable GPU debug labels by default outside optimized builds.
if (CMAKE_BUILD_TYPE STREQUAL "Release" OR
    CMAKE_BUILD_TYPE STREQUAL "MinSizeRel")
    set(_TIE_GPU_DEBUG_LABELS_DEFAULT OFF)
else()
    set(_TIE_GPU_DEBUG_LABELS_DEFAULT ON)
endif()
option(TIE_GPU_DEBUG_LABELS
    "Emit SDL_GPU debug-group / resource-name calls for frame capture"
    ${_TIE_GPU_DEBUG_LABELS_DEFAULT})
unset(_TIE_GPU_DEBUG_LABELS_DEFAULT)

add_subdirectory(src/landru)
tie_enable_warnings(landru)
if(NOT MSVC)
    target_compile_options(landru PRIVATE -fno-strict-aliasing)
endif()

add_subdirectory(src/imuse)
tie_enable_warnings(imuse)
if(NOT MSVC)
    target_compile_options(imuse PRIVATE -fno-strict-aliasing)
endif()

# Recovered TIE and its authored runtime form one internal game target.
set(TIE_RECOVERED_SOURCES
        src/tie/asl.c
        src/tie/asl.h
		src/tie/frontend_display_tie98.c
		src/tie/frontend_display_tie98.h
		src/tie/flight_surface_tie98.c
		src/tie/flight_surface_tie98.h
		src/tie/flight_composite_tie98.c
		src/tie/flight_composite_tie98.h
		src/tie/tie_render_tie98.c
		src/tie/tie_render_tie98.h
		src/tie/math3d_tie98.c
		src/tie/math3d_tie98.h
		src/tie/model_texture_tie98.c
		src/tie/model_texture_tie98.h
		src/tie/std3d_tie98.c
		src/tie/std3d_tie98.h
		src/tie/render_clip_tie98.c
		src/tie/render_clip_tie98.h
		src/tie/render_list_tie98.c
		src/tie/render_list_tie98.h
		src/tie/render_scene_tie98.h
		src/tie/render_scene_tie98.c
		src/tie/render_texture_tie98.c
		src/tie/render_texture_tie98.h
        src/tie/fcallbk.c
        src/tie/shell.c
        src/tie/shell.h
        src/tie/shellext.c
        src/tie/shellext.h
        src/tie/soundext.c
        src/tie/soundext.h
        src/tie/textext.c
        src/tie/textext.h
        src/tie/stub.c
        src/tie/stub.h
        src/tie/rotpoly.c
        src/tie/rotpoly.h
        src/tie/shipext.c
        src/tie/shipext.h
        src/tie/mfscript.c
        src/tie/mfscript.h
        src/tie/gamesnd.c
        src/tie/gamesnd.h
		src/tie/frontend_sound_tie98.c
		src/tie/frontend_sound_tie98.h
		src/tie_runtime/integration/winmm_compat.h
		src/tie/cdaudio_tie98.c
		src/tie/cdaudio_tie98.h
		src/tie/filestream_tie98.c
		src/tie/filestream_tie98.h
		src/tie/dsound_wave_tie98.c
		src/tie/dsound_wave_tie98.h
		src/tie/wavestream_tie98.c
		src/tie/wavestream_tie98.h
        src/tie/fediskio.c
        src/tie/fediskio.h
		src/tie/modelmesh.c
		src/tie/modelmesh.h
		src/tie/modelbounds.c
		src/tie/collide_opt.c
		src/tie/collide_opt.h
		src/tie/modelbounds.h
        src/tie/festring.c
        src/tie/festring.h
        src/tie/tie.h
        src/tie/tie.c
        src/tie/mission.h
        src/tie/fmusic.c
        src/tie/fmusic.h
        src/tie/fscript.c
        src/tie/fscript.h
        src/tie/panel.c
        src/tie/overlay.c
        src/tie/damage.c
        src/tie/feinput.c
        src/tie/feinput.h
        src/tie/gate.c
        src/tie/goals.c
        src/tie/help.c
        src/tie/maproom.c
        src/tie/msg.c
        src/tie/msg.h
        src/tie/msgroom.c
        src/tie/msgroom.h
        src/tie/option.c
        src/tie/panelrts.c
        src/tie/user.c
        src/tie/filelen.c
        src/tie/filelen.h
        src/tie/wingman.c
        src/tie/wingman.h
        src/tie/shade.c
        src/tie/shade.h
        src/tie/sys2.c
        src/tie/sys2.h
        src/tie/mouse2.c
        src/tie/mouse2.h
        src/tie/player.c
        src/tie/player.h
        src/tie/tielogo.c
        src/tie/tielogo.h
        src/tie/mainmenu.c
        src/tie/mainmenu.h
        src/tie/credits.c
        src/tie/credits.h
        src/tie/filmview.c
        src/tie/filmview.h
        src/tie/debrief.c
        src/tie/debrief.h
        src/tie/register.c
        src/tie/register.h
        src/tie/brief.c
        src/tie/brief.h
        src/tie/title.c
        src/tie/title.h
        src/tie/slant.c
        src/tie/slant.h
        src/tie/tourdesk.c
        src/tie/tourdesk.h
        src/tie/computer.c
        src/tie/computer.h
        src/tie/talk.c
        src/tie/talk.h
        src/tie/map.c
        src/tie/map.h
        src/tie/armship.c
        src/tie/armship.h
        src/tie/play1.c
        src/tie/play1.h
        src/tie/deltadd.c
        src/tie/deltadd.h
        src/tie/drawstrm.c
        src/tie/drawstrm.h
        src/tie/blueprnt.c
        src/tie/blueprnt.h
        src/tie/train.c
        src/tie/train.h
        src/tie/combat.c
        src/tie/combat.h
        src/tie/matrix.c
        src/tie/matrix.h
        src/tie/trig2.c
        src/tie/trig2.h
        src/tie/math2.c
        src/tie/math2.h
        src/tie/rand.c
        src/tie/rand.h
        src/tie/logbuf2.c
        src/tie/logbuf2.h
        src/tie/xtrans2.c
        src/tie/xtrans2.h
        src/tie/drawpol.c
        src/tie/drawpol.h
        src/tie/drawln2.c
        src/tie/drawln2.h
        src/tie/collide.c
        src/tie/collide.h
        src/tie/draw.c
        src/tie/draw.h
        src/tie/anim.c
        src/tie/anim.h
        src/tie/bpflight.c
        src/tie/bpflight.h
        src/tie/create.c
        src/tie/create.h
        src/tie/dynamix.c
        src/tie/dynamix.h
        src/tie/score.c
        src/tie/score.h
        src/tie/spec.c
        src/tie/spec.h
        src/tie/species.c
        src/tie/species.h
        src/tie/move.c
        src/tie/move.h
        src/tie/pai.c
        src/tie/pai.h
        src/tie/paifight.c
        src/tie/paifight.h
        src/tie/paiman.c
        src/tie/paiman.h
        src/tie/paiorder.c
        src/tie/paiorder.h
        src/tie/starship.c
        src/tie/starship.h
        src/tie/static.c
        src/tie/static.h
        src/tie/laser.c
        src/tie/laser.h
        src/tie/fsfx.c
        src/tie/fsfx.h
        src/tie/gate.h
        src/tie/rotscale.c
        src/tie/rotscale.h
        src/tie/trace2.c
        src/tie/trace2.h
        src/tie/transfm2.c
        src/tie/transfm2.h
        src/tie/fview.c
        src/tie/fview.h
        src/tie/backdrp2.c
        src/tie/backdrp2.h
        src/tie/rtsvga2.c
        src/tie/rtsvga2.h
        src/tie/replay.c
        src/tie/replay.h
        src/tie/replayio.c
        src/tie/replayio.h
        src/tie/xtimer.c
        src/tie/xtimer.h)

set(TIE_RUNTIME_SOURCES
    src/tie_runtime/snapshot/snapshot.h
    src/tie_runtime/snapshot/snapshot.c
    src/tie_runtime/snapshot/capture_views.h
    src/tie_runtime/snapshot/snapshot_frontend.c
    src/tie_runtime/snapshot/snapshot_frontend.h
    src/tie_runtime/snapshot/snapshot_flight.c
    src/tie_runtime/snapshot/snapshot_flight.h
    src/tie_runtime/snapshot/snapshot_hud.c
    src/tie_runtime/snapshot/snapshot_hud.h
    src/tie_runtime/snapshot/snapshot_map.c
    src/tie_runtime/snapshot/snapshot_map.h
    src/tie_runtime/snapshot/snapshot_billboards.c
    src/tie_runtime/snapshot/snapshot_billboards.h
    src/tie_runtime/runtime/profile_types.h
    src/tie_runtime/runtime/exports.h
    src/tie_runtime/flight_assets/model_types.h
    src/tie_runtime/audio/config.h
    src/tie_runtime/runtime/exports.c
    src/tie_runtime/diagnostics/diagnostics.c

    src/tie_runtime/diagnostics/diagnostics.h
    src/tie_runtime/integration/landru_adapter.c
    src/tie_runtime/integration/landru_adapter.h
    src/tie_runtime/flight_assets/native_opt.c
    src/tie_runtime/flight_assets/native_opt.h
    src/tie_runtime/display/tie98_renderer.c
    src/tie_runtime/display/tie98_renderer.h
    src/tie_runtime/runtime/profile.c
    src/tie_runtime/runtime/profile.h
    src/tie_runtime/runtime/replay_format.c
    src/tie_runtime/runtime/replay_format.h
    src/tie_runtime/runtime/flight_screen.c
    src/tie_runtime/runtime/flight_screen.h
    src/tie_runtime/runtime/inflight_state.h
    src/tie_runtime/display/classic_display.c
    src/tie_runtime/display/classic_display.h
    src/tie_runtime/display/classic_framebuffer.c
    src/tie_runtime/display/classic_framebuffer.h
    src/tie_runtime/runtime/runtime.c
    src/tie_runtime/runtime/runtime.h
    src/tie_runtime/presentation/presentation.c
    src/tie_runtime/presentation/presentation.h
    src/tie_runtime/presentation/classic_layer.c
    src/tie_runtime/presentation/classic_layer.h
    src/tie_runtime/presentation/pilot_name.c
    src/tie_runtime/presentation/pilot_name.h
    src/tie_runtime/input/actions.c
    src/tie_runtime/input/actions.h
    src/tie_runtime/input/input.c
    src/tie_runtime/input/input.h
    src/tie_runtime/input/controller_mapping.c
    src/tie_runtime/input/controller_mapping.h
    src/tie_runtime/input/transform.c
    src/tie_runtime/input/transform.h
    src/tie_runtime/storage/storage.c
    src/tie_runtime/storage/storage.h
    src/tie_runtime/storage/pilot_storage.h
    src/tie_runtime/storage/score_tables.c
    src/tie_runtime/storage/score_tables.h
    src/tie_runtime/flight_assets/assets.c
    src/tie_runtime/flight_assets/assets.h
    src/tie_runtime/flight_assets/source.h
    src/tie_runtime/flight_assets/config.h
    src/tie_runtime/flight_assets/store.c
    src/tie_runtime/flight_assets/store.h
    src/tie_runtime/flight_assets/model_cache.c
    src/tie_runtime/flight_assets/model_cache.h
    src/tie_runtime/flight_assets/native_opt_cache.c
    src/tie_runtime/flight_assets/native_opt_cache.h
    src/tie_runtime/flight_assets/original_archive.c
    src/tie_runtime/flight_assets/original_archive.h
    src/tie_runtime/flight_assets/ship_model_converter.c
    src/tie_runtime/flight_assets/ship_model_converter.h
    src/tie_runtime/flight_assets/service.c
    src/tie_runtime/flight_assets/service.h
    src/tie_runtime/timing/sim_clock.c
    src/tie_runtime/timing/sim_clock.h
    src/tie_runtime/timing/flight_timing.c
    src/tie_runtime/timing/flight_timing.h
    src/tie_runtime/timing/flight_timing_state.c
    src/tie_runtime/timing/flight_timing_state.h
    src/tie_runtime/timing/user_timing.c
    src/tie_runtime/timing/user_timing.h
    src/tie_runtime/timing/flight_checkpoint.c
    src/tie_runtime/timing/flight_checkpoint.h
    src/tie_runtime/timing/ai_lead.c
    src/tie_runtime/timing/ai_lead.h
    src/tie_runtime/timing/chase_camera.c
    src/tie_runtime/timing/chase_camera.h
    src/tie_runtime/timing/replay_timing.c
    src/tie_runtime/timing/replay_timing.h
    src/tie_runtime/timing/replay_recording.c
    src/tie_runtime/timing/replay_recording.h
    src/tie_runtime/audio/midi_backend.c
    src/tie_runtime/audio/midi_backend.h
    src/tie_runtime/audio/music_policy.c
    src/tie_runtime/audio/music_policy.h
    src/tie_runtime/audio/flight_audio.c
    src/tie_runtime/audio/flight_audio.h
    src/tie_runtime/audio/player_engine.c
    src/tie_runtime/audio/player_engine.h
    src/tie_runtime/audio/voc_compat.c
    src/tie_runtime/audio/voc_compat.h
    src/tie_runtime/audio/imuse_session.h
    src/tie_runtime/audio/config.c
    src/tie_runtime/audio/output.c
    src/tie_runtime/audio/output.h
    src/tie_runtime/hooks/orientation.c
    src/tie_runtime/hooks/orientation.h)

# Shared parsers and generated asset tables used by runtime and tools.
add_library(tie_formats STATIC
	 src/tie_formats/common.c
	 src/tie_formats/lfd.c
	 src/tie_formats/panel.c
	 src/tie_formats/shape.c
	 src/tie_formats/cockpit.c
	 src/tie_formats/xact.c
	 src/tie_formats/cockpit_font.c
	 src/tie_formats/cockpit_masks.c
	 src/tie_formats/cockpit_masks.h)
target_include_directories(tie_formats PUBLIC ${CMAKE_SOURCE_DIR}/src)
set_target_properties(tie_formats PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON)
tie_enable_warnings(tie_formats)

add_library(tie_core STATIC ${TIE_RECOVERED_SOURCES} ${TIE_RUNTIME_SOURCES})
target_include_directories(tie_core PUBLIC ${CMAKE_SOURCE_DIR}/src)
if(NOT MSVC)
    # Recovered code retains aliasing patterns that are unsafe to optimize.
    set_source_files_properties(${TIE_RECOVERED_SOURCES}
        PROPERTIES COMPILE_OPTIONS "-fno-strict-aliasing")
endif()

target_link_libraries(tie_core PUBLIC landru::landru imuse PRIVATE tie_formats)
tie_enable_warnings(tie_core)

# Aeron loads all compiled shaders from one executable-relative directory.
set(TIE_SHADER_BINARY_DIR "${CMAKE_BINARY_DIR}/shaders")
file(MAKE_DIRECTORY "${TIE_SHADER_BINARY_DIR}")

# Aeron_Init resolves this path against the executable directory.
file(RELATIVE_PATH TIE_SHADER_RELATIVE_DIR
     "${CMAKE_BINARY_DIR}" "${TIE_SHADER_BINARY_DIR}")

option(TIE_ENABLE_DEBUG_UI "Build the Aeron ImGui debug overlay + TIE tools" OFF)
set(AERON_DEBUG_UI ${TIE_ENABLE_DEBUG_UI})

set(AERON_BUILD_TOOLS ${TIE_BUILD_TOOLS})
# Cutscenes use TIE's FILM/DELT pipeline rather than Aeron video.
set(AERON_ENABLE_VIDEO OFF)
set(AERON_ENABLE_AUDIO_DECODE ON)
set(AERON_ENABLE_WINMM_CD_AUDIO ON)
set(AERON_ENABLE_DX5_COMPAT ON)
add_subdirectory(aeron)
target_link_libraries(tie_core PRIVATE
    aeron_dx5
    aeron_winmm
    aeron_asset
    aeron_scene
    lucas_opt
    tie_libm)

# Compiles HLSL sources into every format in AERON_SHADER_FORMATS.
# Stages are inferred from filename suffixes; all HLSL includes are dependencies.
function(tie_add_shaders TARGET SOURCE_DIR)
    # shadercross accepts one include directory; Aeron includes use relative paths.
    file(GLOB SHADER_INCLUDES CONFIGURE_DEPENDS
        "${SOURCE_DIR}/*.hlsli"
        "${CMAKE_SOURCE_DIR}/aeron/shaders/*.hlsli")
    set(SHADER_OUTPUTS "")
    foreach(hlsl IN LISTS ARGN)
        get_filename_component(name "${hlsl}" NAME)
        if(name MATCHES "\\.vert\\.hlsl$")
            set(stage vertex)
        elseif(name MATCHES "\\.frag\\.hlsl$")
            set(stage fragment)
        elseif(name MATCHES "\\.comp\\.hlsl$")
            set(stage compute)
        else()
            message(FATAL_ERROR "Cannot infer shader stage from ${hlsl}")
        endif()
        string(REGEX REPLACE "\\.hlsl$" "" stem "${name}")
        set(in_path  "${SOURCE_DIR}/${hlsl}")
        set(outputs "")
        set(commands "")
        foreach(format IN LISTS AERON_SHADER_FORMATS)
            string(TOUPPER "${format}" format)
            if(format STREQUAL "MSL")
                set(extension msl)
            elseif(format STREQUAL "SPIRV")
                set(extension spv)
            elseif(format STREQUAL "DXIL")
                set(extension dxil)
            else()
                message(FATAL_ERROR
                    "Unsupported Aeron shader format '${format}'; expected MSL, SPIRV, or DXIL")
            endif()
            set(out_path "${TIE_SHADER_BINARY_DIR}/${stem}.${extension}")
            list(APPEND outputs "${out_path}")
            list(APPEND commands
                COMMAND "${AERON_SHADERCROSS_EXECUTABLE}" "${in_path}"
                    --source HLSL --dest "${format}" --stage "${stage}" --entrypoint main
                    -I "${SOURCE_DIR}"
                    --output "${out_path}")
        endforeach()
        add_custom_command(
            OUTPUT ${outputs}
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${TIE_SHADER_BINARY_DIR}"
            ${commands}
            DEPENDS "${in_path}" ${SHADER_INCLUDES}
            COMMENT "shadercross HLSL: ${hlsl}"
            VERBATIM)
        list(APPEND SHADER_OUTPUTS ${outputs})
    endforeach()
    set(STAMP "${CMAKE_CURRENT_BINARY_DIR}/${TARGET}.stamp")
    add_custom_command(
        OUTPUT  "${STAMP}"
        COMMAND "${CMAKE_COMMAND}" -E touch "${STAMP}"
        DEPENDS ${SHADER_OUTPUTS}
        VERBATIM)
    add_custom_target(${TARGET} DEPENDS "${STAMP}")
endfunction()

# TIE-owned flight shaders; Aeron builds the shared scene shaders.
tie_add_shaders(flight_shaders
    "${CMAKE_SOURCE_DIR}/shaders"
    flight_mesh.vert.hlsl
    flight_line.vert.hlsl
    flight_mesh_classic_lut.frag.hlsl
    flight_hyperstars.vert.hlsl
    flight_hyperstars.frag.hlsl
    flight_stars.vert.hlsl
    flight_stars.frag.hlsl
    # cockpit_gpu shares the flight shader build target.
    cockpit_pip_compose.vert.hlsl
    cockpit_pip_compose.frag.hlsl
)

# Builds KTX2 cubemaps from explicit SOURCES or every PNG/HDR in INPUT_DIR.
# Additional PACK_ARGS are passed to skybox_pack.
function(tie_add_skyboxes TARGET INPUT_DIR OUTPUT_DIR)
    if(NOT TIE_BUILD_TOOLS)
        message(FATAL_ERROR "tie_add_skyboxes(${TARGET}) requires TIE_BUILD_TOOLS=ON")
    endif()
    cmake_parse_arguments(ARG "" "" "SOURCES;PACK_ARGS" ${ARGN})

    if (ARG_SOURCES)
        set(INPUTS ${ARG_SOURCES})
    else()
        file(GLOB INPUTS CONFIGURE_DEPENDS
             "${INPUT_DIR}/*.png" "${INPUT_DIR}/*.hdr")
    endif()

    file(MAKE_DIRECTORY "${OUTPUT_DIR}")
    set(OUTPUTS "")
    foreach(in_path IN LISTS INPUTS)
        get_filename_component(stem "${in_path}" NAME_WE)
        set(out_path "${OUTPUT_DIR}/${stem}.ktx2")
        add_custom_command(
            OUTPUT  "${out_path}"
            COMMAND $<TARGET_FILE:skybox_pack>
                    "${in_path}" "${out_path}" ${ARG_PACK_ARGS}
            DEPENDS "${in_path}" skybox_pack
            COMMENT "skybox_pack: ${stem} -> ${stem}.ktx2"
            VERBATIM)
        list(APPEND OUTPUTS "${out_path}")
    endforeach()
    add_custom_target(${TARGET} DEPENDS ${OUTPUTS})
endfunction()

add_subdirectory(src/tie_remaster/scene2d)
add_subdirectory(src/tie_remaster)
add_subdirectory(src/tie_app)

if(TIE_BUILD_TOOLS)
add_subdirectory(tools)
endif()

# Format first-party C sources only.
find_program(CLANG_FORMAT clang-format)
if(CLANG_FORMAT)
    file(GLOB_RECURSE TIE_FORMAT_FILES CONFIGURE_DEPENDS
        "${CMAKE_SOURCE_DIR}/src/*.c"      "${CMAKE_SOURCE_DIR}/src/*.h"
        "${CMAKE_SOURCE_DIR}/tools/*.c"    "${CMAKE_SOURCE_DIR}/tools/*.h"
        "${CMAKE_SOURCE_DIR}/examples/*.c" "${CMAKE_SOURCE_DIR}/examples/*.h")
    list(FILTER TIE_FORMAT_FILES EXCLUDE REGEX "/src/third_party/")

    add_custom_target(format
        COMMAND ${CLANG_FORMAT} --Wno-error=unknown -i ${TIE_FORMAT_FILES}
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Formatting C sources with clang-format"
        VERBATIM)

    add_custom_target(format-check
        COMMAND ${CLANG_FORMAT} --Wno-error=unknown --dry-run --Werror ${TIE_FORMAT_FILES}
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Checking C source formatting"
        VERBATIM)
else()
    add_custom_target(format
        COMMAND ${CMAKE_COMMAND} -E echo "clang-format not found; cannot format sources")

    add_custom_target(format-check
        COMMAND ${CMAKE_COMMAND} -E echo "clang-format not found; cannot check source formatting"
        COMMAND ${CMAKE_COMMAND} -E false)
endif()
