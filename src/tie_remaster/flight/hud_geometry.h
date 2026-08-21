#ifndef TIE_FLIGHT_HUD_GEOMETRY_H
#define TIE_FLIGHT_HUD_GEOMETRY_H

#include <stdbool.h>

#include "tie_runtime/snapshot/snapshot_types.h"

typedef struct TieFlightHudMessageBarGeometry {
	int classic_w;
	int classic_h;
	float text_fit_x;
	float text_scale_x;
	float scale_y;
	float separator_y;
	float separator_h;
	float bar_y;
	float bar_h;
} TieFlightHudMessageBarGeometry;

/* The message bar is framebuffer UI. Its strip spans the render target,
 * while text keeps the original 4:3 horizontal presentation frame. */
static inline bool TieFlightHud_MessageBarGeometry(const TieSnapshot* snap, int target_w, int target_h,
												   TieFlightHudMessageBarGeometry* out) {
	if (!snap || !out || target_w <= 0 || target_h <= 0)
		return false;
	int classic_w = (int)snap->cockpit.classic_w;
	int classic_h = (int)snap->cockpit.classic_h;
	if (classic_w <= 0)
		classic_w = 640;
	if (classic_h <= 0)
		classic_h = 480;
	const int line_top = (int)snap->hud.msg_bar.line_top;
	const int line_bottom = (int)snap->hud.msg_bar.line_bottom;
	if (line_top <= 0 || line_bottom <= line_top || line_bottom > classic_h)
		return false;

	const float scale_y = (float)target_h / (float)classic_h;
	const float classic_display_w = (float)target_h * (4.0f / 3.0f);
	*out = (TieFlightHudMessageBarGeometry) {
		.classic_w = classic_w,
		.classic_h = classic_h,
		.text_fit_x = ((float)target_w - classic_display_w) * 0.5f,
		.text_scale_x = classic_display_w / (float)classic_w,
		.scale_y = scale_y,
		.separator_y = (float)(line_top - 1) * scale_y,
		.separator_h = scale_y,
		.bar_y = (float)line_top * scale_y,
		.bar_h = (float)(line_bottom - line_top) * scale_y,
	};
	return true;
}

#endif
