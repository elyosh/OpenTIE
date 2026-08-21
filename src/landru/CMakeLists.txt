set(LANDRU_SOURCES
    actanim.c
    actcust.c
    actdelt.c
    actor.c
    actraw.c
    bitmap.c
    btnchk.c
    btnpush.c
    btnsldr.c
    btnstr.c
    btntext.c
    canvas.c
    color.c
    cursor.c
    delta.c
    dialog.c
    dirty.c
    error.c
    fade.c
    file.c
    filedir.c
    film.c
    flip.c
    font.c
    host.c
    inpattr.c
    inpcall.c
    input.c
    io.c
    joy.c
    memcom.c
    memptr.c
    mouse.c
    paint.c
    pal.c
    paragrp.c
    rect.c
    remap.c
    render.c
    res.c
    scale.c
    sound.c
    stream.c
    style.c
    surface.c
    task.c
    timer.c
    vesa.c
    video.c
    view.c
    viewadd.c
)

add_library(landru STATIC ${LANDRU_SOURCES})
add_library(landru::landru ALIAS landru)

target_include_directories(landru
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

# The root project has a legacy directory-wide `src` include. Replace the
# inherited compile path so this target cannot accidentally reach TIE headers.
set_property(TARGET landru PROPERTY INCLUDE_DIRECTORIES
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

set_target_properties(landru PROPERTIES
    VERSION 0.1.0
    SOVERSION 1
    POSITION_INDEPENDENT_CODE ON
)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

install(DIRECTORY include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    FILES_MATCHING PATTERN "*.h"
)

install(TARGETS landru
    EXPORT landruTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(EXPORT landruTargets
    FILE landruTargets.cmake
    NAMESPACE landru::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/landru
)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/landruConfigVersion.cmake"
    VERSION 0.1.0
    COMPATIBILITY SameMajorVersion
)

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/landruConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/landruConfig.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/landru
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/landruConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/landruConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/landru
)
