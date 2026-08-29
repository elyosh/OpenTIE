#include "tie_runtime/presentation/classic_layer.h"

#include <stdint.h>
#include <string.h>

#include "aeron/aeron.h"
#include "aeron/compat/host.h"
#include "tie_runtime/display/classic_framebuffer.h"
#include "tie_runtime/input/input.h"
#include "tie_runtime/presentation/presentation.h"

typedef struct TieClassicLayerState {
	int framebuffer_width;
	int framebuffer_height;
	uint32_t generation;
	AeronPaletteEntry palette[256];
	bool palette_valid;
	bool suppressed;
} TieClassicLayerState;

static TieClassicLayerState s_classic_layer;

void TieClassicLayer_Init(void) { memset(&s_classic_layer, 0, sizeof s_classic_layer); }

void TieClassicLayer_SetSuppressed(bool suppressed) {
	if (suppressed == s_classic_layer.suppressed)
		return;
	AeronDx5_SetClassicFlightRenderingSuppressed(suppressed ? 1 : 0);
	s_classic_layer.suppressed = suppressed;
}

void TieClassicLayer_SyncInputExtent(void) {
	const TieFramebuffer* framebuffer = TieClassicFramebuffer_Current();
	const int width = framebuffer ? framebuffer->width : 0;
	const int height = framebuffer ? framebuffer->height : 0;
	if (width <= 0 || height <= 0 ||
		(width == s_classic_layer.framebuffer_width && height == s_classic_layer.framebuffer_height))
		return;
	s_classic_layer.framebuffer_width = width;
	s_classic_layer.framebuffer_height = height;
	TieInput_SetFramebufferSize(width, height);
}

void TieClassicLayer_Submit(const TieFramebuffer* framebuffer) {
	const TiePresentationLayout* presentation = TiePresentation_Layout();
	if (Aeron_FatalErrorRequested() || !framebuffer || !framebuffer->pixels || framebuffer->width <= 0 ||
		framebuffer->height <= 0 || !presentation)
		return;

	TieClassicLayer_SyncInputExtent();
	AeronPaletteEntry palette[256];
	for (int i = 0; i < 256; ++i) {
		const uint8_t r = framebuffer->palette[i * 3 + 0];
		const uint8_t g = framebuffer->palette[i * 3 + 1];
		const uint8_t b = framebuffer->palette[i * 3 + 2];
		palette[i].r = (uint8_t)((r << 2) | (r >> 4));
		palette[i].g = (uint8_t)((g << 2) | (g >> 4));
		palette[i].b = (uint8_t)((b << 2) | (b >> 4));
		palette[i].a = 255;
	}

	const bool palette_changed =
		!s_classic_layer.palette_valid || memcmp(s_classic_layer.palette, palette, sizeof palette) != 0;
	const bool content_changed = TieClassicFramebuffer_TakeDirty();
	if (palette_changed) {
		memcpy(s_classic_layer.palette, palette, sizeof palette);
		s_classic_layer.palette_valid = true;
	}
	if (!s_classic_layer.generation || content_changed || palette_changed)
		++s_classic_layer.generation;

	const AeronPixelLayerDesc layer = {
		.frame = {
			.pixels = framebuffer->pixels,
			.width = framebuffer->width,
			.height = framebuffer->height,
			.pitch = framebuffer->pitch,
			.bpp = 8,
			.format = AERON_PIXEL_FORMAT_INDEX8,
			.color_space = AERON_COLOR_SPACE_SRGB,
			.palette = s_classic_layer.palette,
			.generation = s_classic_layer.generation,
		},
		.logical_rect = presentation->classic,
		.blend_mode = AERON_LAYER_BLEND_OPAQUE,
		.sampling = AERON_PIXEL_SAMPLING_SHARP_BILINEAR,
	};
	if (!Aeron_SubmitPixelLayer(&layer))
		Aeron_RequestFatalRendererError("classic framebuffer layer submission");
}

void TieClassicLayer_Shutdown(void) {
	TieClassicLayer_SetSuppressed(false);
	memset(&s_classic_layer, 0, sizeof s_classic_layer);
}
