# Configure before creating targets so dependencies share the modern compiler cache.
option(TIE_ENABLE_COMPILER_CACHE "Cache modern C/C++ compilation with ccache" OFF)
set(TIE_COMPILER_CACHE_DIR "" CACHE PATH "Shared compiler cache directory (defaults to per-user temporary storage)")

if(NOT TIE_ENABLE_COMPILER_CACHE)
    return()
endif()

find_program(TIE_CCACHE_EXECUTABLE NAMES ccache REQUIRED)

# Map the source directory out of debug information so builds from different
# worktree paths produce identical objects and share cache entries.
foreach(language IN ITEMS C CXX)
    if(CMAKE_${language}_COMPILER_ID MATCHES "^(AppleClang|Clang)$"
            AND NOT CMAKE_${language}_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
        add_compile_options("$<$<COMPILE_LANGUAGE:${language}>:-fdebug-compilation-dir=.>")
    elseif(CMAKE_${language}_COMPILER_ID STREQUAL "GNU")
        add_compile_options("$<$<COMPILE_LANGUAGE:${language}>:-fdebug-prefix-map=${CMAKE_SOURCE_DIR}=.>")
    else()
        message(FATAL_ERROR "TIE_ENABLE_COMPILER_CACHE requires a GNU-compatible Clang or GCC ${language} compiler")
    endif()
endforeach()

if(NOT TIE_COMPILER_CACHE_DIR)
    if(NOT UNIX)
        message(FATAL_ERROR "Set TIE_COMPILER_CACHE_DIR to a private shared cache directory on this platform")
    endif()
    execute_process(COMMAND id -u
        OUTPUT_VARIABLE tie_cache_user_id
        OUTPUT_STRIP_TRAILING_WHITESPACE
        COMMAND_ERROR_IS_FATAL ANY)
    if(DEFINED ENV{TMPDIR} AND NOT "$ENV{TMPDIR}" STREQUAL "")
        set(tie_cache_temp_dir "$ENV{TMPDIR}")
    else()
        set(tie_cache_temp_dir "/tmp")
    endif()
    set(TIE_COMPILER_CACHE_DIR "${tie_cache_temp_dir}/opentie-ccache-${tie_cache_user_id}"
        CACHE PATH "Shared compiler cache directory (defaults to per-user temporary storage)" FORCE)
    file(MAKE_DIRECTORY "${TIE_COMPILER_CACHE_DIR}")
    file(CHMOD "${TIE_COMPILER_CACHE_DIR}" PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE)
endif()

if(NOT IS_ABSOLUTE "${TIE_COMPILER_CACHE_DIR}")
    message(FATAL_ERROR "TIE_COMPILER_CACHE_DIR must be an absolute path")
endif()

# Store settings in the launcher so direct builds and every worker phase agree.
# Checking --version identifies the selected toolchain behind /usr/bin/clang on macOS.
set(tie_compiler_cache_launcher
    "${TIE_CCACHE_EXECUTABLE}"
    "cache_dir=${TIE_COMPILER_CACHE_DIR}"
    "base_dir=${CMAKE_SOURCE_DIR}"
    "compiler_check=%compiler% --version")
set(CMAKE_C_COMPILER_LAUNCHER ${tie_compiler_cache_launcher})
set(CMAKE_CXX_COMPILER_LAUNCHER ${tie_compiler_cache_launcher})
message(STATUS "OpenTIE compiler cache: ${TIE_COMPILER_CACHE_DIR}")
