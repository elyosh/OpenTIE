/*
 * starfield_core — procedural HDR star cubemap generation, shared by
 * the CLI baker (starfield.c) and the ImGui tuner (starfield_tune.cpp).
 *
 * The renderer scatters stars uniformly on the sphere, projects each
 * direction onto whichever cube face(s) it faces, and additively
 * splats it as a linear-HDR Gaussian (or single pixel for the
 * faintest). The 6 faces feed write_ktx2_bc6h_cubemap_with_generated_mips
 * → a BC6H_UFLOAT KTX2 the runtime samples as HDR-linear.
 *
 * Every knob that shaped the original hardcoded look is promoted to
 * TieStarfieldParams so the tuner can drive it live; the CLI and tuner
 * share the exact same render so the preview can't drift from the bake.
 */
#ifndef STARFIELD_CORE_H
#define STARFIELD_CORE_H

#include "starfield_elements.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Three Gaussian splat tiers above the faint floor. A star's
 * normalized brightness (0..1, before the intensity scale) selects a
 * tier by threshold; the matching sigma/radius set the splat size. */
enum { STARFIELD_TIERS = 3 };

/* Splat sigma/radius in TieStarfieldParams are authored in pixels at this
 * reference face resolution. The renderer scales them by
 * face_size / STARFIELD_REF_FACE, so a star's angular size is
 * sigma / STARFIELD_REF_FACE at any bake resolution
 * (resolution-independent): two bake sizes differ only in sharpness.
 *
 * 3840 = the original Go tool's image width, so the default tier sizes
 * (1.00/1.45/1.90 px sigma, copied from that tool) reproduce its star
 * sizes as seen in its 3840×2160 output. */
enum { STARFIELD_REF_FACE = 3840 };

typedef struct {
	/* ---- counts ---- */
	int num_stars;   /* stars over the whole sphere */
	uint64_t seed;   /* PRNG seed */
	float intensity; /* linear radiance scale (HDR headroom) */

	/* ---- brightness distribution ---- */
	float bright_floor; /* min normalized brightness (0..1) */
	float bright_pow;   /* power-law exponent on a uniform u */

	/* ---- size tiers (normalized-brightness thresholds, ascending) ----
	 * rel <  thresh[0]                       → single pixel
	 * thresh[i-1] <= rel < thresh[i]         → splat tier i
	 * rel >= thresh[STARFIELD_TIERS-1]       → splat last tier
	 * The splat's clip radius is derived as ceil(3·sigma) at render
	 * time (a Gaussian is negligible beyond 3σ), so sigma alone sets
	 * the star size — there is no separate radius control. */
	float tier_thresh[STARFIELD_TIERS];
	float tier_sigma[STARFIELD_TIERS];

	/* ---- colour / spectral spread ---- */
	float tint_sigma;    /* Gaussian sigma of the temp term */
	float tint_strength; /* scales blue↔warm channel skew */
	float tint_bias;     /* shifts the temp mean: <0 bluer, >0 warmer */

	/* ---- background ---- */
	float bg_color[3]; /* linear-RGB fill behind the stars */

	/* ---- output ---- */
	int face_size; /* per-face dim (multiple of 4) */
	bool zstd;     /* zstd-supercompress the KTX2 levels */
} TieStarfieldParams;

/* Fill p with the defaults that reproduce the original hardcoded look. */
void TieStarfieldCore_StarfieldDefaultParams(TieStarfieldParams* p);

/* Render p->num_stars into 6 linear-HDR float faces, each
 * p->face_size² × 3 floats, tightly packed, top-row-first, in KTX2
 * face order (+X, -X, +Y, -Y, +Z, -Z). The caller allocates (and must
 * zero) each faces[f]; the renderer only adds into them. */
void TieStarfieldCore_StarfieldRenderFaces(const TieStarfieldParams* p, float* faces[6]);

/* Render the full scene into 6 caller-allocated faces: stars, then the
 * `n_els` elements alpha-composited on top (each image loaded from its
 * path and freed). Faces are filled (caller need not zero them). */
void TieStarfieldCore_StarfieldRenderScene(const TieStarfieldParams* p, const TieStarfieldElement* els,
										   int n_els, float* faces[6]);

/* Reflect the 6 natural (readable) faces across world-X into the
 * engine's GPU cube convention (h-flip each face + swap ±X). Apply once,
 * immediately before writing a KTX2 — NOT to the faces used for the live
 * preview, which stay natural. */
void TieStarfieldCore_StarfieldMirrorCubeX(float* faces[6], int fs);

/* Render the full scene at p->face_size and write a BC6H_UFLOAT KTX2
 * cubemap to path. Returns false on allocation or write failure. */
bool TieStarfieldCore_StarfieldBakeKtx2(const TieStarfieldParams* p, const TieStarfieldElement* els,
										int n_els, const char* path);

#ifdef __cplusplus
}
#endif

#endif /* STARFIELD_CORE_H */
