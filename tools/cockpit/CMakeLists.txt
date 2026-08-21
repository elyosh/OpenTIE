add_executable(cockpit_extract
    cockpit_extract.c
    panel_int.c)
target_include_directories(cockpit_extract PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(cockpit_extract PRIVATE tie_formats imgbake)
