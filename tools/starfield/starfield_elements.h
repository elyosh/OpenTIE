/*
 * starfield_elements — artist-placed sky elements (planets, moons,
 * galaxies) composited into the star cubemap.
 *
 * Each element is an image patch pinned to a direction on the sphere
 * and gnomonic-projected onto the cube faces, alpha-composited over the
 * stars in linear light (opaque disc occludes the stars behind it). The
 * same compositing runs in the CLI bake and the tuner preview, so what
 * the artist places is what bakes.
 */
#ifndef STARFIELD_ELEMENTS_H
#define STARFIELD_ELEMENTS_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { STARFIELD_MAX_ELEMENTS = 64, STARFIELD_PATH_MAX = 1024 };

typedef struct {
	char path[STARFIELD_PATH_MAX]; /* source image (PNG or .hdr) */
	float yaw, pitch;              /* radians; direction on the sphere */
	float size_deg;                /* full vertical angular size */
	float roll_deg;                /* in-plane rotation */
	float intensity;               /* linear radiance multiplier */
	float tint[3];                 /* per-channel multiplier */
	bool enabled;
} TieStarfieldElement;

/* Decoded image in linear light, tightly packed RGBA float. Alpha is
 * coverage (PNG alpha / 1.0 for .hdr). */
typedef struct {
	int w, h;
	float* rgba;
} TieStarfieldImage;

/* Load + decode. PNG: sRGB→linear on RGB, alpha/255. .hdr: float linear,
 * alpha = 1. Returns false (and leaves out zeroed) on failure. */
bool TieStarfieldElements_StarfieldImageLoad(const char* path, TieStarfieldImage* out);
void TieStarfieldElements_StarfieldImageFree(TieStarfieldImage* img);

/* Alpha-composite one element (using preloaded `img`) into the 6 linear
 * face buffers (face_size² × 3 floats each, KTX2 order +X,-X,+Y,-Y,
 * +Z,-Z). No-op if the element is disabled or img is empty. */
void TieStarfieldElements_StarfieldCompositeElement(float* faces[6], int fs, const TieStarfieldElement* e,
													const TieStarfieldImage* img);

/* Default-initialise an element (white, 10° at the +Z forward pole). */
void TieStarfieldElements_StarfieldElementDefault(TieStarfieldElement* e);

#ifdef __cplusplus
}
#endif

#endif /* STARFIELD_ELEMENTS_H */
