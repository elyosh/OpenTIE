# libimuse — iMUSE sound library.
#
# Layout:
#   include/                 public headers (consumer surface)
#       imuse.h              umbrella
#       imuse/{commands,filelist,groups,handle,hilevel,lolevel,
#              midi_backend,midi_fluidsynth,midi_fm4_opl3,version}.h
#   internal/                private headers (implementation only)
#   *.c                      implementation
#
# Built and installed as a CMake package (`find_package(imuse)`).
# Out-of-tree consumers see only what's reachable through include/.

# Public option: enable the FluidSynth-backed MIDI backend. When OFF
# (or when FluidSynth isn't on the system), midi_fluidsynth.c builds
# as a silent stub that returns NULL from imuse_midi_fluidsynth_create.
option(IMUSE_ENABLE_FLUIDSYNTH "Enable FluidSynth MIDI backend" ON)

# Public option: build the self-contained TIE FM4 backend with the vendored
# Nuked-OPL3-fast synthesizer.
option(IMUSE_ENABLE_FM4_OPL3 "Enable FM4 Nuked-OPL3 MIDI backend" ON)

option(IMUSE_ENABLE_NUKED_SC55 "Enable Nuked SC-55 MIDI backend" ON)

# Public option: per-event MIDI backend trace. Compiled in by default;
# output is gated at runtime by the host log callback's level filter
# (libimuse emits trace events at IMUSE_LOG_TRACE — the host can
# ignore them).
option(IMUSE_ENABLE_GMIDI_TRACE "Compile per-event MIDI backend tracing" ON)

# Probe FluidSynth via pkg-config when enabled. The dependency is
# re-resolved by downstream consumers (see imuseConfig.cmake.in)
# rather than baked in as an absolute path so the install tree
# stays relocatable.
if (IMUSE_ENABLE_FLUIDSYNTH)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(FLUIDSYNTH IMPORTED_TARGET fluidsynth)
    if (FLUIDSYNTH_FOUND)
        message(STATUS "libimuse: FluidSynth found -- midi_fluidsynth backend uses it")
    else()
        message(WARNING "libimuse: FluidSynth not found -- midi_fluidsynth backend is a silent stub")
    endif()
endif()

set(IMUSE_DEPENDENCY_TARGETS)
if (IMUSE_ENABLE_FM4_OPL3)
    set(NUKED_OPL3_FAST_DIR
        "${CMAKE_CURRENT_SOURCE_DIR}/../third_party/nuked-opl3-fast")
    foreach(required_file opl3.c opl3.h wf_rom.h LICENSE UPSTREAM.md)
        if (NOT EXISTS "${NUKED_OPL3_FAST_DIR}/${required_file}")
            message(FATAL_ERROR
                "libimuse: missing vendored Nuked-OPL3-fast file "
                "${NUKED_OPL3_FAST_DIR}/${required_file}")
        endif()
    endforeach()

    add_library(nuked_opl3_fast STATIC
        "${NUKED_OPL3_FAST_DIR}/opl3.c")
    target_include_directories(nuked_opl3_fast
        PRIVATE "${NUKED_OPL3_FAST_DIR}")
    target_compile_definitions(nuked_opl3_fast PRIVATE
        OPL_WF_TABLE_RUNTIME=0
        OPL_ENABLE_STEREOEXT=0
        OPL_COMPAT_OLD_EG=0
        OPL_COMPAT_DEFERRED_4OP_ALG=0)
    if (MSVC)
        target_compile_options(nuked_opl3_fast PRIVATE /W0)
    else()
        target_compile_options(nuked_opl3_fast PRIVATE -w)
    endif()
    set_target_properties(nuked_opl3_fast PROPERTIES
        POSITION_INDEPENDENT_CODE ON)
    list(APPEND IMUSE_DEPENDENCY_TARGETS nuked_opl3_fast)
endif()

set(IMUSE_NUKED_SC55_AVAILABLE OFF)
if (IMUSE_ENABLE_NUKED_SC55)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SPEEXDSP IMPORTED_TARGET speexdsp)
    set(NUKED_SC55_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../third_party/nuked-sc55")
    if (SPEEXDSP_FOUND AND EXISTS "${NUKED_SC55_DIR}/src/backend/emu.cpp")
        set(NUKED_SC55_BACKEND_DIR "${NUKED_SC55_DIR}/src/backend")
        add_library(nuked_sc55_core STATIC
            "${NUKED_SC55_BACKEND_DIR}/diagnostics.cpp"
            "${NUKED_SC55_BACKEND_DIR}/emu.cpp"
            "${NUKED_SC55_BACKEND_DIR}/file_hashing.cpp"
            "${NUKED_SC55_BACKEND_DIR}/file_io.cpp"
            "${NUKED_SC55_BACKEND_DIR}/lcd.cpp"
            "${NUKED_SC55_BACKEND_DIR}/mcu_interrupt.cpp"
            "${NUKED_SC55_BACKEND_DIR}/mcu_opcodes.cpp"
            "${NUKED_SC55_BACKEND_DIR}/mcu_timer.cpp"
            "${NUKED_SC55_BACKEND_DIR}/mcu.cpp"
            "${NUKED_SC55_BACKEND_DIR}/pcm.cpp"
            "${NUKED_SC55_BACKEND_DIR}/rom_io.cpp"
            "${NUKED_SC55_BACKEND_DIR}/rom.cpp"
            "${NUKED_SC55_BACKEND_DIR}/standard_romsets.cpp"
            "${NUKED_SC55_BACKEND_DIR}/submcu.cpp"
            "${NUKED_SC55_BACKEND_DIR}/sha/sha224-256.c"
            "${NUKED_SC55_DIR}/src/common/rom_loader.cpp")
        target_include_directories(nuked_sc55_core PRIVATE
            "${NUKED_SC55_BACKEND_DIR}"
            "${NUKED_SC55_DIR}/src/common")
        target_compile_features(nuked_sc55_core PRIVATE cxx_std_23)
        if (MSVC)
            target_compile_options(nuked_sc55_core PRIVATE /W0
                $<$<COMPILE_LANGUAGE:CXX>:/FIcstddef>)
        else()
            target_compile_options(nuked_sc55_core PRIVATE -w
                $<$<COMPILE_LANGUAGE:CXX>:-include>
                $<$<COMPILE_LANGUAGE:CXX>:cstddef>
                $<$<CONFIG:Debug>:-O3>)
        endif()
        set_target_properties(nuked_sc55_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
        list(APPEND IMUSE_DEPENDENCY_TARGETS nuked_sc55_core)
        set(IMUSE_NUKED_SC55_AVAILABLE ON)
        message(STATUS "libimuse: Nuked SC-55 and SpeexDSP found")
    else()
        message(WARNING "libimuse: Nuked SC-55 source or SpeexDSP missing -- backend is unavailable")
    endif()
endif()

set(IMUSE_SOURCES
    commands.c
    debug.c
    dispatch.c
    fades.c
    filelist.c
    files.c
    fm4_data.c
    fm4_driver.c
    gmidi_driver.c
    groups.c
    hilevel.c
    lolevel.c
    midi.c
    midi_backend.c
    midi_fm4_opl3.c
    midi_fluidsynth.c
    midi_nuked_sc55.cpp
    mix.c
    mixer.c
    players.c
    seq.c
    slots.c
    state.c
    state_io.c
    streamer.c
    sustain.c
    timer.c
    tracks.c
    triggers.c
    utils.c
    version.c
    wave.c
    wave_renderer.c
)

add_library(imuse STATIC ${IMUSE_SOURCES})

# Public headers are reachable via:
#   - in-tree builds  → src/imuse/include
#   - install tree    → ${CMAKE_INSTALL_INCLUDEDIR}
target_include_directories(imuse
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

set_target_properties(imuse PROPERTIES
    VERSION   "0.4.0"          # tracks IMUSE_VERSION_*
    SOVERSION 3                # tracks IMUSE_ABI_VERSION
    POSITION_INDEPENDENT_CODE ON
)

if (FLUIDSYNTH_FOUND)
    target_compile_definitions(imuse PRIVATE HAVE_FLUIDSYNTH=1)
    # PUBLIC because the MIDI backend factory's allocations come
    # from FluidSynth's heap; consumers don't need the headers, but
    # the linker needs the library on the link line.
    target_link_libraries(imuse PUBLIC PkgConfig::FLUIDSYNTH)
endif()

if (IMUSE_ENABLE_FM4_OPL3)
    target_compile_definitions(imuse PRIVATE HAVE_FM4_OPL3=1)
    target_include_directories(imuse PRIVATE "${NUKED_OPL3_FAST_DIR}")
    target_link_libraries(imuse PUBLIC nuked_opl3_fast)
endif()

if (IMUSE_NUKED_SC55_AVAILABLE)
    target_compile_features(imuse PRIVATE cxx_std_23)
    target_compile_definitions(imuse PRIVATE HAVE_NUKED_SC55=1)
    target_include_directories(imuse PRIVATE
        "${NUKED_SC55_DIR}/src/backend"
        "${NUKED_SC55_DIR}/src/common")
    target_link_libraries(imuse PUBLIC nuked_sc55_core PkgConfig::SPEEXDSP)
endif()

if (IMUSE_ENABLE_GMIDI_TRACE)
    target_compile_definitions(imuse PRIVATE GMIDI_TRACE=1)
endif()

if (NOT MSVC AND NOT APPLE)
    target_link_libraries(imuse PUBLIC m)
endif()

# ===== Install rules =====
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

# Headers: copy the entire include/ tree so the umbrella + every
# imuse/<x>.h land at ${prefix}/include/.
install(DIRECTORY include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    FILES_MATCHING PATTERN "*.h"
)

# Library + export set.
install(TARGETS imuse ${IMUSE_DEPENDENCY_TARGETS}
    EXPORT imuseTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

if (IMUSE_NUKED_SC55_AVAILABLE)
    install(FILES
        "${NUKED_SC55_DIR}/LICENSE"
        "${CMAKE_CURRENT_SOURCE_DIR}/../third_party/nuked-sc55.UPSTREAM.md"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses/imuse/nuked-sc55")
endif()

# Export the target so downstream find_package(imuse) gets a
# usable IMPORTED `imuse::imuse` target.
install(EXPORT imuseTargets
    FILE imuseTargets.cmake
    NAMESPACE imuse::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/imuse
)

# Generate package version file (semantic-version comparison).
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/imuseConfigVersion.cmake"
    VERSION 0.4.0
    COMPATIBILITY SameMajorVersion
)

# Generate package config from the template. The template re-runs
# pkg-config-based dependency lookup on the consumer side so the
# IMPORTED PkgConfig::FLUIDSYNTH target gets recreated there.
configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/imuseConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/imuseConfig.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/imuse
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/imuseConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/imuseConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/imuse
)
