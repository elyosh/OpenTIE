/* Cockpit HUD layout loaded from <craft>_hud_layout.yaml. */
#ifndef COCKPIT_LAYOUT_H
#define COCKPIT_LAYOUT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#include "aeron/vfs.h"

#define COCKPIT_LAYOUT_MAX_INSTRUMENTS 95

typedef struct {
	/* Atlas pixel rect of this cel. */
	int atlas_x;
	int atlas_y;
	int atlas_w;
	int atlas_h;
	/* On-screen dimensions in the reference coord frame. */
	int size_w;
	int size_h;
} TieCockpitLayoutCel;

/* Sparse per-instrument overrides in reference-frame pixels. Zero means
 * no override; negative integer values are unsupported. */
typedef struct {
	int16_t x;
	int16_t y;
	int16_t radius;   /* radar disc */
	int16_t stride_x; /* repeated widget spacing */
	int16_t stride_y;
	int16_t right_at;  /* right-aligned text edge */
	int16_t center_at; /* centered text axis */
	int16_t label_at;  /* label/value split */
	float hdr_boost;   /* linear RGB multiplier; zero resolves to 1.0 */
	uint8_t present;
} TieCockpitLayoutInstrumentAnchor;

typedef struct {
	int reference_w; /* coord frame the renderer uses */
	int reference_h;
	int atlas_w; /* shipped-texture dims */
	int atlas_h;
	TieCockpitLayoutCel* cels;
	int cel_count;
	/* PIP override. pip_present == 0 means "use snap->cockpit.pip_*". */
	int16_t pip_x;
	int16_t pip_y;
	int16_t pip_w;
	int16_t pip_h;
	uint8_t pip_present;
	/* Optional cockpit-text scale override. font_atlas_scale > 0 means
	 * "use this scale"; otherwise the renderer computes
	 * 4.5 × reference_h / 480 at consume time. */
	float font_atlas_scale;
	/* Sparse instrument anchor table, indexed by HUD instrument id. */
	TieCockpitLayoutInstrumentAnchor instruments[COCKPIT_LAYOUT_MAX_INSTRUMENTS];
} TieCockpitLayout;

/* Load a layout YAML. Returns false on parse / IO error. */
bool TieCockpitLayout_Load(TieCockpitLayout* out, AeronVfs* vfs, AeronVfsRoot root, const char* yaml_path);

void TieCockpitLayout_Free(TieCockpitLayout* l);

/* Look up one cel by index (== classic farbufferptrs[] slot). Returns
 * NULL when idx is out of range or layout is empty. */
const TieCockpitLayoutCel* TieCockpitLayout_Cel(const TieCockpitLayout* l, int idx);

/* Resolve an authored anchor or rescale its classic coordinates. */
void TieCockpitLayout_Anchor(const TieCockpitLayout* l, int id, int16_t snap_x, int16_t snap_y, int classic_w,
							 int classic_h, float* out_x, float* out_y);

/* Per-instrument HDR brightness boost (linear RGB multiplier for the
 * instrument's lit cels). Returns the authored `hdr_boost:` value, or
 * 1.0 when the layout is NULL, the id is out of range, the instrument
 * wasn't listed, or no boost was authored. */
float TieCockpitLayout_HdrBoost(const TieCockpitLayout* l, int id);

/* Render coord-frame size the cockpit pass should use. When a layout is
 * loaded, returns its reference frame; otherwise returns the snapshot's
 * classic frame. Callers pass (classic_w, classic_h) as the fallback. */
void TieCockpitLayout_CoordFrame(const TieCockpitLayout* l, int classic_w, int classic_h, int* out_w,
								 int* out_h);

/* Return the classic-to-reference scale used by missing overrides. */
void TieCockpitLayout_ClassicPixelSize(const TieCockpitLayout* l, int classic_w, int classic_h, float* out_sx,
									   float* out_sy);

/* Per-radar disc radius in reference-frame px. Returns 0 when the
 * layout has no `radius:` field for the given radar id (caller falls
 * back to a global classic_pixel_size-derived radius). id should be
 * TIE_HUDI_RADAR_LEFT or TIE_HUDI_RADAR_RIGHT. */
int TieCockpitLayout_RadarRadius(const TieCockpitLayout* l, int id);

/* Per-instrument stride in reference-frame px for cluster widgets
 * (slider rungs, LED rows, beam-arc). Each axis falls back to 0 when
 * the layout didn't author one — caller substitutes
 * classic_step × classic_pixel_size. */
void TieCockpitLayout_Stride(const TieCockpitLayout* l, int id, int16_t* out_sx, int16_t* out_sy);

#ifdef __cplusplus
}
#endif

#endif
