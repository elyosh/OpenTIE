#include "tie/flight_composite_tie98.h"

#include "tie/frontend_display_tie98.h"
#include "tie/logbuf2.h"
#include "tie/render_texture_tie98.h"
#include "tie/std3d_tie98.h"
#include "tie/tie.h"
#include "tie/xtrans2.h"

#include "aeron/dx5/compat.h"

#include <stdint.h>
#include <stdlib.h>
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

/* PORT: host-owned coverage plane consumed by the render-target composition. */
static uint8_t* g_cockpitCoverage;
static size_t g_cockpitCoverageCapacity;

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
	if (coverage_size == 0)
		return;
	if (g_cockpitCoverageCapacity < coverage_size) {
		uint8_t* coverage = realloc(g_cockpitCoverage, coverage_size);
		if (!coverage)
			return;
		g_cockpitCoverage = coverage;
		g_cockpitCoverageCapacity = coverage_size;
	}

	const int view_left = (int)displaycorner_columns;
	const int view_top = (int)displaycorner_lines;
	const uint8_t* mask = (const uint8_t*)xtransdataptr + (uint16_t)maskbufptr;
	uint8_t* row = g_cockpitCoverage;

	memset(row, 255, (size_t)width * (size_t)view_top);
	row += (size_t)width * (size_t)view_top;
	for (int y = 0; y < pixelsdeep; ++y) {
		memset(row, 255, (size_t)view_left);
		int8_t copy_run = (int8_t)*mask++;
		int x = 0;
		while (x < pixelswide) {
			int run = *mask++;
			if (run == 0) {
				run = *mask++;
				if (run == 0)
					run = *mask++ + 256;
				run += 255;
			}
			int fill = run;
			if (fill > width - (view_left + x))
				fill = width - (view_left + x);
			if (fill > 0)
				memset(row + view_left + x, copy_run < 0 ? 255 : 0, (size_t)fill);
			x += run;
			copy_run = (int8_t)-copy_run;
		}
		const int view_right = view_left + x;
		if (width > view_right)
			memset(row + view_right, 255, (size_t)(width - view_right));
		row += width;
	}
	const int TieScene2dViewport_Bottom = view_top + pixelsdeep;
	if (height > TieScene2dViewport_Bottom)
		memset(row, 255, (size_t)width * (size_t)(height - TieScene2dViewport_Bottom));

	AeronDx5_ComposeSurfaceOverRenderTarget(g_lpRenderSurface, (int)((g_displayWidth - g_surfaceWidth) >> 1),
											(int)((g_displayHeight - g_surfaceHeight) >> 1),
											g_flightOffscreenSurface, g_cockpitCoverage, width);
}
