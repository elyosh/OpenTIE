add_library(tie_remaster_scene2d STATIC
    actor_layout.c
    canvas.c
    cursor.c
    cursor_layout.c
    cutscene.c
    manifest.c
    manifest_expr.c
    map.c
    map_composition.c
    paint.c
    paint_commands.c
    snapshot_merge.c
    text.c
    text_layout.c
    viewport.c)
target_include_directories(tie_remaster_scene2d PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(tie_remaster_scene2d
    PUBLIC aeron aeron_scene)
set_target_properties(tie_remaster_scene2d PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    POSITION_INDEPENDENT_CODE ON)
tie_enable_warnings(tie_remaster_scene2d)
