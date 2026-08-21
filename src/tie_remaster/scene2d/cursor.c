/* GPU presentation for the engine's 8bpp Landru cursor bitmap. */

#include "tie_remaster/scene2d/cursor.h"
#include "tie_remaster/scene2d/cursor_layout.h"

#include <stdlib.h>
#include <string.h>

#define CURSOR_MAX_DIM 32

struct TieScene2dCursorRenderer {
	AeronTexture* tex;
	int tex_w, tex_h;
	/* CPU staging for the palette-expanded cursor pixels; the HAL's
	 * TieScene2dCursor_Upload path copies from here each prep. */
	uint8_t staging[CURSOR_MAX_DIM * CURSOR_MAX_DIM * 4];
};

TieScene2dCursorRenderer* TieScene2dCursor_Init(void) {
	TieScene2dCursorRenderer* g = (TieScene2dCursorRenderer*)calloc(1, sizeof *g);
	if (!g) {
		Aeron_RequestFatalRendererError("cursor renderer allocation");
		return NULL;
	}
	/* _SRGB: cursor pixels come from the classic engine palette
	 * (sRGB-encoded VGA bytes). HW decodes on sample, the cursor
	 * shader composites in linear, then HW encodes again at the
	 * swapchain store. */
	g->tex = Aeron_CreateTexture(&(AeronTextureDesc) {
		.width = CURSOR_MAX_DIM,
		.height = CURSOR_MAX_DIM,
		.mip_count = 1,
		.format = AERON_TEXTURE_FORMAT_RGBA8_SRGB,
		.usage = AERON_TEXTURE_USAGE_SAMPLED | AERON_TEXTURE_USAGE_TRANSFER_DST,
	});
	if (!g->tex) {
		Aeron_RequestFatalRendererError("cursor texture creation");
		free(g);
		return NULL;
	}
	g->tex_w = CURSOR_MAX_DIM;
	g->tex_h = CURSOR_MAX_DIM;
	return g;
}

void TieScene2dCursor_Shutdown(TieScene2dCursorRenderer* g) {
	if (!g)
		return;
	if (g->tex)
		Aeron_DestroyTexture(g->tex);
	free(g);
}

/* Upload the cursor bitmap as RGBA8 through the CPU staging buffer.
 * Index 0 = transparent (engine color-key); other indices use the
 * snapshot palette with alpha=255. */
static bool TieScene2dCursor_Upload(TieScene2dCursorRenderer* g, AeronCommandBuffer* cmd,
									const uint8_t* bitmap, int w, int h, const uint32_t* palette) {
	if (!bitmap || w <= 0 || h <= 0)
		return false;
	if (w > CURSOR_MAX_DIM)
		w = CURSOR_MAX_DIM;
	if (h > CURSOR_MAX_DIM)
		h = CURSOR_MAX_DIM;

	memset(g->staging, 0, sizeof g->staging);
	for (int y = 0; y < h; y++) {
		const uint8_t* src = bitmap + (size_t)y * (size_t)w;
		uint8_t* dst = g->staging + (size_t)y * CURSOR_MAX_DIM * 4;
		for (int x = 0; x < w; x++) {
			uint8_t idx = src[x];
			if (idx == 0)
				continue; /* color-key */
			uint32_t argb = palette[idx];
			dst[x * 4 + 0] = (uint8_t)((argb >> 16) & 0xFFu);
			dst[x * 4 + 1] = (uint8_t)((argb >> 8) & 0xFFu);
			dst[x * 4 + 2] = (uint8_t)(argb & 0xFFu);
			dst[x * 4 + 3] = 255;
		}
	}

	return Aeron_UploadTextureDataCmd(cmd, &(AeronTextureUploadDesc) {
											   .texture = g->tex,
											   .width = CURSOR_MAX_DIM,
											   .height = CURSOR_MAX_DIM,
											   .pixels = g->staging,
											   .pitch = CURSOR_MAX_DIM * 4,
											   .pixel_format = AERON_PIXEL_FORMAT_RGBA8888,
											   .color_space = AERON_COLOR_SPACE_SRGB,
											   .cycle = 1,
										   }) != 0;
}

void TieScene2dCursor_RecordLayer(TieScene2dCursorRenderer* g, AeronDrawList2D* list, int rt_w, int rt_h,
								  float hot_x, float hot_y, float scale_x, float scale_y,
								  const TieCursorState* cursor) {
	if (!g || !list || !cursor)
		return;
	if (rt_w <= 0 || rt_h <= 0)
		return;

	TieScene2dCanvas canvas;
	TieScene2dCanvas_Begin(&canvas, list, rt_w, rt_h);
	TieScene2dCursor_Record(&canvas, g->tex, CURSOR_MAX_DIM, cursor, hot_x, hot_y, scale_x, scale_y);
}

bool TieScene2dCursor_Prep(TieScene2dCursorRenderer* g, AeronCommandBuffer* cmd, const uint8_t* bitmap,
						   int16_t bitmap_width, int16_t bitmap_height, const uint32_t* palette) {
	if (!g || !cmd || !palette)
		return false;
	if (!bitmap)
		return true;
	if (!TieScene2dCursor_Upload(g, cmd, bitmap, bitmap_width, bitmap_height, palette)) {
		Aeron_RequestFatalRendererError("cursor texture upload");
		return false;
	}
	return true;
}
