/*
 * starfield_core — see starfield_core.h.
 *
 * Face pixel↔direction convention is the algebraic inverse of
 * skybox_pack.c's face_direction(), so a baked star lands where the
 * runtime cube sampler reads it. Stars near a face edge are splatted
 * onto every face whose normal they face beyond COVER_DOT, which fills
 * both sides of the seam and keeps bright cores whole across edges.
 */

#include "starfield_core.h"
#include "imgbake/ktx2_writer.h"

#include <math.h>
#include <stdlib.h>

/* KTX2 face order: +X, -X, +Y, -Y, +Z, -Z. */
enum { FACE_PX = 0, FACE_NX, FACE_PY, FACE_NY, FACE_PZ, FACE_NZ };

/* A face-centre direction scores dot 1.0; an edge-shared direction
 * 1/√2 ≈ 0.707 on both faces; a corner 1/√3 ≈ 0.577 on all three. 0.5
 * therefore catches every face a near-edge splat can spill onto, and
 * bounds the projection divisor (= this dot) away from zero. */
#define COVER_DOT 0.5

void TieStarfieldCore_StarfieldDefaultParams(TieStarfieldParams* p) {
	/* 6000 stars filled the original Go tool's 3840×2160 frame; reading
	 * that frame as one hemisphere of sky (2π sr), the full sphere (4π)
	 * carries 6000·4π/2π = 12000 at the same areal density. Matches the
	 * Go reference look without over-populating the sky. */
	p->num_stars = 12000;
	p->seed = 0;
	p->intensity = 1.0f;
	p->bright_floor = 0.10f;
	p->bright_pow = 4.0f;
	p->tier_thresh[0] = 0.30f;
	p->tier_sigma[0] = 1.00f;
	p->tier_thresh[1] = 0.65f;
	p->tier_sigma[1] = 1.45f;
	p->tier_thresh[2] = 0.92f;
	p->tier_sigma[2] = 1.90f;
	p->tint_sigma = 0.15f;
	p->tint_strength = 1.0f;
	p->tint_bias = 0.0f;
	p->bg_color[0] = 0.0f;
	p->bg_color[1] = 0.0f;
	p->bg_color[2] = 0.0f;
	p->face_size = 1024;
	p->zstd = true;
}

/* ---------- PRNG: xoshiro256** seeded via splitmix64 ---------- */

typedef struct {
	uint64_t s[4];
} TieStarfieldRng;

static uint64_t TieStarfieldCore_Splitmix64(uint64_t* x) {
	uint64_t z = (*x += 0x9E3779B97F4A7C15ULL);
	z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
	z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
	return z ^ (z >> 31);
}

static void TieStarfieldCore_RngSeed(TieStarfieldRng* r, uint64_t seed) {
	uint64_t sm = seed;
	for (int i = 0; i < 4; ++i)
		r->s[i] = TieStarfieldCore_Splitmix64(&sm);
}

static inline uint64_t TieStarfieldCore_Rotl(uint64_t x, int k) { return (x << k) | (x >> (64 - k)); }

static uint64_t TieStarfieldCore_RngNext(TieStarfieldRng* r) {
	uint64_t* s = r->s;
	uint64_t res = TieStarfieldCore_Rotl(s[1] * 5, 7) * 9;
	uint64_t t = s[1] << 17;
	s[2] ^= s[0];
	s[3] ^= s[1];
	s[1] ^= s[2];
	s[0] ^= s[3];
	s[2] ^= t;
	s[3] = TieStarfieldCore_Rotl(s[3], 45);
	return res;
}

/* Uniform double in [0, 1) from the top 53 bits. */
static double TieStarfieldCore_RngF64(TieStarfieldRng* r) {
	return (double)(TieStarfieldCore_RngNext(r) >> 11) * (1.0 / 9007199254740992.0);
}

/* Standard normal via Box–Muller (one of the two deviates). */
static double TieStarfieldCore_RngNorm(TieStarfieldRng* r) {
	double u1 = TieStarfieldCore_RngF64(r);
	double u2 = TieStarfieldCore_RngF64(r);
	if (u1 < 1e-300)
		u1 = 1e-300;
	return sqrt(-2.0 * log(u1)) * cos(6.283185307179586 * u2);
}

static double TieStarfieldCore_Clampd(double v, double lo, double hi) {
	return v < lo ? lo : (v > hi ? hi : v);
}

/* ---------- rendering ---------- */

/* Additive isotropic 2D Gaussian centred at (cx, cy) in face-pixel
 * space, clipped to a (radius)-pixel box and to the face bounds. */
static void TieStarfieldCore_Splat(float* face, int fs, double cx, double cy, float cr, float cg, float cb,
								   double sigma, int radius) {
	int x0 = (int)floor(cx) - radius, x1 = (int)floor(cx) + radius + 1;
	int y0 = (int)floor(cy) - radius, y1 = (int)floor(cy) + radius + 1;
	if (x0 < 0)
		x0 = 0;
	if (y0 < 0)
		y0 = 0;
	if (x1 > fs)
		x1 = fs;
	if (y1 > fs)
		y1 = fs;
	if (x0 >= x1 || y0 >= y1)
		return;

	double inv = 1.0 / (2.0 * sigma * sigma);
	for (int yy = y0; yy < y1; ++yy) {
		double dy = (double)yy - cy, dy2 = dy * dy;
		for (int xx = x0; xx < x1; ++xx) {
			double dx = (double)xx - cx;
			float k = (float)exp(-(dx * dx + dy2) * inv);
			size_t idx = ((size_t)yy * (size_t)fs + (size_t)xx) * 3u;
			face[idx] += cr * k;
			face[idx + 1] += cg * k;
			face[idx + 2] += cb * k;
		}
	}
}

/* Project a unit star direction onto one cube face's pixel plane.
 * Returns false if the star does not face this face beyond COVER_DOT. */
static bool TieStarfieldCore_ProjectToFace(int face, double dx, double dy, double dz, int fs, double* px,
										   double* py) {
	double sc, tc, dn;
	switch (face) {
		case FACE_PX:
			dn = dx;
			sc = -dz;
			tc = -dy;
			break;
		case FACE_NX:
			dn = -dx;
			sc = dz;
			tc = -dy;
			break;
		case FACE_PY:
			dn = dy;
			sc = dx;
			tc = dz;
			break;
		case FACE_NY:
			dn = -dy;
			sc = dx;
			tc = -dz;
			break;
		case FACE_PZ:
			dn = dz;
			sc = dx;
			tc = -dy;
			break;
		case FACE_NZ:
			dn = -dz;
			sc = -dx;
			tc = -dy;
			break;
		default:
			return false;
	}
	if (dn <= COVER_DOT)
		return false;
	double uc = sc / dn, vc = tc / dn; /* face coords in [-1, 1] */
	*px = (uc * 0.5 + 0.5) * (double)fs - 0.5;
	*py = (vc * 0.5 + 0.5) * (double)fs - 0.5;
	return true;
}

void TieStarfieldCore_StarfieldRenderFaces(const TieStarfieldParams* p, float* faces[6]) {
	TieStarfieldRng rng;
	TieStarfieldCore_RngSeed(&rng, p->seed);
	int fs = p->face_size;
	double floor_b = p->bright_floor;
	double span_b = 1.0 - floor_b;
	double pow_b = p->bright_pow;

	/* Resolution independence: splat sigma/radius are authored at
	 * STARFIELD_REF_FACE pixels, scaled to the actual face size so a
	 * preset's angular star sizes hold at any resolution. The single-
	 * pixel faint tier is a point at any resolution (it does not
	 * scale — that is what "single pixel" means). */
	double scale = (double)fs / (double)STARFIELD_REF_FACE;

	/* Fill the background colour; stars are then added on top. */
	for (int f = 0; f < 6; ++f) {
		float* fc = faces[f];
		for (size_t t = 0; t < (size_t)fs * (size_t)fs; ++t) {
			fc[t * 3 + 0] = p->bg_color[0];
			fc[t * 3 + 1] = p->bg_color[1];
			fc[t * 3 + 2] = p->bg_color[2];
		}
	}

	for (int i = 0; i < p->num_stars; ++i) {
		/* Uniform direction on the sphere: z uniform in [-1, 1],
		 * azimuth uniform in [0, 2π). Faces are rendered in the natural
		 * (readable) convention; the GPU-cube X-mirror is applied once,
		 * at KTX2 write (starfield_mirror_cube_x). */
		double z = 2.0 * TieStarfieldCore_RngF64(&rng) - 1.0;
		double phi = 6.283185307179586 * TieStarfieldCore_RngF64(&rng);
		double rxy = sqrt(1.0 - z * z);
		double dx = rxy * cos(phi), dy = rxy * sin(phi), dz = z;

		/* Power-law brightness with a floor; subtle colour tint
		 * (negative temp → blue, positive → warm). `rel` is the
		 * normalized brightness used for tier selection. */
		double u = TieStarfieldCore_RngF64(&rng);
		double rel = floor_b + span_b * pow(u, pow_b);
		double bright = rel * (double)p->intensity;
		double temp =
			TieStarfieldCore_Clampd(
				TieStarfieldCore_RngNorm(&rng) * (double)p->tint_sigma + (double)p->tint_bias, -0.4, 0.4) *
			(double)p->tint_strength;
		float cr = (float)(bright * (1.0 + 0.5 * temp));
		float cg = (float)(bright * (1.0 - 0.1 * fabs(temp)));
		float cb = (float)(bright * (1.0 - 0.5 * temp));

		bool single = rel < p->tier_thresh[0];
		double sigma = 0;
		int radius = 0;
		if (!single) {
			int t = STARFIELD_TIERS - 1;
			for (int k = 0; k < STARFIELD_TIERS; ++k) {
				if (rel < p->tier_thresh[k]) {
					t = k;
					break;
				}
			}
			sigma = p->tier_sigma[t] * scale;
			if (sigma < 1e-3)
				sigma = 1e-3;
			/* Clip box at 3σ — a Gaussian is <1% of peak beyond that,
			 * so sigma alone governs the visible star size. */
			radius = (int)ceil(3.0 * sigma);
			if (radius < 1)
				radius = 1;
		}

		for (int f = 0; f < 6; ++f) {
			double px, py;
			if (!TieStarfieldCore_ProjectToFace(f, dx, dy, dz, fs, &px, &py))
				continue;
			if (single) {
				int ix = (int)floor(px + 0.5), iy = (int)floor(py + 0.5);
				if (ix < 0 || ix >= fs || iy < 0 || iy >= fs)
					continue;
				size_t idx = ((size_t)iy * (size_t)fs + (size_t)ix) * 3u;
				faces[f][idx] += cr;
				faces[f][idx + 1] += cg;
				faces[f][idx + 2] += cb;
			} else {
				TieStarfieldCore_Splat(faces[f], fs, px, py, cr, cg, cb, sigma, radius);
			}
		}
	}
}

void TieStarfieldCore_StarfieldRenderScene(const TieStarfieldParams* p, const TieStarfieldElement* els,
										   int n_els, float* faces[6]) {
	TieStarfieldCore_StarfieldRenderFaces(p, faces); /* stars + background fill */
	for (int i = 0; i < n_els; ++i) {
		if (!els[i].enabled || els[i].path[0] == '\0')
			continue;
		TieStarfieldImage img;
		if (!TieStarfieldElements_StarfieldImageLoad(els[i].path, &img))
			continue;
		TieStarfieldElements_StarfieldCompositeElement(faces, p->face_size, &els[i], &img);
		TieStarfieldElements_StarfieldImageFree(&img);
	}
}

void TieStarfieldCore_StarfieldMirrorCubeX(float* faces[6], int fs) {
	/* Reflect the cube across the world-X plane to convert the natural
	 * (readable) faces into the engine's GPU cube convention. mirrorX of
	 * a direction maps each face to a horizontally-flipped copy, with
	 * the +X/-X faces swapped. A true cube reflection — seamless. The
	 * runtime samples a left-handed cube, so this is what makes a
	 * readable bake render un-mirrored in flight (same convention as
	 * skybox_pack). Call once, just before writing the KTX2. */
	for (int f = 0; f < 6; ++f) { /* h-flip every face */
		float* fc = faces[f];
		for (int y = 0; y < fs; ++y) {
			float* row = fc + (size_t)y * fs * 3u;
			for (int x = 0; x < fs / 2; ++x) {
				float* a = row + (size_t)x * 3u;
				float* b = row + (size_t)(fs - 1 - x) * 3u;
				for (int c = 0; c < 3; ++c) {
					float t = a[c];
					a[c] = b[c];
					b[c] = t;
				}
			}
		}
	}
	float* tmp = faces[FACE_PX]; /* swap +X / -X */
	faces[FACE_PX] = faces[FACE_NX];
	faces[FACE_NX] = tmp;
}

bool TieStarfieldCore_StarfieldBakeKtx2(const TieStarfieldParams* p, const TieStarfieldElement* els,
										int n_els, const char* path) {
	size_t face_floats = (size_t)p->face_size * (size_t)p->face_size * 3u;
	float* faces[6] = { 0 };
	bool ok = true;
	for (int f = 0; f < 6; ++f) {
		faces[f] = (float*)calloc(face_floats, sizeof(float));
		if (!faces[f]) {
			ok = false;
			break;
		}
	}
	if (ok) {
		TieStarfieldCore_StarfieldRenderScene(p, els, n_els, faces);
		TieStarfieldCore_StarfieldMirrorCubeX(faces, p->face_size); /* → GPU convention */
		const float* cf[6] = { faces[0], faces[1], faces[2], faces[3], faces[4], faces[5] };
		ok = write_ktx2_bc6h_cubemap_with_generated_mips(path, p->face_size, cf, KTX2_BC6H_QUALITY_FAST,
														 p->zstd);
	}
	for (int f = 0; f < 6; ++f)
		free(faces[f]);
	return ok;
}
