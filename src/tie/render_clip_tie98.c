#include "tie/render_clip_tie98.h"
#include "tie/logbuf2.h"
#include "tie/tie.h"
#include "tie/transfm2.h"

#include <math.h>

// GLOBAL: TIE98 0x570250
int g_clipIdxA[32];
// GLOBAL: TIE98 0x5601D0
int g_clipIdxB[32];
// GLOBAL: TIE98 0x5802D8
int g_clipCountA;
// GLOBAL: TIE98 0x5802F8
int g_clipCountB;
// GLOBAL: TIE98 0x5802FC
int g_clipVertCursor;
// GLOBAL: TIE98 0x5FD2A0
float g_invProjScale;

// FUNCTION: TIE98 0x429060
void RenderClip_ClipPolyTop(int prev_vert, int cur_vert, ProjVertexTIE98* vert_buf) {
	ProjVertexTIE98* previous = &vert_buf[prev_vert];
	ProjVertexTIE98* current = &vert_buf[cur_vert];
	const float previous_y = previous->sy;
	const float current_y = current->sy;
	const float delta_y = current_y - previous_y;
	float delta_sx;
	float delta_sy;
	float delta_w;
	float delta_light;
	float delta_tu;
	float delta_tv;
	float t;
	float uv_t;
	float previous_inv_w;
	float current_inv_w;
	ProjVertexTIE98* out;
	int output;

	if (previous_y < 0.0f) {
		if (current_y < 0.0f)
			return;
		output = g_clipVertCursor++;
		out = &vert_buf[output];
		delta_sx = current->sx - previous->sx;
		delta_sy = current->sy - previous->sy;
		delta_w = current->w - previous->w;
		delta_light = current->lightIntensity - previous->lightIntensity;
		delta_tu = current->tu - previous->tu;
		delta_tv = current->tv - previous->tv;
		if (-previous_y >= current_y) {
			t = current_y / delta_y;
			out->sx = current->sx - delta_sx * t;
			out->sy = current->sy - delta_sy * t;
			out->w = current->w - delta_w * t;
			out->lightIntensity = current->lightIntensity - delta_light * t;
			if (fabsf(delta_w) < 0.00001f) {
				out->tu = current->tu - delta_tu * t;
				out->tv = current->tv - delta_tv * t;
			} else {
				current_inv_w = (float)perspFactor / current->w;
				previous_inv_w = (float)perspFactor / previous->w;
				uv_t = ((float)perspFactor / out->w - current_inv_w) / (previous_inv_w - current_inv_w);
				out->tu = current->tu - delta_tu * uv_t;
				out->tv = current->tv - delta_tv * uv_t;
			}
		} else {
			t = -previous_y / delta_y;
			out->sx = previous->sx + delta_sx * t;
			out->sy = previous->sy + delta_sy * t;
			out->w = previous->w + delta_w * t;
			out->lightIntensity = previous->lightIntensity + delta_light * t;
			if (fabsf(delta_w) < 0.00001f) {
				out->tu = previous->tu + delta_tu * t;
				out->tv = previous->tv + delta_tv * t;
			} else {
				previous_inv_w = (float)perspFactor / previous->w;
				current_inv_w = (float)perspFactor / current->w;
				uv_t = ((float)perspFactor / out->w - previous_inv_w) / (current_inv_w - previous_inv_w);
				out->tu = previous->tu + delta_tu * uv_t;
				out->tv = previous->tv + delta_tv * uv_t;
			}
		}
		out->sy = 0.0f;
		g_clipIdxB[g_clipCountB++] = output;
		g_clipIdxB[g_clipCountB++] = cur_vert;
		return;
	}
	if (current_y < 0.0f) {
		output = g_clipVertCursor++;
		out = &vert_buf[output];
		delta_sx = current->sx - previous->sx;
		delta_sy = current->sy - previous->sy;
		delta_w = current->w - previous->w;
		delta_light = current->lightIntensity - previous->lightIntensity;
		delta_tu = current->tu - previous->tu;
		delta_tv = current->tv - previous->tv;
		if (previous_y >= -current_y) {
			t = -current_y / delta_y;
			out->sx = current->sx + delta_sx * t;
			out->sy = current->sy + delta_sy * t;
			out->w = current->w + delta_w * t;
			out->lightIntensity = current->lightIntensity + delta_light * t;
			if (fabsf(delta_w) < 0.00001f) {
				out->tu = current->tu + delta_tu * t;
				out->tv = current->tv + delta_tv * t;
			} else {
				current_inv_w = (float)perspFactor / current->w;
				previous_inv_w = (float)perspFactor / previous->w;
				uv_t = ((float)perspFactor / out->w - current_inv_w) / (previous_inv_w - current_inv_w);
				out->tu = current->tu - delta_tu * uv_t;
				out->tv = current->tv - delta_tv * uv_t;
			}
		} else {
			t = previous_y / delta_y;
			out->sx = previous->sx - delta_sx * t;
			out->sy = previous->sy - delta_sy * t;
			out->w = previous->w - delta_w * t;
			out->lightIntensity = previous->lightIntensity - delta_light * t;
			if (fabsf(delta_w) < 0.00001f) {
				out->tu = previous->tu - delta_tu * t;
				out->tv = previous->tv - delta_tv * t;
			} else {
				previous_inv_w = (float)perspFactor / previous->w;
				current_inv_w = (float)perspFactor / current->w;
				uv_t = ((float)perspFactor / out->w - previous_inv_w) / (current_inv_w - previous_inv_w);
				out->tu = previous->tu + delta_tu * uv_t;
				out->tv = previous->tv + delta_tv * uv_t;
			}
		}
		out->sy = 0.0f;
		g_clipIdxB[g_clipCountB++] = output;
		return;
	}
	g_clipIdxB[g_clipCountB++] = cur_vert;
}

// FUNCTION: TIE98 0x429670
void RenderClip_ClipPolyBottom(int prev_vert, int cur_vert, ProjVertexTIE98* vert_buf) {
	float previous_y = vert_buf[prev_vert].sy;
	float current_y = vert_buf[cur_vert].sy;
	float max_y = (float)pixelsdeepmin1;
	float current_inv_w;
	float previous_inv_w;
	float uv_t;
	float delta_light;
	float delta_tv;
	float delta_tu;
	float t;
	float delta_w;
	float delta_sx;
	float current_tv;
	float current_tu;
	float current_light;
	float current_w;
	float current_sx;
	float previous_tv;
	float previous_tu;
	float previous_light;
	float previous_w;
	float previous_sx;
	float delta_y;
	int output;

	if (previous_y > max_y) {
		if (current_y < max_y) {
			delta_y = current_y - previous_y;
			output = g_clipVertCursor++;
			previous_sx = vert_buf[prev_vert].sx;
			previous_w = vert_buf[prev_vert].w;
			previous_light = vert_buf[prev_vert].lightIntensity;
			previous_tu = vert_buf[prev_vert].tu;
			previous_tv = vert_buf[prev_vert].tv;
			current_sx = vert_buf[cur_vert].sx;
			current_w = vert_buf[cur_vert].w;
			current_light = vert_buf[cur_vert].lightIntensity;
			current_tu = vert_buf[cur_vert].tu;
			current_tv = vert_buf[cur_vert].tv;
			delta_sx = current_sx - previous_sx;
			delta_w = current_w - previous_w;
			delta_light = current_light - previous_light;
			delta_tu = current_tu - previous_tu;
			delta_tv = current_tv - previous_tv;
			if ((previous_y - max_y) < (max_y - current_y)) {
				t = (previous_y - max_y) / delta_y;
				vert_buf[output].sx = previous_sx - delta_sx * t;
				vert_buf[output].w = previous_w - delta_w * t;
				vert_buf[output].lightIntensity = previous_light - delta_light * t;
				if (delta_w != 0.0f) {
					previous_inv_w = (float)(unsigned int)perspFactor / previous_w;
					current_inv_w = (float)(unsigned int)perspFactor / current_w;
					uv_t = ((float)(unsigned int)perspFactor / vert_buf[output].w - previous_inv_w) /
						   (current_inv_w - previous_inv_w);
					vert_buf[output].tu = previous_tu + delta_tu * uv_t;
					vert_buf[output].tv = previous_tv + delta_tv * uv_t;
				} else {
					vert_buf[output].tu = previous_tu - delta_tu * t;
					vert_buf[output].tv = previous_tv - delta_tv * t;
				}
			} else {
				t = (max_y - current_y) / delta_y;
				vert_buf[output].sx = current_sx + delta_sx * t;
				vert_buf[output].w = current_w + delta_w * t;
				vert_buf[output].lightIntensity = current_light + delta_light * t;
				if (delta_w != 0.0f) {
					current_inv_w = (float)(unsigned int)perspFactor / current_w;
					previous_inv_w = (float)(unsigned int)perspFactor / previous_w;
					uv_t = ((float)(unsigned int)perspFactor / vert_buf[output].w - current_inv_w) /
						   (previous_inv_w - current_inv_w);
					vert_buf[output].tu = current_tu - delta_tu * uv_t;
					vert_buf[output].tv = current_tv - delta_tv * uv_t;
				} else {
					vert_buf[output].tu = current_tu + delta_tu * t;
					vert_buf[output].tv = current_tv + delta_tv * t;
				}
			}
			vert_buf[output].sy = (float)pixelsdeepmin1;
			g_clipIdxA[g_clipCountA++] = output;
			g_clipIdxA[g_clipCountA++] = cur_vert;
		}
	} else if (current_y > max_y) {
		delta_y = current_y - previous_y;
		output = g_clipVertCursor++;
		previous_sx = vert_buf[prev_vert].sx;
		previous_w = vert_buf[prev_vert].w;
		previous_light = vert_buf[prev_vert].lightIntensity;
		previous_tu = vert_buf[prev_vert].tu;
		previous_tv = vert_buf[prev_vert].tv;
		current_sx = vert_buf[cur_vert].sx;
		current_w = vert_buf[cur_vert].w;
		current_light = vert_buf[cur_vert].lightIntensity;
		current_tu = vert_buf[cur_vert].tu;
		current_tv = vert_buf[cur_vert].tv;
		delta_sx = current_sx - previous_sx;
		delta_w = current_w - previous_w;
		delta_light = current_light - previous_light;
		delta_tu = current_tu - previous_tu;
		delta_tv = current_tv - previous_tv;
		if ((max_y - previous_y) < (current_y - max_y)) {
			t = (max_y - previous_y) / delta_y;
			vert_buf[output].sx = previous_sx + delta_sx * t;
			vert_buf[output].w = previous_w + delta_w * t;
			vert_buf[output].lightIntensity = previous_light + delta_light * t;
			if (delta_w != 0.0f) {
				previous_inv_w = (float)(unsigned int)perspFactor / previous_w;
				current_inv_w = (float)(unsigned int)perspFactor / current_w;
				uv_t = ((float)(unsigned int)perspFactor / vert_buf[output].w - previous_inv_w) /
					   (current_inv_w - previous_inv_w);
				vert_buf[output].tu = previous_tu + delta_tu * uv_t;
				vert_buf[output].tv = previous_tv + delta_tv * uv_t;
			} else {
				vert_buf[output].tu = previous_tu + delta_tu * t;
				vert_buf[output].tv = previous_tv + delta_tv * t;
			}
		} else {
			t = (current_y - max_y) / delta_y;
			vert_buf[output].sx = current_sx - delta_sx * t;
			vert_buf[output].w = current_w - delta_w * t;
			vert_buf[output].lightIntensity = current_light - delta_light * t;
			if (delta_w != 0.0f) {
				current_inv_w = (float)(unsigned int)perspFactor / current_w;
				previous_inv_w = (float)(unsigned int)perspFactor / previous_w;
				uv_t = ((float)(unsigned int)perspFactor / vert_buf[output].w - current_inv_w) /
					   (previous_inv_w - current_inv_w);
				vert_buf[output].tu = current_tu - delta_tu * uv_t;
				vert_buf[output].tv = current_tv - delta_tv * uv_t;
			} else {
				vert_buf[output].tu = current_tu - delta_tu * t;
				vert_buf[output].tv = current_tv - delta_tv * t;
			}
		}
		vert_buf[output].sy = (float)pixelsdeepmin1;
		g_clipIdxA[g_clipCountA++] = output;
	} else {
		g_clipIdxA[g_clipCountA++] = cur_vert;
	}
}

// FUNCTION: TIE98 0x429CD0
void RenderClip_ClipPolyLeft(int prev_vert, int cur_vert, ProjVertexTIE98* vert_buf) {
	ProjVertexTIE98* previous = &vert_buf[prev_vert];
	ProjVertexTIE98* current = &vert_buf[cur_vert];
	const float previous_x = previous->sx;
	const float current_x = current->sx;
	const float delta_x = current_x - previous_x;
	float delta_sx;
	float delta_sy;
	float delta_w;
	float delta_light;
	float delta_tu;
	float delta_tv;
	float t;
	float uv_t;
	float previous_inv_w;
	float current_inv_w;
	ProjVertexTIE98* out;
	int output;

	if (previous_x < 0.0f) {
		if (current_x < 0.0f)
			return;
		output = g_clipVertCursor++;
		out = &vert_buf[output];
		delta_sx = current->sx - previous->sx;
		delta_sy = current->sy - previous->sy;
		delta_w = current->w - previous->w;
		delta_light = current->lightIntensity - previous->lightIntensity;
		delta_tu = current->tu - previous->tu;
		delta_tv = current->tv - previous->tv;
		if (-previous_x >= current_x) {
			t = current_x / delta_x;
			out->sx = current->sx - delta_sx * t;
			out->sy = current->sy - delta_sy * t;
			out->w = current->w - delta_w * t;
			out->lightIntensity = current->lightIntensity - delta_light * t;
			if (fabsf(delta_w) < 0.00001f) {
				out->tu = current->tu - delta_tu * t;
				out->tv = current->tv - delta_tv * t;
			} else {
				current_inv_w = (float)perspFactor / current->w;
				previous_inv_w = (float)perspFactor / previous->w;
				uv_t = ((float)perspFactor / out->w - current_inv_w) / (previous_inv_w - current_inv_w);
				out->tu = current->tu - delta_tu * uv_t;
				out->tv = current->tv - delta_tv * uv_t;
			}
		} else {
			t = -previous_x / delta_x;
			out->sx = previous->sx + delta_sx * t;
			out->sy = previous->sy + delta_sy * t;
			out->w = previous->w + delta_w * t;
			out->lightIntensity = previous->lightIntensity + delta_light * t;
			if (fabsf(delta_w) < 0.00001f) {
				out->tu = previous->tu + delta_tu * t;
				out->tv = previous->tv + delta_tv * t;
			} else {
				previous_inv_w = (float)perspFactor / previous->w;
				current_inv_w = (float)perspFactor / current->w;
				uv_t = ((float)perspFactor / out->w - previous_inv_w) / (current_inv_w - previous_inv_w);
				out->tu = previous->tu + delta_tu * uv_t;
				out->tv = previous->tv + delta_tv * uv_t;
			}
		}
		out->sx = 0.0f;
		g_clipIdxB[g_clipCountB++] = output;
		g_clipIdxB[g_clipCountB++] = cur_vert;
		return;
	}
	if (current_x < 0.0f) {
		output = g_clipVertCursor++;
		out = &vert_buf[output];
		delta_sx = current->sx - previous->sx;
		delta_sy = current->sy - previous->sy;
		delta_w = current->w - previous->w;
		delta_light = current->lightIntensity - previous->lightIntensity;
		delta_tu = current->tu - previous->tu;
		delta_tv = current->tv - previous->tv;
		if (previous_x >= -current_x) {
			t = -current_x / delta_x;
			out->sx = current->sx + delta_sx * t;
			out->sy = current->sy + delta_sy * t;
			out->w = current->w + delta_w * t;
			out->lightIntensity = current->lightIntensity + delta_light * t;
			if (fabsf(delta_w) < 0.00001f) {
				out->tu = current->tu + delta_tu * t;
				out->tv = current->tv + delta_tv * t;
			} else {
				current_inv_w = (float)perspFactor / current->w;
				previous_inv_w = (float)perspFactor / previous->w;
				uv_t = ((float)perspFactor / out->w - current_inv_w) / (previous_inv_w - current_inv_w);
				out->tu = current->tu - delta_tu * uv_t;
				out->tv = current->tv - delta_tv * uv_t;
			}
		} else {
			t = previous_x / delta_x;
			out->sx = previous->sx - delta_sx * t;
			out->sy = previous->sy - delta_sy * t;
			out->w = previous->w - delta_w * t;
			out->lightIntensity = previous->lightIntensity - delta_light * t;
			if (fabsf(delta_w) < 0.00001f) {
				out->tu = previous->tu - delta_tu * t;
				out->tv = previous->tv - delta_tv * t;
			} else {
				previous_inv_w = (float)perspFactor / previous->w;
				current_inv_w = (float)perspFactor / current->w;
				uv_t = ((float)perspFactor / out->w - previous_inv_w) / (current_inv_w - previous_inv_w);
				out->tu = previous->tu + delta_tu * uv_t;
				out->tv = previous->tv + delta_tv * uv_t;
			}
		}
		out->sx = 0.0f;
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
	float delta_sx;
	float delta_sy;
	float delta_w;
	float delta_light;
	float delta_tu;
	float delta_tv;
	float t;
	float uv_t;
	float previous_inv_w;
	float current_inv_w;
	ProjVertexTIE98* out;
	int output;

	if (previous_x > max_x) {
		if (current_x > max_x)
			return;
		output = g_clipVertCursor++;
		out = &vert_buf[output];
		delta_sx = current->sx - previous->sx;
		delta_sy = current->sy - previous->sy;
		delta_w = current->w - previous->w;
		delta_light = current->lightIntensity - previous->lightIntensity;
		delta_tu = current->tu - previous->tu;
		delta_tv = current->tv - previous->tv;
		if (previous_x - max_x >= max_x - current_x) {
			t = (max_x - current_x) / delta_x;
			out->sx = current->sx + delta_sx * t;
			out->sy = current->sy + delta_sy * t;
			out->w = current->w + delta_w * t;
			out->lightIntensity = current->lightIntensity + delta_light * t;
			if (fabsf(delta_w) < 0.00001f) {
				out->tu = current->tu + delta_tu * t;
				out->tv = current->tv + delta_tv * t;
			} else {
				current_inv_w = (float)perspFactor / current->w;
				previous_inv_w = (float)perspFactor / previous->w;
				uv_t = ((float)perspFactor / out->w - current_inv_w) / (previous_inv_w - current_inv_w);
				out->tu = current->tu - delta_tu * uv_t;
				out->tv = current->tv - delta_tv * uv_t;
			}
		} else {
			t = (previous_x - max_x) / delta_x;
			out->sx = previous->sx - delta_sx * t;
			out->sy = previous->sy - delta_sy * t;
			out->w = previous->w - delta_w * t;
			out->lightIntensity = previous->lightIntensity - delta_light * t;
			if (fabsf(delta_w) < 0.00001f) {
				out->tu = previous->tu - delta_tu * t;
				out->tv = previous->tv - delta_tv * t;
			} else {
				previous_inv_w = (float)perspFactor / previous->w;
				current_inv_w = (float)perspFactor / current->w;
				uv_t = ((float)perspFactor / out->w - previous_inv_w) / (current_inv_w - previous_inv_w);
				out->tu = previous->tu + delta_tu * uv_t;
				out->tv = previous->tv + delta_tv * uv_t;
			}
		}
		out->sx = max_x;
		g_clipIdxA[g_clipCountA++] = output;
		g_clipIdxA[g_clipCountA++] = cur_vert;
		return;
	}
	if (current_x > max_x) {
		output = g_clipVertCursor++;
		out = &vert_buf[output];
		delta_sx = current->sx - previous->sx;
		delta_sy = current->sy - previous->sy;
		delta_w = current->w - previous->w;
		delta_light = current->lightIntensity - previous->lightIntensity;
		delta_tu = current->tu - previous->tu;
		delta_tv = current->tv - previous->tv;
		if (max_x - previous_x >= current_x - max_x) {
			t = (current_x - max_x) / delta_x;
			out->sx = current->sx - delta_sx * t;
			out->sy = current->sy - delta_sy * t;
			out->w = current->w - delta_w * t;
			out->lightIntensity = current->lightIntensity - delta_light * t;
			if (fabsf(delta_w) < 0.00001f) {
				out->tu = current->tu - delta_tu * t;
				out->tv = current->tv - delta_tv * t;
			} else {
				current_inv_w = (float)perspFactor / current->w;
				previous_inv_w = (float)perspFactor / previous->w;
				uv_t = ((float)perspFactor / out->w - current_inv_w) / (previous_inv_w - current_inv_w);
				out->tu = current->tu - delta_tu * uv_t;
				out->tv = current->tv - delta_tv * uv_t;
			}
		} else {
			t = (max_x - previous_x) / delta_x;
			out->sx = previous->sx + delta_sx * t;
			out->sy = previous->sy + delta_sy * t;
			out->w = previous->w + delta_w * t;
			out->lightIntensity = previous->lightIntensity + delta_light * t;
			if (fabsf(delta_w) < 0.00001f) {
				out->tu = previous->tu + delta_tu * t;
				out->tv = previous->tv + delta_tv * t;
			} else {
				previous_inv_w = (float)perspFactor / previous->w;
				current_inv_w = (float)perspFactor / current->w;
				uv_t = ((float)perspFactor / out->w - previous_inv_w) / (current_inv_w - previous_inv_w);
				out->tu = previous->tu + delta_tu * uv_t;
				out->tv = previous->tv + delta_tv * uv_t;
			}
		}
		out->sx = max_x;
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
		float current_scale;
		float current_x;
		float current_y;
		float t;

		if (current_w < 0.0f)
			return;
		output = g_clipVertCursor++;
		current_scale = (float)perspFactor / current_w;
		current_x = (current->sx - (float)halfpixelswide) * current_scale * g_invProjScale;
		current_y =
			(current->sy - (float)(transfm2_screenyoffset + halfpixelsdeep)) * current_scale * g_invProjScale;
		t = previous_w / (current_scale - previous_w - 1.0f);
		vert_buf[output].sx = previous->sx - (current_x - previous->sx) * t;
		vert_buf[output].sy = previous->sy - (current_y - previous->sy) * t;
		vert_buf[output].lightIntensity =
			previous->lightIntensity - (current->lightIntensity - previous->lightIntensity) * t;
		vert_buf[output].tu = previous->tu - (current->tu - previous->tu) * t;
		vert_buf[output].tv = previous->tv - (current->tv - previous->tv) * t;
	} else if (current_w < 0.0f) {
		float previous_scale;
		float previous_x;
		float previous_y;
		float t;

		output = g_clipVertCursor++;
		previous_scale = (float)perspFactor / previous_w;
		previous_x = (previous->sx - (float)halfpixelswide) * previous_scale * g_invProjScale;
		previous_y = (previous->sy - (float)(transfm2_screenyoffset + halfpixelsdeep)) * previous_scale *
					 g_invProjScale;
		t = current_w / (current_w - previous_scale + 1.0f);
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
