#include "tie_runtime/presentation/presentation.h"

#include <limits.h>
#include <string.h>

#define TIE_PRESENTATION_RESIZE_SETTLE_US 150000u

typedef struct TiePresentationState {
	int initialized;
	TieAspectRatio modern_aspect;
	TiePresentationLayout layout;
	int observed_pixel_width;
	int observed_pixel_height;
	uint64_t observed_stable_us;
	TiePresentationChange pending_changes;
} TiePresentationState;

static TiePresentationState g_presentation;

static int TiePresentation_GreatestCommonDivisor(int a, int b) {
	while (b != 0) {
		const int remainder = a % b;
		a = b;
		b = remainder;
	}
	return a;
}

static int TiePresentation_CalculateRenderWidth(int height, TieAspectRatio aspect, int* out_width) {
	const int64_t width = ((int64_t)height * aspect.width + aspect.height / 2) / aspect.height;
	if (!out_width || height <= 0 || width <= 0 || width > INT_MAX)
		return 0;
	*out_width = (int)width;
	return 1;
}

static int TiePresentation_ComputeFullHeightRect(int bounds_width, int bounds_height, TieAspectRatio aspect,
												 AeronRectI* out) {
	const int64_t width = ((int64_t)bounds_height * aspect.width + aspect.height / 2) / aspect.height;
	if (!out || bounds_width <= 0 || bounds_height <= 0 || aspect.width <= 0 || aspect.height <= 0 ||
		width <= 0 || width > INT_MAX)
		return 0;
	*out = (AeronRectI) { (bounds_width - (int)width) / 2, 0, (int)width, bounds_height };
	return 1;
}

static int TiePresentation_UpdateLogicalLayout(int window_width, int window_height, int force) {
	TiePresentationLayout* layout = &g_presentation.layout;
	AeronRectI modern;
	AeronRectI classic;
	int64_t logical_width64;
	int logical_width;

	if (window_width <= 0 || window_height <= 0)
		return 0;
	logical_width64 =
		((int64_t)TIE_PRESENTATION_LOGICAL_HEIGHT * window_width + window_height / 2) / window_height;
	if (logical_width64 <= 0 || logical_width64 > INT_MAX)
		return 0;
	logical_width = (int)logical_width64;
	if (!TiePresentation_ComputeFullHeightRect(logical_width, TIE_PRESENTATION_LOGICAL_HEIGHT,
											   g_presentation.modern_aspect, &modern) ||
		!TiePresentation_ComputeFullHeightRect(logical_width, TIE_PRESENTATION_LOGICAL_HEIGHT,
											   (TieAspectRatio) { 4, 3 }, &classic)) {
		return 0;
	}
	if (!force && layout->frame.width == logical_width &&
		layout->frame.height == TIE_PRESENTATION_LOGICAL_HEIGHT) {
		return 0;
	}
	if (!Aeron_SetLogicalSize(logical_width, TIE_PRESENTATION_LOGICAL_HEIGHT)) {
		return 0;
	}
	layout->frame = (AeronRectI) { 0, 0, logical_width, TIE_PRESENTATION_LOGICAL_HEIGHT };
	layout->modern = modern;
	layout->classic = classic;
	layout->split_scissor = (AeronRectI) {
		logical_width / 2,
		0,
		logical_width - logical_width / 2,
		TIE_PRESENTATION_LOGICAL_HEIGHT,
	};
	return 1;
}

int TiePresentation_Init(TieAspectRatio modern_aspect) {
	int divisor;
	int window_width;
	int window_height;
	int pixel_width;
	int pixel_height;
	int render_width;

	if (g_presentation.initialized || modern_aspect.width <= 0 || modern_aspect.height <= 0) {
		return 0;
	}
	divisor = TiePresentation_GreatestCommonDivisor(modern_aspect.width, modern_aspect.height);
	modern_aspect.width /= divisor;
	modern_aspect.height /= divisor;
	memset(&g_presentation, 0, sizeof g_presentation);
	g_presentation.modern_aspect = modern_aspect;

	if (!Aeron_SetWindowAspectRatio(modern_aspect.width, modern_aspect.height)) {
		Aeron_LogWarn("tie.presentation", "window aspect constraint %d:%d was not applied",
					  modern_aspect.width, modern_aspect.height);
	}
	if (!Aeron_ResizeWindowToAspect(modern_aspect.width, modern_aspect.height)) {
		Aeron_LogWarn("tie.presentation", "initial window resize to %d:%d was not applied",
					  modern_aspect.width, modern_aspect.height);
	}
	if (!Aeron_GetWindowSize(&window_width, &window_height) ||
		!TiePresentation_UpdateLogicalLayout(window_width, window_height, 1) ||
		!Aeron_GetPresentationPixelSize(&pixel_width, &pixel_height) ||
		!TiePresentation_CalculateRenderWidth(pixel_height, modern_aspect, &render_width)) {
		memset(&g_presentation, 0, sizeof g_presentation);
		return 0;
	}
	g_presentation.observed_pixel_width = pixel_width;
	g_presentation.observed_pixel_height = pixel_height;
	g_presentation.layout.render_width = render_width;
	g_presentation.layout.render_height = pixel_height;
	g_presentation.layout.render_generation = 1;
	g_presentation.initialized = 1;
	Aeron_LogInfo("tie.presentation", "aspect %d:%d, logical %dx%d, render %dx%d", modern_aspect.width,
				  modern_aspect.height, g_presentation.layout.frame.width, g_presentation.layout.frame.height,
				  render_width, pixel_height);
	return 1;
}

int TiePresentation_SetModernAspect(TieAspectRatio modern_aspect) {
	int window_width;
	int window_height;
	int pixel_width;
	int pixel_height;
	int render_width;
	if (!g_presentation.initialized || modern_aspect.width <= 0 || modern_aspect.height <= 0)
		return 0;
	const int divisor = TiePresentation_GreatestCommonDivisor(modern_aspect.width, modern_aspect.height);
	modern_aspect.width /= divisor;
	modern_aspect.height /= divisor;
	if (modern_aspect.width == g_presentation.modern_aspect.width &&
		modern_aspect.height == g_presentation.modern_aspect.height)
		return 1;
	g_presentation.modern_aspect = modern_aspect;
	if (!Aeron_SetWindowAspectRatio(modern_aspect.width, modern_aspect.height))
		Aeron_LogWarn("tie.presentation", "window aspect constraint %d:%d was not applied",
					  modern_aspect.width, modern_aspect.height);
	if (!Aeron_ResizeWindowToAspect(modern_aspect.width, modern_aspect.height))
		Aeron_LogWarn("tie.presentation", "window resize to %d:%d was not applied", modern_aspect.width,
					  modern_aspect.height);
	if (!Aeron_GetWindowSize(&window_width, &window_height) ||
		!TiePresentation_UpdateLogicalLayout(window_width, window_height, 1) ||
		!Aeron_GetPresentationPixelSize(&pixel_width, &pixel_height) ||
		!TiePresentation_CalculateRenderWidth(pixel_height, modern_aspect, &render_width))
		return 0;
	g_presentation.observed_pixel_width = pixel_width;
	g_presentation.observed_pixel_height = pixel_height;
	g_presentation.observed_stable_us = 0;
	g_presentation.layout.render_width = render_width;
	g_presentation.layout.render_height = pixel_height;
	g_presentation.layout.render_generation++;
	g_presentation.pending_changes =
		(TiePresentationChange)(TIE_PRESENTATION_CHANGE_LAYOUT | TIE_PRESENTATION_CHANGE_RENDER_SIZE);
	Aeron_LogInfo("tie.presentation", "aspect %d:%d, logical %dx%d, render %dx%d", modern_aspect.width,
				  modern_aspect.height, g_presentation.layout.frame.width, g_presentation.layout.frame.height,
				  render_width, pixel_height);
	return 1;
}

TiePresentationChange TiePresentation_BeginFrame(const AeronInputSnapshot* input, int32_t delta_us) {
	TiePresentationChange change = g_presentation.pending_changes;
	g_presentation.pending_changes = TIE_PRESENTATION_CHANGE_NONE;
	int pixel_width;
	int pixel_height;
	int render_width;

	if (!g_presentation.initialized)
		return change;
	if (input && TiePresentation_UpdateLogicalLayout(input->window_width, input->window_height, 0)) {
		change = (TiePresentationChange)(change | TIE_PRESENTATION_CHANGE_LAYOUT);
	}
	if (!Aeron_GetPresentationPixelSize(&pixel_width, &pixel_height))
		return change;
	if (pixel_width != g_presentation.observed_pixel_width ||
		pixel_height != g_presentation.observed_pixel_height) {
		g_presentation.observed_pixel_width = pixel_width;
		g_presentation.observed_pixel_height = pixel_height;
		g_presentation.observed_stable_us = 0;
		return change;
	}
	if (delta_us > 0 && g_presentation.observed_stable_us < TIE_PRESENTATION_RESIZE_SETTLE_US) {
		const uint64_t remaining_us = TIE_PRESENTATION_RESIZE_SETTLE_US - g_presentation.observed_stable_us;
		const uint64_t advance_us = (uint32_t)delta_us;
		g_presentation.observed_stable_us += advance_us < remaining_us ? advance_us : remaining_us;
	}
	if (g_presentation.observed_stable_us < TIE_PRESENTATION_RESIZE_SETTLE_US ||
		!TiePresentation_CalculateRenderWidth(pixel_height, g_presentation.modern_aspect, &render_width) ||
		(render_width == g_presentation.layout.render_width &&
		 pixel_height == g_presentation.layout.render_height)) {
		return change;
	}
	g_presentation.layout.render_width = render_width;
	g_presentation.layout.render_height = pixel_height;
	g_presentation.layout.render_generation++;
	change = (TiePresentationChange)(change | TIE_PRESENTATION_CHANGE_RENDER_SIZE);
	Aeron_LogInfo("tie.presentation", "physical render size: %dx%d", render_width, pixel_height);
	return change;
}

const TiePresentationLayout* TiePresentation_Layout(void) {
	return g_presentation.initialized ? &g_presentation.layout : NULL;
}

TieAspectRatio TiePresentation_ModernAspect(void) { return g_presentation.modern_aspect; }

void TiePresentation_FromClassic(float classic_x, float classic_y, int classic_width, int classic_height,
								 float* logical_x, float* logical_y) {
	const AeronRectI* rect = &g_presentation.layout.classic;
	if (!g_presentation.initialized || classic_width <= 0 || classic_height <= 0) {
		return;
	}
	if (logical_x) {
		*logical_x = (float)rect->x + classic_x * (float)rect->width / (float)classic_width;
	}
	if (logical_y) {
		*logical_y = (float)rect->y + classic_y * (float)rect->height / (float)classic_height;
	}
}

void TiePresentation_Shutdown(void) { memset(&g_presentation, 0, sizeof g_presentation); }
