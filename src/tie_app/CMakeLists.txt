add_executable(tie
    application.c
    application.h
    frame_loop.c
    frame_loop.h
    hotkeys.c
    hotkeys.h
    main.c
    midi_resources.c
    ui.c
    config/app_config.c
    settings/flight_options.c
    settings/inflight_options.c
    settings/launch_options.c
    settings/video_options.c
    settings/settings.c
    settings/controller_page.c
    setup/installation.c
    setup/installation_ui.c
    setup/setup.c)

# Aeron resolves generated shaders and staged resources relative to the
# executable. Keep the application beside those build-tree directories even
# though its sources are organized under src/tie_app.
set_target_properties(tie PROPERTIES
    OUTPUT_NAME "OpenTIE"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

if(TIE_ENABLE_DEBUG_UI)
    target_sources(tie PRIVATE
        debug/pbr_global_inspector.cpp
        debug/point_lights_editor.cpp
        debug/ssao_inspector.cpp
        debug/directional_shadow_inspector.cpp)
endif()

target_include_directories(tie PRIVATE
    ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(tie PRIVATE
    tie_core
    tie_remaster)
target_compile_definitions(tie PRIVATE
    TIE_SHADER_RELATIVE_DIR="${TIE_SHADER_RELATIVE_DIR}"
    TIE_VERSION_STRING="${TIE_VERSION}")
if(TIE_GPU_DEBUG_LABELS)
    target_compile_definitions(tie PRIVATE TIE_GPU_DEBUG_LABELS=1)
else()
    target_compile_definitions(tie PRIVATE TIE_GPU_DEBUG_LABELS=0)
endif()
tie_enable_warnings(tie)

add_dependencies(tie flight_shaders)

if(NOT APPLE)
    set(TIE_AERON_SCENE3D_DEFAULTS
        "${CMAKE_SOURCE_DIR}/aeron/config/scene3d_defaults.yaml")
    add_custom_target(tie_aeron_scene3d_defaults
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_FILE_DIR:tie>/resources/aeron"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${TIE_AERON_SCENE3D_DEFAULTS}"
            "$<TARGET_FILE_DIR:tie>/resources/aeron/scene3d_defaults.yaml"
        DEPENDS "${TIE_AERON_SCENE3D_DEFAULTS}"
        COMMENT "Staging Aeron Scene3D defaults for TIE"
        VERBATIM)
    add_dependencies(tie tie_aeron_scene3d_defaults)
    add_custom_command(TARGET tie POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/resources"
            "$<TARGET_FILE_DIR:tie>/resources"
        COMMENT "Staging OpenTIE application resources"
        VERBATIM)
endif()

if(APPLE)
    set(TIE_MACOS_BUILD_VERSION "1" CACHE STRING "OpenTIE macOS bundle build version")
    if(NOT DEFINED TIE_MACOS_BUNDLE_IDENTIFIER)
        set(TIE_MACOS_BUNDLE_IDENTIFIER "org.totallyopen.opentie" CACHE STRING
            "OpenTIE macOS bundle identifier")
    elseif(TIE_MACOS_BUNDLE_IDENTIFIER STREQUAL "org.totallyopen.tie")
        set(TIE_MACOS_BUNDLE_IDENTIFIER "org.totallyopen.opentie" CACHE STRING
            "OpenTIE macOS bundle identifier" FORCE)
    endif()
    set_property(CACHE TIE_MACOS_BUNDLE_IDENTIFIER PROPERTY HELPSTRING
        "OpenTIE macOS bundle identifier")
    set_target_properties(tie PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/cmake/macos/Info.plist.in"
        INSTALL_RPATH "@executable_path/../Frameworks")
    set(TIE_MACOS_ICON "${CMAKE_SOURCE_DIR}/cmake/macos/TIE.icns")
    target_sources(tie PRIVATE "${TIE_MACOS_ICON}")
    set_source_files_properties("${TIE_MACOS_ICON}" PROPERTIES MACOSX_PACKAGE_LOCATION Resources)

    set(TIE_MACOS_ROOT_RESOURCE_FILES
        "${CMAKE_SOURCE_DIR}/resources/config.yaml")
    target_sources(tie PRIVATE ${TIE_MACOS_ROOT_RESOURCE_FILES})
    set_source_files_properties(${TIE_MACOS_ROOT_RESOURCE_FILES} PROPERTIES
        HEADER_FILE_ONLY TRUE
        MACOSX_PACKAGE_LOCATION Resources/resources)

    set(TIE_MACOS_FLIGHT_RESOURCE_FILES
        "${CMAKE_SOURCE_DIR}/resources/flight/tie98-models.yaml")
    target_sources(tie PRIVATE ${TIE_MACOS_FLIGHT_RESOURCE_FILES})
    set_source_files_properties(${TIE_MACOS_FLIGHT_RESOURCE_FILES} PROPERTIES
        HEADER_FILE_ONLY TRUE
        MACOSX_PACKAGE_LOCATION Resources/resources/flight)

    set(TIE_MACOS_FONT_RESOURCE_FILES
        "${CMAKE_SOURCE_DIR}/resources/assets/fonts/menu.fnt"
        "${CMAKE_SOURCE_DIR}/resources/assets/fonts/menu.png")
    target_sources(tie PRIVATE ${TIE_MACOS_FONT_RESOURCE_FILES})
    set_source_files_properties(${TIE_MACOS_FONT_RESOURCE_FILES} PROPERTIES
        HEADER_FILE_ONLY TRUE
        MACOSX_PACKAGE_LOCATION Resources/resources/assets/fonts)

    set(TIE_MACOS_AERON_RESOURCE_FILES
        "${CMAKE_SOURCE_DIR}/aeron/config/scene3d_defaults.yaml")
    target_sources(tie PRIVATE ${TIE_MACOS_AERON_RESOURCE_FILES})
    set_source_files_properties(${TIE_MACOS_AERON_RESOURCE_FILES} PROPERTIES
        HEADER_FILE_ONLY TRUE
        MACOSX_PACKAGE_LOCATION Resources/resources/aeron)

    add_custom_command(TARGET tie POST_BUILD
        COMMAND ${CMAKE_COMMAND}
            "-DTIE_BUNDLE_CONTENT_DIR=$<TARGET_BUNDLE_CONTENT_DIR:tie>"
            "-DTIE_SHADER_SOURCE_DIR=${TIE_SHADER_BINARY_DIR}"
            -P "${CMAKE_SOURCE_DIR}/cmake/macos/StageBundle.cmake"
        COMMENT "Staging OpenTIE macOS shaders"
        VERBATIM)
    install(TARGETS tie BUNDLE DESTINATION .)
    set(TIE_MACOS_DEPENDENCY_DIRS)
    foreach(prefix IN LISTS CMAKE_PREFIX_PATH)
        list(APPEND TIE_MACOS_DEPENDENCY_DIRS "${prefix}/lib")
    endforeach()
    list(JOIN TIE_MACOS_DEPENDENCY_DIRS ";" TIE_MACOS_DEPENDENCY_DIRS)
    configure_file("${CMAKE_SOURCE_DIR}/cmake/macos/FixupBundle.cmake.in"
                   "${CMAKE_CURRENT_BINARY_DIR}/FixupTieBundle.cmake" @ONLY)
    install(SCRIPT "${CMAKE_CURRENT_BINARY_DIR}/FixupTieBundle.cmake")
endif()

if(WIN32)
    set_target_properties(tie PROPERTIES WIN32_EXECUTABLE TRUE)
    enable_language(RC)
    if(TIE_VERSION MATCHES "^([0-9]+)\\.([0-9]+)\\.([0-9]+)")
        set(TIE_VERSION_MAJOR ${CMAKE_MATCH_1})
        set(TIE_VERSION_MINOR ${CMAKE_MATCH_2})
        set(TIE_VERSION_PATCH ${CMAKE_MATCH_3})
    else()
        set(TIE_VERSION_MAJOR 0)
        set(TIE_VERSION_MINOR 0)
        set(TIE_VERSION_PATCH 0)
    endif()
    set(TIE_WINDOWS_ICON "${CMAKE_SOURCE_DIR}/cmake/windows/tie.ico")
    configure_file("${CMAKE_SOURCE_DIR}/cmake/windows/tie.rc.in"
                   "${CMAKE_CURRENT_BINARY_DIR}/tie.rc" @ONLY)
    target_sources(tie PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/tie.rc")
endif()
