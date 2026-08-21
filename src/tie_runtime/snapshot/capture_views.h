#ifndef TIE_RUNTIME_SNAPSHOT_CAPTURE_VIEWS_H
#define TIE_RUNTIME_SNAPSHOT_CAPTURE_VIEWS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <landru/render.h>

#include "tie_runtime/snapshot/snapshot_types.h"

typedef struct TieRecoveredMapSnapshotView {
	bool active;
	uint8_t background_kind;
	bool has_polygon;
	int16_t source_width;
	int16_t source_height;
	int16_t polygon_x[4];
	int16_t polygon_y[4];
	int16_t destination_x;
	int16_t destination_y;
	int32_t scene_time;
} TieRecoveredMapSnapshotView;

typedef struct TieRecoveredTextSnapshotLine {
	char text[TIE_UI_TEXT_MAX_CHARS];
	int16_t x;
	int16_t y;
	uint8_t color;
	uint8_t bold_color;
	uint8_t shadow_color;
} TieRecoveredTextSnapshotLine;

bool TieRecoveredMap_ReadSnapshotView(TieRecoveredMapSnapshotView* out);
int TieRecoveredTitle_SnapshotLineCount(void);
bool TieRecoveredTitle_ReadSnapshotLine(int index, char* text, size_t capacity, float* initial_y);
int TieRecoveredText_SnapshotLineCount(void);
bool TieRecoveredText_ReadSnapshotLine(int index, TieRecoveredTextSnapshotLine* out);
int TieRecoveredLogo_ReadSnapshotActors(LandruActorRenderState* actors, int capacity);

#endif
