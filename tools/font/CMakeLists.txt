add_executable(font_extract font_extract.c)
target_link_libraries(font_extract PRIVATE tie_tool_lfd imgbake)

add_executable(cockpit_font_extract cockpit_font_extract.c)
target_link_libraries(cockpit_font_extract PRIVATE tie_formats imgbake)

add_executable(font_tune font_tune.cpp)
target_link_libraries(font_tune PRIVATE imgbake aeron_imgui PkgConfig::SDL3 aeron)
target_compile_features(font_tune PRIVATE cxx_std_17)
