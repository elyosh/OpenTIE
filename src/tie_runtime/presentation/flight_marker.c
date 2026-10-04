/* OpenXvT's hollow virtual-stick marker, composited above every flight
 * renderer from a texture uploaded once at startup. */

#include "tie_runtime/presentation/flight_marker.h"

#include <math.h>
#include <stdint.h>

#include "aeron/aeron.h"
#include "tie_runtime/input/mouse_flight.h"
#include "tie_runtime/presentation/presentation.h"
#include "tie_runtime/runtime/flight_screen.h"
#include "tie_runtime/snapshot/snapshot_types.h"

enum {
	TIE_FLIGHT_MARKER_TEXELS = 4,
	/* OpenXW draws the four texels across four units of a 480-line frame. */
	TIE_FLIGHT_MARKER_SIZE = (TIE_PRESENTATION_LOGICAL_HEIGHT * TIE_FLIGHT_MARKER_TEXELS + 240) / 480,
};

static const uint8_t marker_pixels[TIE_FLIGHT_MARKER_TEXELS][TIE_FLIGHT_MARKER_TEXELS][4] = {
	{ { 0, 255, 0, 255 }, { 0, 255, 0, 255 }, { 0, 255, 0, 255 }, { 0, 255, 0, 255 } },
	{ { 0, 255, 0, 255 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 255, 0, 255 } },
	{ { 0, 255, 0, 255 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 255, 0, 255 } },
	{ { 0, 255, 0, 255 }, { 0, 255, 0, 255 }, { 0, 255, 0, 255 }, { 0, 255, 0, 255 } },
};

static AeronTexture* marker_texture;

bool TieFlightMarker_Init(AeronCommandBuffer* startup_cmd) {
	if (marker_texture)
		return true;
	if (!startup_cmd)
		return false;
	marker_texture = Aeron_CreateTexture(&(AeronTextureDesc) {
		.width = TIE_FLIGHT_MARKER_TEXELS,
		.height = TIE_FLIGHT_MARKER_TEXELS,
		.mip_count = 1,
		.format = AERON_TEXTURE_FORMAT_RGBA8_SRGB,
		.usage = AERON_TEXTURE_USAGE_SAMPLED | AERON_TEXTURE_USAGE_TRANSFER_DST,
		.debug_name = "tie.flight_marker",
	});
	if (!marker_texture)
		return false;
	if (!Aeron_UploadTextureDataCmd(startup_cmd, &(AeronTextureUploadDesc) {
													 .texture = marker_texture,
													 .width = TIE_FLIGHT_MARKER_TEXELS,
													 .height = TIE_FLIGHT_MARKER_TEXELS,
													 .pixels = marker_pixels,
													 .pitch = sizeof marker_pixels[0],
													 .pixel_format = AERON_PIXEL_FORMAT_RGBA8888,
													 .color_space = AERON_COLOR_SPACE_SRGB,
												 })) {
		TieFlightMarker_Shutdown();
		return false;
	}
	return true;
}

void TieFlightMarker_Shutdown(void) {
	if (marker_texture)
		Aeron_DestroyTexture(marker_texture);
	marker_texture = NULL;
}

/* Forward cockpit views (full panel or panel hidden) and the external camera. */
static bool TieFlightMarker_ViewVisible(const TieCameraState* camera) {
	return camera->zoom_active || camera->pilotview == 0 || camera->pilotview == 19;
}

void TieFlightMarker_Submit(const TieSnapshot* snapshot) {
	int yaw;
	int pitch;
	if (!marker_texture || !snapshot || snapshot->scene_kind != TIE_SCENE_FLIGHT ||
		snapshot->flight_screen != TIE_FLIGHT_SCREEN_NORMAL || !snapshot->classic_w || !snapshot->classic_h ||
		!TieFlightMarker_ViewVisible(&snapshot->camera) || !TieMouseFlight_GetHudMarker(&yaw, &pitch))
		return;
	const TiePresentationLayout* layout = TiePresentation_Layout();
	const TieCameraState* camera = &snapshot->camera;
	if (!layout || layout->classic.height <= 0 || camera->viewport_frac_h <= 0.0f)
		return;

	/* The engine projects the forward axis to the aperture center plus the
	 * per-view screen-Y offset; the stick spans a sixth of the aperture. */
	const float center_x = (camera->viewport_frac_x + camera->viewport_frac_w * 0.5f) * snapshot->classic_w;
	const float center_y =
		(camera->viewport_frac_y + camera->viewport_frac_h * 0.5f * (1.0f + camera->screen_y_offset_ndc)) *
		snapshot->classic_h;
	float x;
	float y;
	TiePresentation_FromClassic(center_x, center_y, snapshot->classic_w, snapshot->classic_h, &x, &y);
	const float range = camera->viewport_frac_h * (float)layout->classic.height / 6.0f;
	x += (float)yaw * range / 127.0f;
	y -= (float)pitch * range / 127.0f;

	AeronTextureLayerDesc layer = {
		.texture = marker_texture,
		.logical_rect = { (int)lroundf(x) - TIE_FLIGHT_MARKER_SIZE / 2,
						  (int)lroundf(y) - TIE_FLIGHT_MARKER_SIZE / 2, TIE_FLIGHT_MARKER_SIZE,
						  TIE_FLIGHT_MARKER_SIZE },
		.blend_mode = AERON_LAYER_BLEND_PREMULTIPLIED,
		.color_space = AERON_COLOR_SPACE_SRGB,
		.scissor = layout->classic,
	};
	if (!Aeron_SubmitTextureLayer(&layer))
		Aeron_RequestFatalRendererError("flight marker layer submission");
}
