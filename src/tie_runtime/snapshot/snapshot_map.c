#include "tie_runtime/snapshot/snapshot_map.h"

#include "tie_runtime/snapshot/capture_views.h"
#include "tie_runtime/snapshot/snapshot_internal.h"

void TieMapSnapshot_Capture(void) {
	TieMapHeader* map = TieSnapshotBuilder_MapMut();
	if (!map)
		return;

	TieRecoveredMapSnapshotView view;
	if (!TieRecoveredMap_ReadSnapshotView(&view) || !view.active) {
		map->active = 0;
		return;
	}

	map->active = 1;
	map->bg_kind = view.background_kind;
	map->has_polygon = view.has_polygon ? 1 : 0;
	map->src_rect_w = view.source_width;
	map->src_rect_h = view.source_height;
	map->dst_rect_x = view.destination_x;
	map->dst_rect_y = view.destination_y;
	for (int index = 0; index < 4; ++index) {
		map->dst_poly_x[index] = view.polygon_x[index];
		map->dst_poly_y[index] = view.polygon_y[index];
	}
	map->start_z = (int16_t)0x7ffe;
	TieSnapshotBuilder_SetSceneClock(view.scene_time, 0.0f, 0u);
}
