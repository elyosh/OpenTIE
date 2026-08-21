#include "tie_app/settings/video_options.h"

#include <math.h>
#include <string.h>

static struct {
	TieAppVideoConfig defaults;
	TieAppVideoConfig persisted;
	TieAppVideoConfig requested;
	TieVideoOptionsApplyFn apply;
	TieVideoOptionsPersistFn persist;
	void* user;
	bool configured;
	bool dirty;
} g_video_options;

static bool TieVideoOptions_VideoOptionsValid(const TieAppVideoConfig* options) {
	if (!options)
		return false;
	const TieVideoOptions* output = &options->output;
	return output->sdr_content_gamma >= TIE_SDR_CONTENT_GAMMA_2_2 &&
		   output->sdr_content_gamma <= TIE_SDR_CONTENT_GAMMA_SRGB &&
		   output->starfield_style >= TIE_FLIGHT_STARFIELD_STYLE_TIE95 &&
		   output->starfield_style <= TIE_FLIGHT_STARFIELD_STYLE_TIE98 &&
		   (output->paper_white_auto ||
			(isfinite(output->paper_white_nits) && output->paper_white_nits > 0.0f));
}

static void TieVideoOptions_VideoOptionsNormalize(TieAppVideoConfig* options) {
	options->fullscreen = options->fullscreen != 0;
	options->output.hdr = options->output.hdr != 0;
	options->output.paper_white_auto = options->output.paper_white_auto != 0;
	options->output.shadows_enabled = options->output.shadows_enabled != 0;
	if (options->output.ssao_quality < 0)
		options->output.ssao_quality = 0;
	if (options->output.ssao_quality > 2)
		options->output.ssao_quality = 2;
	if (options->output.shadow_atlas_size != 1024 && options->output.shadow_atlas_size != 2048 &&
		options->output.shadow_atlas_size != 4096 && options->output.shadow_atlas_size != 8192)
		options->output.shadow_atlas_size = 4096;
	if (options->output.fsr_mode < TIE_FLIGHT_TEMPORAL_OFF ||
		options->output.fsr_mode > TIE_FLIGHT_TEMPORAL_PERFORMANCE)
		options->output.fsr_mode = TIE_FLIGHT_TEMPORAL_NATIVE_AA;
	if (!isfinite(options->output.fsr_sharpness) || options->output.fsr_sharpness < 0.0f)
		options->output.fsr_sharpness = 0.0f;
	if (options->output.fsr_sharpness > 1.0f)
		options->output.fsr_sharpness = 1.0f;
	if (options->output.motion_blur_quality < 0)
		options->output.motion_blur_quality = 0;
	if (options->output.motion_blur_quality > 2)
		options->output.motion_blur_quality = 2;
	if (!isfinite(options->output.motion_blur_shutter) || options->output.motion_blur_shutter < 0.0f)
		options->output.motion_blur_shutter = 0.0f;
	if (options->output.motion_blur_shutter > 8.0f)
		options->output.motion_blur_shutter = 8.0f;
	if (options->output.msaa_samples != 2 && options->output.msaa_samples != 4 &&
		options->output.msaa_samples != 8)
		options->output.msaa_samples = 1;
	if (options->output.msaa_samples > 1)
		options->output.fsr_mode = TIE_FLIGHT_TEMPORAL_OFF;
	if (options->output.paper_white_auto)
		options->output.paper_white_nits = 0.0f;
}

static bool TieVideoOptions_VideoOptionsEqual(const TieAppVideoConfig* left, const TieAppVideoConfig* right) {
	return left->fullscreen == right->fullscreen && left->output.hdr == right->output.hdr &&
		   left->output.sdr_content_gamma == right->output.sdr_content_gamma &&
		   left->output.paper_white_auto == right->output.paper_white_auto &&
		   left->output.paper_white_nits == right->output.paper_white_nits &&
		   left->output.ssao_quality == right->output.ssao_quality &&
		   left->output.shadows_enabled == right->output.shadows_enabled &&
		   left->output.shadow_atlas_size == right->output.shadow_atlas_size &&
		   left->output.fsr_mode == right->output.fsr_mode &&
		   left->output.fsr_sharpness == right->output.fsr_sharpness &&
		   left->output.motion_blur_quality == right->output.motion_blur_quality &&
		   left->output.motion_blur_shutter == right->output.motion_blur_shutter &&
		   left->output.msaa_samples == right->output.msaa_samples &&
		   left->output.starfield_style == right->output.starfield_style;
}

bool TieVideoOptions_Configure(const TieAppVideoConfig* defaults, const TieAppVideoConfig* requested,
							   TieVideoOptionsApplyFn apply, TieVideoOptionsPersistFn persist, void* user) {
	memset(&g_video_options, 0, sizeof g_video_options);
	if (!TieVideoOptions_VideoOptionsValid(defaults) || !TieVideoOptions_VideoOptionsValid(requested) ||
		!apply || !persist)
		return false;
	g_video_options.defaults = *defaults;
	g_video_options.persisted = *requested;
	g_video_options.requested = *requested;
	TieVideoOptions_VideoOptionsNormalize(&g_video_options.defaults);
	TieVideoOptions_VideoOptionsNormalize(&g_video_options.persisted);
	TieVideoOptions_VideoOptionsNormalize(&g_video_options.requested);
	g_video_options.apply = apply;
	g_video_options.persist = persist;
	g_video_options.user = user;
	g_video_options.configured = true;
	return true;
}

void TieVideoOptions_Shutdown(void) { memset(&g_video_options, 0, sizeof g_video_options); }

void TieVideoOptions_Get(TieAppVideoConfig* out) {
	if (out && g_video_options.configured)
		*out = g_video_options.requested;
}

bool TieVideoOptions_Set(const TieAppVideoConfig* options, char* error, size_t error_capacity) {
	TieAppVideoConfig normalized;
	if (!g_video_options.configured || !TieVideoOptions_VideoOptionsValid(options))
		return false;
	normalized = *options;
	TieVideoOptions_VideoOptionsNormalize(&normalized);
	if (TieVideoOptions_VideoOptionsEqual(&normalized, &g_video_options.requested))
		return true;
	if (!g_video_options.apply(&g_video_options.requested, &normalized, g_video_options.user, error,
							   error_capacity))
		return false;
	g_video_options.requested = normalized;
	g_video_options.dirty = !TieVideoOptions_VideoOptionsEqual(&normalized, &g_video_options.persisted);
	return true;
}

bool TieVideoOptions_RestoreDefaults(char* error, size_t error_capacity) {
	return TieVideoOptions_Set(&g_video_options.defaults, error, error_capacity);
}

bool TieVideoOptions_Flush(char* error, size_t error_capacity) {
	if (!g_video_options.configured || !g_video_options.dirty)
		return true;
	if (!g_video_options.persist(&g_video_options.requested, g_video_options.user, error, error_capacity))
		return false;
	g_video_options.persisted = g_video_options.requested;
	g_video_options.dirty = false;
	return true;
}
