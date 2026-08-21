#ifndef TIE_SCENE2D_CURSOR_H
#define TIE_SCENE2D_CURSOR_H

/*
 * Cursor compositor — records the engine cursor sprite at the supplied
 * viewport-pixel hotspot. The compositor
 * doesn't compute the hotspot (the snapshot's classic-coord pose vs
 * the application's pillarbox-extended canonical pose are caller business).
 *
 * `cursor_texture` is the 32×32 RGBA Aeron texture maintained by the
 * remaster cursor renderer.
 */

#include "tie_remaster/scene2d/canvas.h"
#include "tie_runtime/snapshot/snapshot_types.h" /* TieCursorState */

#ifdef __cplusplus
extern "C" {
#endif

/* `tex_dim` is the cursor texture's full size in pixels (the SDL
 * backend uses 32; the cursor itself is `cursor->w` × `cursor->h`
 * in the top-left corner, with the rest transparent). UV samples
 * the [0..cursor->w/tex_dim] × [0..cursor->h/tex_dim] sub-rect.
 *
 * `scale_x/y` are the classic→viewport pixel scale (cursor visual
 * size stays consistent across pillarbox extension by tying it to
 * the 4:3 region scale, not the full viewport). No-op when
 * cursor->visible == 0 or w/h <= 0. */
void TieScene2dCursor_Record(TieScene2dCanvas* canvas, AeronTexture* cursor_texture, int tex_dim,
							 const TieCursorState* cursor, float hotspot_vp_x, float hotspot_vp_y,
							 float scale_x, float scale_y);

#ifdef __cplusplus
}
#endif

#endif
