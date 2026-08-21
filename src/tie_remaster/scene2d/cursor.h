/* GPU presentation for Landru's cursor bitmap. TIE95 uses it above the
 * optional HD frontend, while TIE98 uses it instead of modifying the
 * DirectDraw-backed Landru surface. */
#ifndef TIE_REMASTER_SCENE2D_CURSOR_H
#define TIE_REMASTER_SCENE2D_CURSOR_H

#include "tie_runtime/snapshot/snapshot_types.h" /* TieCursorState */

#include "aeron/render.h"
#include "aeron/scene/draw_list2d.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct TieScene2dCursorRenderer TieScene2dCursorRenderer;

TieScene2dCursorRenderer* TieScene2dCursor_Init(void);

void TieScene2dCursor_Shutdown(TieScene2dCursorRenderer* g);

/* Draw into a dedicated transparent target which the application submits as
 * its final game layer. The sprite hotspot and scale are target pixels. */
void TieScene2dCursor_RecordLayer(TieScene2dCursorRenderer* g, AeronDrawList2D* list, int rt_w, int rt_h,
								  float hot_x, float hot_y, float scale_x, float scale_y,
								  const TieCursorState* cursor);

bool TieScene2dCursor_Prep(TieScene2dCursorRenderer* g, AeronCommandBuffer* cmd, const uint8_t* bitmap,
						   int16_t bitmap_width, int16_t bitmap_height, const uint32_t* palette);

#ifdef __cplusplus
}
#endif

#endif
