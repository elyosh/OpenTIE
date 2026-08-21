# Modern renderer for the game application.
add_library(tie_remaster STATIC
    remaster.c
    flight/cockpit/renderer.c
    flight/cockpit/layout.c
    flight/cockpit/text.c
    flight/backdrops.c
    flight/billboards.c
    flight/classic_lighting.c
    flight/renderer.c
    flight/classic_draw.c
    flight/mesh_draw.c
    flight/scene_mesh.c
    flight/line_draw.c
    flight/render_math.c
    flight/mesh_common.c
    flight/passes.c
    flight/pipelines.c
    flight/classic_mesh.c
    flight/hyperstars.c
    flight/pbr.c
    flight/point_lights.c
    flight/sprite_cache.c
    flight/stars.c
    flight/world_ambient.c
    integration/snapshot_adapter.c
)
if(TIE_ENABLE_DEBUG_UI)
    target_sources(tie_remaster PRIVATE
        debug/debug_tools.cpp
        debug/camera_inspector.cpp
        debug/world_ambient_editor.cpp
        debug/gltf_mesh_inspector.cpp
        debug/motion_blur_inspector.cpp
        debug/hdr_inspector.cpp
        debug/cockpit_inspector.cpp)
endif()
target_include_directories(tie_remaster PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(tie_remaster
    PUBLIC aeron aeron_scene aeron_asset tie_formats
    PRIVATE tie_core tie_remaster_scene2d lucas_opt PkgConfig::ZSTD tie_libm)
target_compile_definitions(tie_remaster PRIVATE TIE_SHADER_DIR="${TIE_SHADER_BINARY_DIR}")
if(TIE_GPU_DEBUG_LABELS)
    target_compile_definitions(tie_remaster PRIVATE TIE_GPU_DEBUG_LABELS=1)
else()
    target_compile_definitions(tie_remaster PRIVATE TIE_GPU_DEBUG_LABELS=0)
endif()
tie_enable_warnings(tie_remaster)
