add_executable(species_billboard_extract
    species_billboard_extract.c
    species_bb.c)
target_include_directories(species_billboard_extract PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(species_billboard_extract PRIVATE tie_tool_lfd tie_formats imgbake)
