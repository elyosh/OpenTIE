#include "tie/render_clip_tie98.h"

#include "tie/logbuf2.h"
#include "tie/tie.h"
#include "tie/transfm2.h"

#include <math.h>

int g_clipIdxA[32];
int g_clipIdxB[32];
int g_clipCountA;
int g_clipCountB;
int g_clipVertCursor;
int g_clipOccurred;
float g_invProjScale;

/* RECOVERY HELPER: removes the intersection-field interpolation repeated by
 * all five recovered TIE98 polygon clipping functions. */
static void RenderClip_WriteIntersection(ProjVertexTIE98* destination, const ProjVertexTIE98* previous,
										 const ProjVertexTIE98* current, float t, int base_is_current,
										 int subtract_delta) {
	const float delta_sx = current->sx - previous->sx;
	const float delta_sy = current->sy - previous->sy;
	const float delta_w = current->w - previous->w;
	const float delta_light = current->lightIntensity - previous->lightIntensity;
	const float delta_tu = current->tu - previous->tu;
	const float delta_tv = current->tv - previous->tv;
	const ProjVertexTIE98* base = base_is_current ? current : previous;
	const float direction = subtract_delta ? -t : t;

	destination->sx = base->sx + delta_sx * direction;
	destination->sy = base->sy + delta_sy * direction;
	destination->w = base->w + delta_w * direction;
	destination->lightIntensity = base->lightIntensity + delta_light * direction;

	if (fabsf(delta_w) < 0.00001f) {
		destination->tu = base->tu + delta_tu * direction;
		destination->tv = base->tv + delta_tv * direction;
	} else if (base_is_current) {
		const float current_inv_w = (float)perspFactor / current->w;
		const float previous_inv_w = (float)perspFactor / previous->w;
		const float uv_t =
			((float)perspFactor / destination->w - current_inv_w) / (previous_inv_w - current_inv_w);
		destination->tu = current->tu - delta_tu * uv_t;
		destination->tv = current->tv - delta_tv * uv_t;
	} else {
		const float previous_inv_w = (float)perspFactor / previous->w;
		const float current_inv_w = (float)perspFactor / current->w;
		const float uv_t =
			((float)perspFactor / destination->w - previous_inv_w) / (current_inv_w - previous_inv_w);
		destination->tu = previous->tu + delta_tu * uv_t;
		destination->tv = previous->tv + delta_tv * uv_t;
	}
}

// FUNCTION: TIE98 0x429060
void RenderClip_ClipPolyTop(int prev_vert, int cur_vert, ProjVertexTIE98* vert_buf) {
	ProjVertexTIE98* previous = &vert_buf[prev_vert];
	ProjVertexTIE98* current = &vert_buf[cur_vert];
	const float previous_y = previous->sy;
	const float current_y = current->sy;
	const float delta_y = current_y - previous_y;
	int output;

	if (previous_y < 0.0f) {
		if (current_y < 0.0f)
			return;
		g_clipOccurred = 1;
		output = g_clipVertCursor++;
		if (-previous_y >= current_y)
			RenderClip_WriteIntersection(&vert_buf[output], previous, current, current_y / delta_y, 1, 1);
		else
			RenderClip_WriteIntersection(&vert_buf[output], previous, current, -previous_y / delta_y, 0, 0);
		vert_buf[output].sy = 0.0f;
		g_clipIdxB[g_clipCountB++] = output;
		g_clipIdxB[g_clipCountB++] = cur_vert;
		return;
	}
	if (current_y < 0.0f) {
		g_clipOccurred = 1;
		output = g_clipVertCursor++;
		if (previous_y >= -current_y)
			RenderClip_WriteIntersection(&vert_buf[output], previous, current, -current_y / delta_y, 1, 0);
		else
			RenderClip_WriteIntersection(&vert_buf[output], previous, current, previous_y / delta_y, 0, 1);
		vert_buf[output].sy = 0.0f;
		g_clipIdxB[g_clipCountB++] = output;
		return;
	}
	g_clipIdxB[g_clipCountB++] = cur_vert;
}

// FUNCTION: TIE98 0x429670
void RenderClip_ClipPolyBottom(int prev_vert, int cur_vert, ProjVertexTIE98* vert_buf) {
	ProjVertexTIE98* previous = &vert_buf[prev_vert];
	ProjVertexTIE98* current = &vert_buf[cur_vert];
	const float previous_y = previous->sy;
	const float current_y = current->sy;
	const float max_y = (float)pixelsdeepmin1;
	const float delta_y = current_y - previous_y;
	int output;

	if (previous_y > max_y) {
		if (current_y > max_y)
			return;
		g_clipOccurred = 1;
		output = g_clipVertCursor++;
		if (previous_y - max_y >= max_y - current_y)
			RenderClip_WriteIntersection(&vert_buf[output], previous, current, (max_y - current_y) / delta_y,
										 1, 0);
		else
			RenderClip_WriteIntersection(&vert_buf[output], previous, current, (previous_y - max_y) / delta_y,
										 0, 1);
		vert_buf[output].sy = max_y;
		g_clipIdxA[g_clipCountA++] = output;
		g_clipIdxA[g_clipCountA++] = cur_vert;
		return;
	}
	if (current_y > max_y) {
		g_clipOccurred = 1;
		output = g_clipVertCursor++;
		if (max_y - previous_y >= current_y - max_y)
			RenderClip_WriteIntersection(&vert_buf[output], previous, current, (current_y - max_y) / delta_y,
										 1, 1);
		else
			RenderClip_WriteIntersection(&vert_buf[output], previous, current, (max_y - previous_y) / delta_y,
										 0, 0);
		vert_buf[output].sy = max_y;
		g_clipIdxA[g_clipCountA++] = output;
		return;
	}
	g_clipIdxA[g_clipCountA++] = cur_vert;
}

// FUNCTION: TIE98 0x429CD0
void RenderClip_ClipPolyLeft(int prev_vert, int cur_vert, ProjVertexTIE98* vert_buf) {
	ProjVertexTIE98* previous = &vert_buf[prev_vert];
	ProjVertexTIE98* current = &vert_buf[cur_vert];
	const float previous_x = previous->sx;
	const float current_x = current->sx;
	const float delta_x = current_x - previous_x;
	int output;

	if (previous_x < 0.0f) {
		if (current_x < 0.0f)
			return;
		g_clipOccurred = 1;
		output = g_clipVertCursor++;
		if (-previous_x >= current_x)
			RenderClip_WriteIntersection(&vert_buf[output], previous, current, current_x / delta_x, 1, 1);
		else
			RenderClip_WriteIntersection(&vert_buf[output], previous, current, -previous_x / delta_x, 0, 0);
		vert_buf[output].sx = 0.0f;
		g_clipIdxB[g_clipCountB++] = output;
		g_clipIdxB[g_clipCountB++] = cur_vert;
		return;
	}
	if (current_x < 0.0f) {
		g_clipOccurred = 1;
		output = g_clipVertCursor++;
		if (previous_x >= -current_x)
			RenderClip_WriteIntersection(&vert_buf[output], previous, current, -current_x / delta_x, 1, 0);
		else
			RenderClip_WriteIntersection(&vert_buf[output], previous, current, previous_x / delta_x, 0, 1);
		vert_buf[output].sx = 0.0f;
		g_clipIdxB[g_clipCountB++] = output;
		return;
	}
	g_clipIdxB[g_clipCountB++] = cur_vert;
}

// FUNCTION: TIE98 0x42A2D0
void RenderClip_ClipPolyRight(int prev_vert, int cur_vert, ProjVertexTIE98* vert_buf) {
	ProjVertexTIE98* previous = &vert_buf[prev_vert];
	ProjVertexTIE98* current = &vert_buf[cur_vert];
	const float previous_x = previous->sx;
	const float current_x = current->sx;
	const float max_x = (float)pixelswide;
	const float delta_x = current_x - previous_x;
	int output;

	if (previous_x > max_x) {
		if (current_x > max_x)
			return;
		g_clipOccurred = 1;
		output = g_clipVertCursor++;
		if (previous_x - max_x >= max_x - current_x)
			RenderClip_WriteIntersection(&vert_buf[output], previous, current, (max_x - current_x) / delta_x,
										 1, 0);
		else
			RenderClip_WriteIntersection(&vert_buf[output], previous, current, (previous_x - max_x) / delta_x,
										 0, 1);
		vert_buf[output].sx = max_x;
		g_clipIdxA[g_clipCountA++] = output;
		g_clipIdxA[g_clipCountA++] = cur_vert;
		return;
	}
	if (current_x > max_x) {
		g_clipOccurred = 1;
		output = g_clipVertCursor++;
		if (max_x - previous_x >= current_x - max_x)
			RenderClip_WriteIntersection(&vert_buf[output], previous, current, (current_x - max_x) / delta_x,
										 1, 1);
		else
			RenderClip_WriteIntersection(&vert_buf[output], previous, current, (max_x - previous_x) / delta_x,
										 0, 0);
		vert_buf[output].sx = max_x;
		g_clipIdxA[g_clipCountA++] = output;
		return;
	}
	g_clipIdxA[g_clipCountA++] = cur_vert;
}

// FUNCTION: TIE98 0x42A930
void RenderClip_ClipPolyNear(int prev_vert, int cur_vert, ProjVertexTIE98* vert_buf) {
	ProjVertexTIE98* previous = &vert_buf[prev_vert];
	ProjVertexTIE98* current = &vert_buf[cur_vert];
	float previous_w = previous->w;
	float current_w = current->w;
	int output;

	if (previous_w < 0.0f) {
		if (current_w < 0.0f)
			return;
		g_clipOccurred = 1;
		output = g_clipVertCursor++;
		const float current_scale = (float)perspFactor / current_w;
		const float current_x = (current->sx - (float)halfpixelswide) * current_scale * g_invProjScale;
		const float current_y =
			(current->sy - (float)(transfm2_screenyoffset + halfpixelsdeep)) * current_scale * g_invProjScale;
		const float t = previous_w / (current_scale - previous_w - 1.0f);
		vert_buf[output].sx = previous->sx - (current_x - previous->sx) * t;
		vert_buf[output].sy = previous->sy - (current_y - previous->sy) * t;
		vert_buf[output].lightIntensity =
			previous->lightIntensity - (current->lightIntensity - previous->lightIntensity) * t;
		vert_buf[output].tu = previous->tu - (current->tu - previous->tu) * t;
		vert_buf[output].tv = previous->tv - (current->tv - previous->tv) * t;
	} else if (current_w < 0.0f) {
		g_clipOccurred = 1;
		output = g_clipVertCursor++;
		const float previous_scale = (float)perspFactor / previous_w;
		const float previous_x = (previous->sx - (float)halfpixelswide) * previous_scale * g_invProjScale;
		const float previous_y = (previous->sy - (float)(transfm2_screenyoffset + halfpixelsdeep)) *
								 previous_scale * g_invProjScale;
		const float t = current_w / (current_w - previous_scale + 1.0f);
		vert_buf[output].sx = current->sx - (current->sx - previous_x) * t;
		vert_buf[output].sy = current->sy - (current->sy - previous_y) * t;
		vert_buf[output].lightIntensity =
			current->lightIntensity - (current->lightIntensity - previous->lightIntensity) * t;
		vert_buf[output].tu = current->tu - (current->tu - previous->tu) * t;
		vert_buf[output].tv = current->tv - (current->tv - previous->tv) * t;
	} else {
		g_clipIdxA[g_clipCountA++] = cur_vert;
		return;
	}

	vert_buf[output].w = (float)perspFactor;
	vert_buf[output].sx = (float)halfpixelswide + (float)perspFactor * vert_buf[output].sx;
	vert_buf[output].sy =
		(float)(transfm2_screenyoffset + halfpixelsdeep) + (float)perspFactor * vert_buf[output].sy;
	g_clipIdxA[g_clipCountA++] = output;
	if (previous_w < 0.0f)
		g_clipIdxA[g_clipCountA++] = cur_vert;
}
