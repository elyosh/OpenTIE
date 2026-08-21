/* starfield_elements — see starfield_elements.h. */

#include "starfield_elements.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_HDR
#include "stb_image.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h> /* strcasecmp */

#define STARFIELD_PI 3.14159265358979323846

/* KTX2 face order: +X, -X, +Y, -Y, +Z, -Z. */
enum { FACE_PX = 0, FACE_NX, FACE_PY, FACE_NY, FACE_PZ, FACE_NZ };

void TieStarfieldElements_StarfieldElementDefault(TieStarfieldElement* e) {
	e->path[0] = '\0';
	e->yaw = 0.0f;
	e->pitch = 0.0f;
	e->size_deg = 10.0f;
	e->roll_deg = 0.0f;
	e->intensity = 1.0f;
	e->tint[0] = e->tint[1] = e->tint[2] = 1.0f;
	e->enabled = true;
}

static float TieStarfieldElements_SrgbToLinear(float s) {
	return s <= 0.04045f ? s / 12.92f : powf((s + 0.055f) / 1.055f, 2.4f);
}

static bool TieStarfieldElements_PathIsHdr(const char* p) {
	const char* dot = strrchr(p, '.');
	return dot && strcasecmp(dot, ".hdr") == 0;
}

bool TieStarfieldElements_StarfieldImageLoad(const char* path, TieStarfieldImage* out) {
	out->w = out->h = 0;
	out->rgba = NULL;
	int w = 0, h = 0, comp = 0;

	if (TieStarfieldElements_PathIsHdr(path)) {
		float* d = stbi_loadf(path, &w, &h, &comp, 4);
		if (!d)
			return false;
		out->w = w;
		out->h = h;
		out->rgba = d; /* already linear */
		return true;
	}

	uint8_t* u8 = stbi_load(path, &w, &h, &comp, 4);
	if (!u8)
		return false;
	float* d = (float*)malloc((size_t)w * h * 4u * sizeof(float));
	if (!d) {
		stbi_image_free(u8);
		return false;
	}
	for (size_t i = 0; i < (size_t)w * h; ++i) {
		d[i * 4 + 0] = TieStarfieldElements_SrgbToLinear(u8[i * 4 + 0] / 255.0f);
		d[i * 4 + 1] = TieStarfieldElements_SrgbToLinear(u8[i * 4 + 1] / 255.0f);
		d[i * 4 + 2] = TieStarfieldElements_SrgbToLinear(u8[i * 4 + 2] / 255.0f);
		d[i * 4 + 3] = u8[i * 4 + 3] / 255.0f; /* coverage stays linear */
	}
	stbi_image_free(u8);
	out->w = w;
	out->h = h;
	out->rgba = d;
	return true;
}

void TieStarfieldElements_StarfieldImageFree(TieStarfieldImage* img) {
	if (img->rgba)
		stbi_image_free(img->rgba); /* malloc/stbi both free-able */
	img->rgba = NULL;
	img->w = img->h = 0;
}

/* Cube face (px, py) → unit world direction. Matches skybox_pack.c /
 * the runtime sampler convention; top-left pixel = face_uv (0,0). */
static void TieStarfieldElements_FaceDirection(int face, int px, int py, int fs, float* dx, float* dy,
											   float* dz) {
	float uc = ((float)px + 0.5f) / (float)fs * 2.f - 1.f;
	float vc = ((float)py + 0.5f) / (float)fs * 2.f - 1.f;
	switch (face) {
		case FACE_PX:
			*dx = 1.f;
			*dy = -vc;
			*dz = -uc;
			break;
		case FACE_NX:
			*dx = -1.f;
			*dy = -vc;
			*dz = uc;
			break;
		case FACE_PY:
			*dx = uc;
			*dy = 1.f;
			*dz = vc;
			break;
		case FACE_NY:
			*dx = uc;
			*dy = -1.f;
			*dz = -vc;
			break;
		case FACE_PZ:
			*dx = uc;
			*dy = -vc;
			*dz = 1.f;
			break;
		default:
			*dx = -uc;
			*dy = -vc;
			*dz = -1.f;
			break; /* NZ */
	}
	float inv = 1.f / sqrtf(*dx * *dx + *dy * *dy + *dz * *dz);
	*dx *= inv;
	*dy *= inv;
	*dz *= inv;
}

/* Bilinear sample of the (linear) image at texcoord (tx, ty) in [0,1],
 * clamped at the borders. */
static void TieStarfieldElements_SampleImage(const TieStarfieldImage* img, float tx, float ty, float out[4]) {
	float fx = tx * (float)img->w - 0.5f;
	float fy = ty * (float)img->h - 0.5f;
	int x0 = (int)floorf(fx), y0 = (int)floorf(fy);
	float ax = fx - (float)x0, ay = fy - (float)y0;
	int x1 = x0 + 1, y1 = y0 + 1;
	if (x0 < 0)
		x0 = 0;
	if (x0 > img->w - 1)
		x0 = img->w - 1;
	if (x1 < 0)
		x1 = 0;
	if (x1 > img->w - 1)
		x1 = img->w - 1;
	if (y0 < 0)
		y0 = 0;
	if (y0 > img->h - 1)
		y0 = img->h - 1;
	if (y1 < 0)
		y1 = 0;
	if (y1 > img->h - 1)
		y1 = img->h - 1;
	const float* p = img->rgba;
	for (int c = 0; c < 4; ++c) {
		float a = p[(y0 * img->w + x0) * 4 + c] * (1 - ax) + p[(y0 * img->w + x1) * 4 + c] * ax;
		float b = p[(y1 * img->w + x0) * 4 + c] * (1 - ax) + p[(y1 * img->w + x1) * 4 + c] * ax;
		out[c] = a * (1 - ay) + b * ay;
	}
}

void TieStarfieldElements_StarfieldCompositeElement(float* faces[6], int fs, const TieStarfieldElement* e,
													const TieStarfieldImage* img) {
	if (!e->enabled || !img || !img->rgba || img->w < 1 || img->h < 1)
		return;

	/* Element tangent frame: forward D from yaw/pitch, right/up from
	 * world-up, rolled in-plane. Same direction convention as the
	 * preview camera. */
	float cp = cosf(e->pitch), sp = sinf(e->pitch);
	float cy = cosf(e->yaw), sy = sinf(e->yaw);
	float D[3] = { cp * sy, sp, cp * cy };
	float wup[3] = { 0, 1, 0 };
	float R[3] = { wup[1] * D[2] - wup[2] * D[1], wup[2] * D[0] - wup[0] * D[2],
				   wup[0] * D[1] - wup[1] * D[0] };
	float rl = sqrtf(R[0] * R[0] + R[1] * R[1] + R[2] * R[2]);
	if (rl < 1e-5f) {
		R[0] = 1;
		R[1] = 0;
		R[2] = 0;
		rl = 1;
	}
	R[0] /= rl;
	R[1] /= rl;
	R[2] /= rl;
	float U[3] = { D[1] * R[2] - D[2] * R[1], D[2] * R[0] - D[0] * R[2], D[0] * R[1] - D[1] * R[0] };
	float cr = cosf(e->roll_deg * (float)STARFIELD_PI / 180.f);
	float sr = sinf(e->roll_deg * (float)STARFIELD_PI / 180.f);
	float Rr[3], Ur[3];
	for (int i = 0; i < 3; ++i) {
		Rr[i] = R[i] * cr + U[i] * sr;
		Ur[i] = -R[i] * sr + U[i] * cr;
	}

	/* Gnomonic half-extents: vertical from size_deg, horizontal scaled
	 * by the image aspect so the patch isn't stretched. */
	float tan_v = tanf(0.5f * e->size_deg * (float)STARFIELD_PI / 180.f);
	if (tan_v < 1e-4f)
		tan_v = 1e-4f;
	float tan_h = tan_v * ((float)img->w / (float)img->h);

	/* Quick per-face reject: a cube face reaches at most 54.74° from its
	 * normal (corner); add the patch's corner half-angle. */
	float cone = atanf(sqrtf(tan_h * tan_h + tan_v * tan_v));
	float reject_cos = cosf(0.9553f + cone); /* 0.9553 rad = 54.74° */
	static const float N[6][3] = { { 1, 0, 0 },  { -1, 0, 0 }, { 0, 1, 0 },
								   { 0, -1, 0 }, { 0, 0, 1 },  { 0, 0, -1 } };

	for (int f = 0; f < 6; ++f) {
		if (D[0] * N[f][0] + D[1] * N[f][1] + D[2] * N[f][2] < reject_cos)
			continue;
		float* fc = faces[f];
		for (int py = 0; py < fs; ++py) {
			for (int px = 0; px < fs; ++px) {
				float dx, dy, dz;
				TieStarfieldElements_FaceDirection(f, px, py, fs, &dx, &dy, &dz);
				float denom = dx * D[0] + dy * D[1] + dz * D[2];
				if (denom <= 1e-3f)
					continue; /* >90° from centre */
				float u = (dx * Rr[0] + dy * Rr[1] + dz * Rr[2]) / denom / tan_h;
				float v = (dx * Ur[0] + dy * Ur[1] + dz * Ur[2]) / denom / tan_v;
				if (u < -1.f || u > 1.f || v < -1.f || v > 1.f)
					continue;

				float s[4];
				TieStarfieldElements_SampleImage(img, u * 0.5f + 0.5f, 0.5f - v * 0.5f, s);
				float a = s[3];
				if (a <= 0.f)
					continue;
				size_t idx = ((size_t)py * fs + px) * 3u;
				float ia = 1.f - a;
				fc[idx] = s[0] * e->tint[0] * e->intensity * a + fc[idx] * ia;
				fc[idx + 1] = s[1] * e->tint[1] * e->intensity * a + fc[idx + 1] * ia;
				fc[idx + 2] = s[2] * e->tint[2] * e->intensity * a + fc[idx + 2] * ia;
			}
		}
	}
}
