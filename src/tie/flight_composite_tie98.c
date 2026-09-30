#include "tie/flight_composite_tie98.h"
#include "tie/frontend_display_tie98.h"
#include "tie/logbuf2.h"
#include "tie/render_texture_tie98.h"
#include "tie/std3d_tie98.h"
#include "tie/tie.h"
#include "tie/xtrans2.h"

#include "aeron/compat/host.h"
#include "tie_runtime/display/tie98_display.h"

#include <stdint.h>
#include <string.h>

// FUNCTION: TIE98 0x42B500
void RenderScene_ClearFrameBuffers(void) {
	DDBLTFX effects;
	memset(&effects, 0, sizeof effects);
	effects.dwSize = sizeof effects;
	effects.dwFillColor = g_flightTextPalette[g_flightColorKeyIndex];
	g_lpRenderSurface->lpVtbl->Blt(g_lpRenderSurface, NULL, NULL, NULL, DDBLT_COLORFILL | DDBLT_WAIT,
								   &effects);
	std3D_ClearZBuffer();
}

// FUNCTION: TIE98 0x42B560
void Renderer_CopyDirtyRectsToHardwareSurface(void) {
	/* PORT: the original locks the Direct3D render surface and copies the
	 * cockpit-mask pixels from the offscreen surface into it. Locking the
	 * emulated GPU target forces a full readback (a GPU sync stall) and rounds
	 * the rendered frame through 16bpp. The identical mask walk instead builds
	 * a per-pixel coverage plane -- opaque exactly where the original copied --
	 * and the offscreen surface is composed over the GPU frame with it. */
	const int width = g_surfaceWidth;
	const int height = g_surfaceHeight;
	const size_t coverage_size = (size_t)width * (size_t)height;
	int view_left, view_top, view_bottom;
	const uint8_t* mask;
	uint8_t* coverage;
	uint8_t* row;
	int y;
	if (coverage_size == 0)
		return;
	coverage = Tie98Display_ReserveCockpitCoverage(coverage_size);
	if (!coverage)
		return;

	view_left = (int)displaycorner_columns;
	view_top = (int)displaycorner_lines;
	mask = (const uint8_t*)xtransdataptr + (uint16_t)maskbufptr;
	row = coverage;

	memset(row, 255, (size_t)width * (size_t)view_top);
	row += (size_t)width * (size_t)view_top;
	for (y = 0; y < pixelsdeep; ++y) {
		int8_t copy_run;
		int x = 0;
		int view_right;

		memset(row, 255, (size_t)view_left);
		copy_run = (int8_t)*mask++;
		while (x < pixelswide) {
			int run = *mask++;
			int fill;
			if (run == 0) {
				run = *mask++;
				if (run == 0)
					run = *mask++ + 256;
				run += 255;
			}
			fill = run;
			if (fill > width - (view_left + x))
				fill = width - (view_left + x);
			if (fill > 0)
				memset(row + view_left + x, copy_run < 0 ? 255 : 0, (size_t)fill);
			x += run;
			copy_run = (int8_t)-copy_run;
		}
		view_right = view_left + x;
		if (width > view_right)
			memset(row + view_right, 255, (size_t)(width - view_right));
		row += width;
	}
	view_bottom = view_top + pixelsdeep;
	if (height > view_bottom)
		memset(row, 255, (size_t)width * (size_t)(height - view_bottom));

	AeronDx5_ComposeSurfaceOverRenderTarget(g_lpRenderSurface, (int)((g_displayWidth - g_surfaceWidth) >> 1),
											(int)((g_displayHeight - g_surfaceHeight) >> 1),
											g_flightOffscreenSurface, coverage, width);
}
