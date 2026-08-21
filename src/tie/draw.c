#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "anim.h"
#include "tie/bpflight.h"
#include "tie/create.h"
#include "tie/draw.h"
#include "tie/drawpol.h"
#include "tie/fediskio.h" /* species_model_handle_sizes (model-buffer bounds) */
#include "tie/fview.h"
#include "tie/laser.h"   /* WEAPON_SPECIES_COUNT, laser_species_idx */
#include "tie/logbuf2.h" /* pixelsdeep */
#include "tie/mission.h"
#include "tie/modelmesh.h"
#include "tie/render_scene_tie98.h"
#include "tie/rotscale.h"
#include "tie/spec.h"
#include "tie/species.h" /* hyperstardata */
#include "tie/tie.h"
#include "tie/transfm2.h"
#include "tie/trig2.h"
#include "tie/xtrans2.h"                              /* flatobjnum */
#include "tie_runtime/diagnostics/diagnostics.h"      /* TieDiagnostics_Log (polydepthsort OOB diagnostic) */
#include "tie_runtime/snapshot/snapshot_billboards.h" /* SNAPSHOT-ONLY billboard capture */

/* ============================================================================
 * Module-owned globals (per watdbg attribution to draw.c).
 * ========================================================================== */

uint16_t comp[40];
// GLOBAL: TIE 0xD35E0
uint16_t highlightcolor;
// GLOBAL: TIE 0xD35E2
uint16_t numberofcomp;
// GLOBAL: TIE 0xD35E4
int16_t relativeshift;
// GLOBAL: TIE 0xD35E8
int16_t relativex;
// GLOBAL: TIE 0xD35EA
int16_t relativey;
// GLOBAL: TIE 0xD35E6
int16_t relativez;

/* ============================================================================
 * External references (cross-module globals/functions not declared elsewhere).
 * ========================================================================== */

/* objects[] FlightObject array, indexed by obj_idx. (defined in tie.c) */

/* Per-weapon-species polygon meshes, extracted bit-for-bit from retail
 * Z_TIE__.EXE. Retail stores these as ten distinct polygon blobs in dseg
 * (0x000CC9DA..0x000CD10C) and exposes them via the laser_species_poly[]
 * pointer table below (retail 0x000CCEE8, indexed by ship_idx). Retail
 * skips laser species in FEDISKIO_loadspecies (flags & 2 == 0) because
 * their meshes are baked in here rather than loaded from LFD — that's
 * why model_handle stays NULL for laser species and DRAW_Lockshipfileptrs
 * is never reached for a real laser bolt.
 *
 * Structure per blob (inferred): 0x4-byte bounds header, mesh header,
 * vertex list, face list with per-face palette indices near the tail.
 * The colour ramps (last few bytes before 0x41) differentiate the bolts:
 *   137/138 + 144         -> 0xC1..0xC5 (green ramp)
 *   139/140 + 151         -> 0xBC..0xC0 (red/orange ramp)
 *   141/142 + 143         -> 0xC7..0xCB (purple ramp)
 *   152..154              -> 0xD1..0xD5 (yellow ramp)
 * Species 145..149 re-use 138/140/142/143/144's blobs verbatim (aliases
 * in the pointer table). */
static const uint8_t laser_bolt_137[183] = {
	0x00, 0x10, 0x00, 0x00, 0x12, 0x00, 0x00, 0x40, 0x00, 0x00, 0x65, 0x00, 0xff, 0xff, 0xff, 0x7f, 0x96,
	0x00, 0x41, 0x00, 0x0a, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x0e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0d,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0c, 0x00, 0x00, 0x14, 0x00, 0x08,
	0x09, 0xc5, 0x10, 0x00, 0x06, 0x07, 0xc4, 0x0c, 0x00, 0x04, 0x05, 0xc3, 0x08, 0x00, 0x02, 0x03, 0xc2,
	0x04, 0x00, 0x00, 0x01, 0xc1, 0x41, 0x00, 0x06, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0e, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0c, 0x00, 0x00, 0x14, 0x00, 0x04, 0x05, 0xc5, 0x0c,
	0x00, 0x02, 0x03, 0xc3, 0x04, 0x00, 0x00, 0x01, 0xc1, 0x41, 0x00, 0x02, 0x01, 0x00, 0x00, 0x00, 0x02,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x0e, 0x00, 0x00, 0x0c, 0x00, 0x00, 0x01, 0xc3,
};

static const uint8_t laser_bolt_138[183] = {
	0x00, 0x10, 0x00, 0x00, 0x12, 0x00, 0x00, 0x40, 0x00, 0x00, 0x65, 0x00, 0xff, 0xff, 0xff, 0x7f, 0x96,
	0x00, 0x41, 0x00, 0x0a, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x0e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0d,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0c, 0x00, 0x00, 0x1e, 0x00, 0x08,
	0x09, 0xc5, 0x18, 0x00, 0x06, 0x07, 0xc4, 0x12, 0x00, 0x04, 0x05, 0xc3, 0x0c, 0x00, 0x02, 0x03, 0xc2,
	0x06, 0x00, 0x00, 0x01, 0xc1, 0x41, 0x00, 0x06, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0e, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0c, 0x00, 0x00, 0x1e, 0x00, 0x04, 0x05, 0xc5, 0x12,
	0x00, 0x02, 0x03, 0xc3, 0x06, 0x00, 0x00, 0x01, 0xc1, 0x41, 0x00, 0x02, 0x01, 0x00, 0x00, 0x00, 0x02,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x0e, 0x00, 0x00, 0x12, 0x00, 0x00, 0x01, 0xc3,
};

static const uint8_t laser_bolt_139[183] = {
	0x00, 0x10, 0x00, 0x00, 0x12, 0x00, 0x00, 0x40, 0x00, 0x00, 0x65, 0x00, 0xff, 0xff, 0xff, 0x7f, 0x96,
	0x00, 0x41, 0x00, 0x0a, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x0e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0d,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0c, 0x00, 0x00, 0x14, 0x00, 0x08,
	0x09, 0xc0, 0x10, 0x00, 0x06, 0x07, 0xbf, 0x0c, 0x00, 0x04, 0x05, 0xbe, 0x08, 0x00, 0x02, 0x03, 0xbd,
	0x04, 0x00, 0x00, 0x01, 0xbc, 0x41, 0x00, 0x06, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0e, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0c, 0x00, 0x00, 0x14, 0x00, 0x04, 0x05, 0xc0, 0x0c,
	0x00, 0x02, 0x03, 0xbe, 0x04, 0x00, 0x00, 0x01, 0xbc, 0x41, 0x00, 0x02, 0x01, 0x00, 0x00, 0x00, 0x02,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x0e, 0x00, 0x00, 0x0c, 0x00, 0x00, 0x01, 0xbe,
};

static const uint8_t laser_bolt_140[183] = {
	0x00, 0x10, 0x00, 0x00, 0x12, 0x00, 0x00, 0x40, 0x00, 0x00, 0x65, 0x00, 0xff, 0xff, 0xff, 0x7f, 0x96,
	0x00, 0x41, 0x00, 0x0a, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x0e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0d,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0c, 0x00, 0x00, 0x1e, 0x00, 0x08,
	0x09, 0xc0, 0x18, 0x00, 0x06, 0x07, 0xbf, 0x12, 0x00, 0x04, 0x05, 0xbe, 0x0c, 0x00, 0x02, 0x03, 0xbd,
	0x06, 0x00, 0x00, 0x01, 0xbc, 0x41, 0x00, 0x06, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0e, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0c, 0x00, 0x00, 0x1e, 0x00, 0x04, 0x05, 0xc0, 0x12,
	0x00, 0x02, 0x03, 0xbe, 0x06, 0x00, 0x00, 0x01, 0xbc, 0x41, 0x00, 0x02, 0x01, 0x00, 0x00, 0x00, 0x02,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x0e, 0x00, 0x00, 0x12, 0x00, 0x00, 0x01, 0xbe,
};

static const uint8_t laser_bolt_141[183] = {
	0x00, 0x10, 0x00, 0x00, 0x12, 0x00, 0x00, 0x40, 0x00, 0x00, 0x65, 0x00, 0xff, 0xff, 0xff, 0x7f, 0x96,
	0x00, 0x41, 0x00, 0x0a, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x0e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0d,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0c, 0x00, 0x00, 0x14, 0x00, 0x08,
	0x09, 0xcb, 0x10, 0x00, 0x06, 0x07, 0xca, 0x0c, 0x00, 0x04, 0x05, 0xc9, 0x08, 0x00, 0x02, 0x03, 0xc8,
	0x04, 0x00, 0x00, 0x01, 0xc7, 0x41, 0x00, 0x06, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0e, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0c, 0x00, 0x00, 0x14, 0x00, 0x04, 0x05, 0xcb, 0x0c,
	0x00, 0x02, 0x03, 0xc9, 0x04, 0x00, 0x00, 0x01, 0xc7, 0x41, 0x00, 0x02, 0x01, 0x00, 0x00, 0x00, 0x02,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x0e, 0x00, 0x00, 0x0c, 0x00, 0x00, 0x01, 0xc9,
};

static const uint8_t laser_bolt_142[183] = {
	0x00, 0x10, 0x00, 0x00, 0x12, 0x00, 0x00, 0x40, 0x00, 0x00, 0x65, 0x00, 0xff, 0xff, 0xff, 0x7f, 0x96,
	0x00, 0x41, 0x00, 0x0a, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x0e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0d,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0c, 0x00, 0x00, 0x1e, 0x00, 0x08,
	0x09, 0xcb, 0x18, 0x00, 0x06, 0x07, 0xca, 0x12, 0x00, 0x04, 0x05, 0xc9, 0x0c, 0x00, 0x02, 0x03, 0xc8,
	0x06, 0x00, 0x00, 0x01, 0xc7, 0x41, 0x00, 0x06, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0e, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0c, 0x00, 0x00, 0x1e, 0x00, 0x04, 0x05, 0xcb, 0x12,
	0x00, 0x02, 0x03, 0xc9, 0x06, 0x00, 0x00, 0x01, 0xc7, 0x41, 0x00, 0x02, 0x01, 0x00, 0x00, 0x00, 0x02,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x0e, 0x00, 0x00, 0x12, 0x00, 0x00, 0x01, 0xc9,
};

static const uint8_t laser_bolt_143[186] = {
	0x00, 0x10, 0x00, 0x00, 0x12, 0x00, 0x00, 0x40, 0x00, 0x00, 0x69, 0x00, 0xff, 0xff, 0xff, 0x7f, 0x99,
	0x00, 0x41, 0x00, 0x09, 0x07, 0x00, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00, 0x00, 0x80, 0x02, 0x00, 0x00,
	0x00, 0x00, 0x80, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00,
	0x00, 0x00, 0x00, 0x80, 0x01, 0x00, 0x00, 0x00, 0x00, 0xa0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0xc0, 0x02, 0x00, 0x00, 0x0a, 0x00, 0x03, 0x01, 0xcb, 0x06, 0x00, 0x02, 0x01,
	0xca, 0x02, 0x00, 0x00, 0x01, 0xc9, 0x18, 0x00, 0x01, 0x04, 0xc8, 0x10, 0x00, 0x08, 0x05, 0xc7, 0x0c,
	0x00, 0x01, 0x06, 0xc7, 0x06, 0x00, 0x01, 0x07, 0xc7, 0x41, 0x00, 0x05, 0x04, 0x00, 0x00, 0x80, 0x02,
	0x00, 0x00, 0x00, 0x00, 0x80, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x00, 0x80,
	0x01, 0x00, 0x00, 0x00, 0x00, 0xa0, 0x00, 0x00, 0x00, 0x0a, 0x00, 0x02, 0x00, 0xc8, 0x06, 0x00, 0x01,
	0x00, 0xca, 0x12, 0x00, 0x00, 0x03, 0xc7, 0x0a, 0x00, 0x00, 0x04, 0xc7, 0x41, 0x00, 0x02, 0x01, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x06, 0x00, 0x00, 0x01, 0xc9,
};

static const uint8_t laser_bolt_144[186] = {
	0x00, 0x10, 0x00, 0x00, 0x12, 0x00, 0x00, 0x40, 0x00, 0x00, 0x69, 0x00, 0xff, 0xff, 0xff, 0x7f, 0x99,
	0x00, 0x41, 0x00, 0x09, 0x07, 0x00, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00, 0x00, 0x80, 0x02, 0x00, 0x00,
	0x00, 0x00, 0x80, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00,
	0x00, 0x00, 0x00, 0x80, 0x01, 0x00, 0x00, 0x00, 0x00, 0xa0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0xc0, 0x02, 0x00, 0x00, 0x0a, 0x00, 0x03, 0x01, 0xc5, 0x06, 0x00, 0x02, 0x01,
	0xc4, 0x02, 0x00, 0x00, 0x01, 0xc3, 0x18, 0x00, 0x01, 0x04, 0xc2, 0x10, 0x00, 0x08, 0x05, 0xc1, 0x0c,
	0x00, 0x01, 0x06, 0xc1, 0x06, 0x00, 0x01, 0x07, 0xc1, 0x41, 0x00, 0x05, 0x04, 0x00, 0x00, 0x80, 0x02,
	0x00, 0x00, 0x00, 0x00, 0x80, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x00, 0x80,
	0x01, 0x00, 0x00, 0x00, 0x00, 0xa0, 0x00, 0x00, 0x00, 0x0a, 0x00, 0x02, 0x00, 0xc5, 0x06, 0x00, 0x01,
	0x00, 0xc4, 0x12, 0x00, 0x00, 0x03, 0xc1, 0x0a, 0x00, 0x00, 0x04, 0xc1, 0x41, 0x00, 0x02, 0x01, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x06, 0x00, 0x00, 0x01, 0xc3,
};

static const uint8_t laser_bolt_151[186] = {
	0x00, 0x10, 0x00, 0x00, 0x12, 0x00, 0x00, 0x40, 0x00, 0x00, 0x69, 0x00, 0xff, 0xff, 0xff, 0x7f, 0x99,
	0x00, 0x41, 0x00, 0x09, 0x07, 0x00, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00, 0x00, 0x80, 0x02, 0x00, 0x00,
	0x00, 0x00, 0x80, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00,
	0x00, 0x00, 0x00, 0x80, 0x01, 0x00, 0x00, 0x00, 0x00, 0xa0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0xc0, 0x02, 0x00, 0x00, 0x0a, 0x00, 0x03, 0x01, 0xc2, 0x06, 0x00, 0x02, 0x01,
	0xc1, 0x02, 0x00, 0x00, 0x01, 0xce, 0x18, 0x00, 0x01, 0x04, 0xcd, 0x10, 0x00, 0x08, 0x05, 0xcc, 0x0c,
	0x00, 0x01, 0x06, 0xcc, 0x06, 0x00, 0x01, 0x07, 0xcc, 0x41, 0x00, 0x05, 0x04, 0x00, 0x00, 0x80, 0x02,
	0x00, 0x00, 0x00, 0x00, 0x80, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x00, 0x80,
	0x01, 0x00, 0x00, 0x00, 0x00, 0xa0, 0x00, 0x00, 0x00, 0x0a, 0x00, 0x02, 0x00, 0xc1, 0x06, 0x00, 0x01,
	0x00, 0xce, 0x12, 0x00, 0x00, 0x03, 0xcc, 0x0a, 0x00, 0x00, 0x04, 0xcc, 0x41, 0x00, 0x02, 0x01, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x06, 0x00, 0x00, 0x01, 0xce,
};

static const uint8_t laser_bolt_152[186] = {
	0x00, 0x10, 0x00, 0x00, 0x12, 0x00, 0x00, 0x40, 0x00, 0x00, 0x69, 0x00, 0xff, 0xff, 0xff, 0x7f, 0x99,
	0x00, 0x41, 0x00, 0x09, 0x07, 0x00, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00, 0x00, 0x80, 0x02, 0x00, 0x00,
	0x00, 0x00, 0x80, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00,
	0x00, 0x00, 0x00, 0x80, 0x01, 0x00, 0x00, 0x00, 0x00, 0xa0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0xc0, 0x02, 0x00, 0x00, 0x0a, 0x00, 0x03, 0x01, 0xd5, 0x06, 0x00, 0x02, 0x01,
	0xd4, 0x02, 0x00, 0x00, 0x01, 0xd3, 0x18, 0x00, 0x01, 0x04, 0xd2, 0x10, 0x00, 0x08, 0x05, 0xd1, 0x0c,
	0x00, 0x01, 0x06, 0xd1, 0x06, 0x00, 0x01, 0x07, 0xd1, 0x41, 0x00, 0x05, 0x04, 0x00, 0x00, 0x80, 0x02,
	0x00, 0x00, 0x00, 0x00, 0x80, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x00, 0x80,
	0x01, 0x00, 0x00, 0x00, 0x00, 0xa0, 0x00, 0x00, 0x00, 0x0a, 0x00, 0x02, 0x00, 0xd5, 0x06, 0x00, 0x01,
	0x00, 0xd3, 0x12, 0x00, 0x00, 0x03, 0xd1, 0x0a, 0x00, 0x00, 0x04, 0xd1, 0x41, 0x00, 0x02, 0x01, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x06, 0x00, 0x00, 0x01, 0xd3,
};

/* Per-weapon-species polygon pointers. Retail has this as an 18-entry
 * array at 0x000CD10C indexed by (ship_idx - 137). The Watcom compiler
 * folds the -137*4 into the addressing mode's displacement, producing
 * `[0xCCEE8 + ship_idx*4]` in assembly — which is why IDA's dword_CCEE8
 * label is NOT a separate object but rather the effective base of an
 * offset-array access. Same indexing convention as the projectile parameter
 * tables in laser.c. */
// GLOBAL: TIE 0xCD10C
static const uint8_t* const laser_species_poly[WEAPON_SPECIES_COUNT] = {
	/*   0 = species 137 */ laser_bolt_137,
	/*   1 = species 138 */ laser_bolt_138,
	/*   2 = species 139 */ laser_bolt_139,
	/*   3 = species 140 */ laser_bolt_140,
	/*   4 = species 141 */ laser_bolt_141,
	/*   5 = species 142 */ laser_bolt_142,
	/*   6 = species 143 */ laser_bolt_143,
	/*   7 = species 144 */ laser_bolt_144,
	/*   8 = species 145 */ laser_bolt_138, /* aliases 138 */
	/*   9 = species 146 */ laser_bolt_140, /* aliases 140 */
	/*  10 = species 147 */ laser_bolt_142, /* aliases 142 */
	/*  11 = species 148 */ laser_bolt_143, /* aliases 143 */
	/*  12 = species 149 */ laser_bolt_144, /* aliases 144 */
	/*  13 = species 150 */ NULL,           /* no poly */
	/*  14 = species 151 */ laser_bolt_151,
	/*  15 = species 152 */ laser_bolt_152,
	/*  16 = species 153 */ laser_bolt_152, /* aliases 152 */
	/*  17 = species 154 */ laser_bolt_152, /* aliases 152 */
};

/* hyperstardata + the per-star color byte at hyperstardata+0x14 are owned
 * by species.c (extern declared in species.h, included above). */

/* draw_drawcomplexobject per-frame diagnostic counters. Reset by
 * tie_updatescreen each frame after flushing. */
int dbg_dcx_total, dbg_dcx_culled_rd, dbg_dcx_null_obp;
int dbg_dcx_first_rd, dbg_dcx_first_eyez;
uint16_t dbg_dcx_first_obj = 0xFFFFu, dbg_dcx_first_ship_idx;

/* staticobjects[NUM_STATIC_OBJECTS] is owned by create.c (see tie.h). */

/* Model handles are direct pointers, so locking is an identity operation. */
static inline void* xmemhdl_lock(void* handle) { return handle; }
static inline void xmemhdl_unlock(void* handle) { (void)handle; }

/* PolyFace moved to draw.h (cross-module shared with drawpol.c). */

/* ============================================================================
 * Helpers
 * ========================================================================== */

/* Clamp a Q30-ish 32-bit value to ±0x3FFF0000 (the value the binary uses
 * to avoid overflow when shifting >>15). Matches Watcom's
 *   if (x >= 0x40000000) x = 1073676288;
 *   if (x <= -1073741824) x = -1073676288;
 * pattern that appears 30+ times in this module. */
static inline int32_t clamp_q30(int32_t v) {
	if (v >= 0x40000000)
		return 1073676288;
	if (v <= -1073741824)
		return -1073676288;
	return v;
}

/* ============================================================================
 * draw_lockshipfileptrs
 * ----------------------------------------------------------------------------
 * Resolve ship_idx → ship file pointer. Set the three module-global
 * pointers used by DRAW/FVIEW/COLLIDE/etc. Returns the byte size of the
 * LOD-records sub-table (= 6 * num_lods).
 * ========================================================================== */
// FUNCTION: TIE 0x1AF50
int draw_lockshipfileptrs(uint16_t ship_idx) {
	void* handle = species_table[ship_idx].model_handle;
	void* raw = xmemhdl_lock(handle);
	xmemhdl_unlock(handle);

	if (!raw)
		return 0;

	/* Skip the two-byte file prefix. */
	ShipModelData* base = (ShipModelData*)((uint8_t*)raw + 2);
	shipimageptr = base;
	objectblockptr = base;

	int compblock_offset = 6 * base->num_lods;
	componentblockptr = (ShipModelMesh*)&base->lod_records[base->num_lods];
	return compblock_offset;
}

/* ============================================================================
 * draw_getcomponentptr
 * ----------------------------------------------------------------------------
 * Resolve mesh by ship_base + comp_idx. Sets componentblockptr.
 * Returns &mesh + mesh.render_offset (per-mesh detail-LOD table base).
 * ========================================================================== */
// FUNCTION: TIE 0x1AFB4
ShipMeshLOD* draw_getcomponentptr(ShipModelData* ship_base, uint16_t comp_idx) {
	ShipModelMesh* mesh_base = (ShipModelMesh*)&ship_base->lod_records[ship_base->num_lods];
	ShipModelMesh* mesh = &mesh_base[comp_idx];
	componentblockptr = mesh;
	return (ShipMeshLOD*)((uint8_t*)mesh + mesh->render_offset);
}

/* ============================================================================
 * draw_getcompdetailptr
 * ----------------------------------------------------------------------------
 * Pick the polygon-detail pointer for a mesh at the given base z.
 * Anchors on comp.pos_xyz (if has_position) or comp.center_*; rotates via
 * rotworldeye*3; clamps to ±0x40000000; calls draw_getdetailptr.
 * Restores shipdetailpolycnt on exit (drawcraft may have temporarily
 * raised it).
 * ========================================================================== */
// FUNCTION: TIE 0x1AFF0
const uint16_t* draw_getcompdetailptr(ShipModelMesh* comp, int base_z) {
	uint16_t saved_polycnt = shipdetailpolycnt;

	int16_t side, fwd, up;
	if (comp->has_position) {
		side = comp->pos_side;
		fwd = comp->pos_fwd;
		up = comp->pos_up;
		shipdetailpolycnt = 4;
	} else {
		side = comp->center_side;
		fwd = comp->center_fwd;
		up = comp->center_up;
	}

	int rel_z = clamp_q30(rotworldeyeC3 * up + rotworldeyeB3 * fwd + rotworldeyeA3 * side);

	ShipMeshLOD* lod = (ShipMeshLOD*)((uint8_t*)comp + comp->render_offset);
	const uint16_t* result = draw_getdetailptr(lod, (rel_z >> 16) + base_z);
	shipdetailpolycnt = saved_polycnt;
	return result;
}

/* ============================================================================
 * draw_getdetailptr
 * ----------------------------------------------------------------------------
 * Walk per-mesh LOD dispatch table (array of {int32 distance, u16 offset},
 * 6 bytes each). Pick the polygon header for z_threshold.
 *
 * Detail mode (shipdetailvalue — note the INVERTED axis vs the UI:
 * negative = high detail, positive = low; see tie.h's extern doc):
 *   -1 (HIGH detail) : halve z_threshold first so the walker stops
 *                      at a CLOSER-range (finer) LOD record. Treat
 *                      remainder of the function as the normal path.
 *    0 (NORMAL)      : plain walk -- return selected record's offset.
 *   >0 (LOW detail)  : if selected record is the INT_MAX terminator
 *                      or its polygon header is valid (type byte
 *                      &0xFE != 0x40 and numpolys <= shipdetailpolycnt),
 *                      return it. Otherwise dip into the NEXT record
 *                      (a coarser LOD). The fallback is a perf-saving
 *                      coarsening, NOT an upgrade.
 * ========================================================================== */
// FUNCTION: TIE 0x1B0A8
const uint16_t* draw_getdetailptr(ShipMeshLOD* lod_table, int z_threshold) {
	int detail_mode = (uint16_t)shipdetailvalue;
	if (shipdetailvalue == -1) {
		z_threshold >>= 1;
		detail_mode = 0;
	}

	ShipMeshLOD* p = lod_table;
	while (z_threshold > p->distance)
		++p;

	if (detail_mode <= 0)
		return (const uint16_t*)((uint8_t*)p + p->offset);

	/* High-detail mode: probe the polygon header for validity. */
	if (p->distance == 0x7FFFFFFF)
		return (const uint16_t*)((uint8_t*)p + p->offset);

	const uint8_t* poly_header = (const uint8_t*)p + p->offset;
	if ((poly_header[0] & 0xFE) != 0x40 && poly_header[4] <= (int)shipdetailpolycnt)
		return (const uint16_t*)((uint8_t*)p + p->offset);

	/* Probe failed -- use the higher-detail variant in p[1]. */
	return (const uint16_t*)((uint8_t*)&p[1] + p[1].offset);
}

/* ============================================================================
 * draw_drawlaser
 * ----------------------------------------------------------------------------
 * Draw a single laser bolt (FlightObject as a single polygon). Retail
 * bakes the per-weapon-species polygon data directly into dseg via
 * laser_species_poly[ship_idx]; the lockshipfileptrs fallback is a
 * defensive branch that shipped data never triggers (laser species have
 * flags & 2 == 0, so FEDISKIO_loadspecies skips them — by design).
 * ========================================================================== */
// FUNCTION: TIE 0x1BAB4
void draw_drawlaser(uint16_t laser_obj_idx) {
	parentobject = laser_obj_idx;
	uint16_t ship_idx = objects[laser_obj_idx].ship_idx;

	ShipMeshLOD* poly_table = (ShipMeshLOD*)laser_species_poly[laser_species_idx(ship_idx)];
	if (!poly_table) {
		draw_lockshipfileptrs(ship_idx);
		poly_table = (ShipMeshLOD*)((uint8_t*)componentblockptr + componentblockptr->render_offset);
	}

	int eyex = objecteyex, eyey = objecteyey, eyez = objecteyez;
	const uint16_t* poly = draw_getdetailptr(poly_table, objecteyez);
	drawpol_drawpolyobject(poly, eyex, eyey, eyez);
}

// FUNCTION: TIE98 0x417EC0
void draw_drawlaser_tie98(uint16_t laser_obj_idx) {
	FlightObject* object = &objects[laser_obj_idx];
	parentobject = laser_obj_idx;
	const int32_t camera_x = camera.x - object->world_x;
	const int32_t camera_y = camera.y - object->world_y;
	const int32_t camera_z = camera.z - object->world_z;
	const int32_t up_dot = (int32_t)(((int64_t)camera_x * object->up_x + (int64_t)camera_y * object->up_y +
									  (int64_t)camera_z * object->up_z) >>
									 15);
	const int32_t side_dot =
		(int32_t)(((int64_t)camera_x * object->side_x + (int64_t)camera_y * object->side_y +
				   (int64_t)camera_z * object->side_z) >>
				  15);
	const int16_t saved_roll = object->roll;
	object->roll += (int16_t)(trig2_arctan(up_dot, side_dot) - 0x4000);
	object->orient_dirty = 1;
	fview_newcalcrotate(object->roll, object->heading, object->pitch, 0, object);
	FlightModel_Draw_Object(object);
	object->roll = saved_roll;
	object->orient_dirty = 1;
}

/* Sizes of each laser_bolt_* blob, kept in lock-step with the static
 * arrays above so the HD path can bound-check while parsing. */
static const size_t laser_species_poly_sizes[WEAPON_SPECIES_COUNT] = {
	sizeof laser_bolt_137, /* 137 */
	sizeof laser_bolt_138, /* 138 */
	sizeof laser_bolt_139, /* 139 */
	sizeof laser_bolt_140, /* 140 */
	sizeof laser_bolt_141, /* 141 */
	sizeof laser_bolt_142, /* 142 */
	sizeof laser_bolt_143, /* 143 */
	sizeof laser_bolt_144, /* 144 */
	sizeof laser_bolt_138, /* 145 (aliases 138) */
	sizeof laser_bolt_140, /* 146 (aliases 140) */
	sizeof laser_bolt_142, /* 147 (aliases 142) */
	sizeof laser_bolt_143, /* 148 (aliases 143) */
	sizeof laser_bolt_144, /* 149 (aliases 144) */
	0,                     /* 150 (NULL slot) */
	sizeof laser_bolt_151, /* 151 */
	sizeof laser_bolt_152, /* 152 */
	sizeof laser_bolt_152, /* 153 (aliases 152) */
	sizeof laser_bolt_152, /* 154 (aliases 152) */
};

const void* tie_laser_species_poly(uint16_t species_idx, size_t* out_size) {
	if (species_idx < WEAPON_SPECIES_BASE || species_idx >= WEAPON_SPECIES_BASE + WEAPON_SPECIES_COUNT) {
		if (out_size)
			*out_size = 0;
		return NULL;
	}
	unsigned k = laser_species_idx(species_idx);
	const void* blob = laser_species_poly[k];
	if (out_size)
		*out_size = blob ? laser_species_poly_sizes[k] : 0;
	return blob;
}

/* ============================================================================
 * draw_drawhyperstar
 * ----------------------------------------------------------------------------
 * Hyperspace starburst sprite at eye-space objecteyex/y/z. parentobject is
 * tagged OBJ_REF_STATIC_BASE + star_idx (the "static/flat-poly" slice of
 * the obj-ref namespace; see tie.h). byte_DC3AC = (star_idx & 3) - 4.
 * Saves/restores flatobjnum so the caller's flat-poly ring is unaffected.
 * ========================================================================== */
// FUNCTION: TIE 0x1BB28
void draw_drawhyperstar(int16_t star_idx) {
	parentobject = (uint16_t)(star_idx + OBJ_REF_STATIC_BASE);
	hyperstardata[0x14] = (uint8_t)((star_idx & 3) - 4);
	uint16_t saved = flatobjnum;
	drawpol_drawpolyobject((const uint16_t*)hyperstardata, objecteyex, objecteyey, objecteyez);
	flatobjnum = saved;
}

// GLOBAL: TIE98 0x4E3D10
static Vec3f g_hyperspaceStreakQuadVertices[4] = {
	{ 64.0f, 0.0f, 0.0f },
	{ 64.0f, -256.0f, 0.0f },
	{ -64.0f, -256.0f, 0.0f },
	{ -64.0f, 0.0f, 0.0f },
};

// GLOBAL: TIE98 0x4E3D58
static OptTexCoordTIE98 g_hyperspaceStreakQuadTexCoords[4] = {
	{ 1.0f, 0.0f },
	{ 1.0f, 1.0f },
	{ 0.0f, 1.0f },
	{ 0.0f, 0.0f },
};

// GLOBAL: TIE98 0x4E3D90
static Vec3f g_hyperspaceStreakQuadVertNormals[1] = {
	{ 0.0f, 0.0f, 1.0f },
};

typedef struct HyperspaceStreakQuadFaceDataTIE98 {
	int edgeCount;
	FaceRecordTIE98 faces[1];
	Vec3f faceNormals[1];
	FaceTextureGradientsTIE98 faceTexturing[1];
} HyperspaceStreakQuadFaceDataTIE98;

// GLOBAL: TIE98 0x4E3DB8
static HyperspaceStreakQuadFaceDataTIE98 g_hyperspaceStreakQuadFaceData = {
	4,
	{
		{
			{ 0, 1, 2, 3 },
			{ 0, 1, 2, 3 },
			{ 0, 0, 0, 0 },
			{ 1, 2, 3, 0 },
		},
	},
	{ { 0.0f, 0.0f, 1.0f } },
	{ { { 1.0f, 1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f } } },
};

static Tie98OptNode g_hyperspaceStreakQuadVertsNode = {
	NULL, TIE98_OPT_NODE_MESH_VERTICES, 0, NULL, 4, g_hyperspaceStreakQuadVertices,
};
static Tie98OptNode g_hyperspaceStreakQuadTexCoordsNode = {
	NULL, TIE98_OPT_NODE_TEXTURE_COORDINATES, 0, NULL, 4, g_hyperspaceStreakQuadTexCoords,
};
static Tie98OptNode g_hyperspaceStreakQuadVertNormalsNode = {
	NULL, TIE98_OPT_NODE_VERTEX_NORMALS, 0, NULL, 1, g_hyperspaceStreakQuadVertNormals,
};
static Tie98OptNode g_hyperspaceStreakQuadFaceNode = {
	NULL, TIE98_OPT_NODE_FACE_DATA, 0, NULL, 1, &g_hyperspaceStreakQuadFaceData,
};
static Tie98OptNode* g_hyperspaceStreakQuadRootChildren[4] = {
	&g_hyperspaceStreakQuadVertsNode,
	&g_hyperspaceStreakQuadTexCoordsNode,
	&g_hyperspaceStreakQuadVertNormalsNode,
	&g_hyperspaceStreakQuadFaceNode,
};
static Tie98OptNode g_hyperspaceStreakQuadRootNode = {
	NULL, TIE98_OPT_NODE_GROUP, 4, g_hyperspaceStreakQuadRootChildren, 4, g_hyperspaceStreakQuadRootChildren,
};
static Tie98OptNode* g_hyperspaceStreakQuadRootNodes[1] = {
	&g_hyperspaceStreakQuadRootNode,
};

// GLOBAL: TIE98 0x4E3E68
static Tie98OptimizedPolyObject g_hyperspaceModelHeaderPatch = {
	0, 1, g_hyperspaceStreakQuadRootNodes, NULL, 0, 0,
};

// GLOBAL: TIE98 0x591E30
static uint32_t g_tie98HyperspaceStreakLength = 32256;

/* PORT: the simulation still runs TIE95 ANIM_dohyperspace. Both versions
 * use the same phase timer, so derive the TIE98 animation global at the
 * renderer-selection boundary without changing either recovered body. */
void draw_sync_tie98_hyperstar_state(void) {
	const uint32_t outbound_start = 0x49C;
	const uint32_t outbound_end = 0x588;
	const uint32_t inbound_shrink_start = 0x84C;
	const uint32_t minimum_length = 0x8200;
	const uint32_t maximum_length = 32256 + 224 * (outbound_end - outbound_start);

	if (hyperspaceflag == 3) {
		uint32_t ticks = hyperticks;
		if (ticks < outbound_start)
			ticks = outbound_start;
		if (ticks > outbound_end)
			ticks = outbound_end;
		g_tie98HyperspaceStreakLength = 32256 + 224 * (ticks - outbound_start);
	} else if (hyperspaceflag == 5) {
		if (hyperticks <= inbound_shrink_start) {
			g_tie98HyperspaceStreakLength = maximum_length;
		} else {
			const uint32_t elapsed = hyperticks - inbound_shrink_start;
			const uint32_t reduction = 224 * elapsed;
			g_tie98HyperspaceStreakLength =
				reduction < maximum_length - minimum_length ? maximum_length - reduction : minimum_length;
		}
	}
}

// FUNCTION: TIE98 0x42F990 DRAW_drawhyperstar
void draw_drawhyperstar_tie98(int16_t star_idx) {
	const int saved_bilinear = g_bilinearEnabled;
	FlightObject saved_object = objects[0];
	const Tie98OptimizedPolyObject* saved_model_override = g_flightModelOverride;

	g_hyperspaceStreakQuadVertices[1].y = (float)(g_tie98HyperspaceStreakLength >> 1);
	g_hyperspaceStreakQuadVertices[2].y = g_hyperspaceStreakQuadVertices[1].y;
	g_bilinearEnabled = 0;

	objects[0].world_x = (int32_t)staticobjects[star_idx].world_x << 8;
	objects[0].world_y = (int32_t)staticobjects[star_idx].world_y << 8;
	objects[0].world_z = (int32_t)staticobjects[star_idx].world_z << 8;
	parentobject = 0;
	objects[0].ship_idx = 137;
	objects[0].genus = GENUS_PROJECTILE_NPC;
	objects[0].roll =
		(int16_t)(trig2_arctan(objects[0].world_z - camera.z, objects[0].world_x - camera.x) + 0x4000);
	objects[0].pitch = 0;
	objects[0].heading = 0x4000;
	objects[0].orient_dirty = 1;
	fview_newcalcrotate(objects[0].roll, 0x4000, 0, 0, &objects[0]);
	g_flightModelOverride = &g_hyperspaceModelHeaderPatch;
	FlightModel_Draw_Object(&objects[0]);
	g_flightModelOverride = saved_model_override;
	g_bilinearEnabled = saved_bilinear;
	objects[0] = saved_object;
}

/* ============================================================================
 * draw_drawbackdropimage
 * ----------------------------------------------------------------------------
 * Rotated/scaled backdrop blit (planet, large-distance ship sprite).
 * Reads species[ship_idx].model_handle for the bitmap blob and
 * species[ship_idx].bitmap_data for the palette remap.
 * ========================================================================== */
// FUNCTION: TIE 0x1BB70
uint16_t draw_drawbackdropimage(uint16_t ship_idx, int16_t screen_x, int16_t screen_y, uint16_t angle) {
	reverseflag = 1;
	worldz = 0x100000;
	void* handle = species_table[ship_idx].model_handle;
	const uint8_t* bitmap_base = (const uint8_t*)xmemhdl_lock(handle);
	xmemhdl_unlock(handle);
	if (!bitmap_base)
		return 0;

	/* Retail bitmaps use a two-level offset to their palette and image data. */
	uint32_t tbl_off = *(const uint32_t*)(bitmap_base + 16);
	uint32_t sub_off = *(const uint32_t*)(bitmap_base + tbl_off);
	const uint8_t* v9 = bitmap_base + sub_off;

	rotscale_prepare_fastdraw(angle);
	rotscale_prepare_color((const char*)v9);
	return rotscale_rotate_scale_image(screen_x, screen_y, 0x100, v9);
}

// FUNCTION: TIE98 0x417FF0 DRAW_drawbackdropimage
uint16_t draw_drawbackdropimage_tie98(uint16_t ship_idx, int16_t screen_x, int16_t screen_y, uint16_t angle) {
	reverseflag = 1;
	worldz = 0x100000;
	objecteyez = 0x7FFFFFFF;
	void* handle = species_table[ship_idx].model_handle;
	const uint8_t* bitmap_base = (const uint8_t*)xmemhdl_lock(handle);
	xmemhdl_unlock(handle);
	if (!bitmap_base)
		return 0;

	const uint32_t table_offset = *(const uint32_t*)(bitmap_base + 16);
	const uint32_t image_offset = *(const uint32_t*)(bitmap_base + table_offset);
	const uint8_t* image = bitmap_base + image_offset;
	if (g_useHardware3D) {
		RenderQuad_DrawRotatedSprite(angle, screen_x, screen_y, 0x100, image);
		return 0;
	}
	rotscale_prepare_fastdraw(angle);
	rotscale_prepare_color((const char*)image);
	return rotscale_rotate_scale_image(screen_x, screen_y, 0x100, image);
}

/* ============================================================================
 * draw_gettreeorder
 * ----------------------------------------------------------------------------
 * Recursive BSP-tree walk in painter's order (back-to-front).
 *
 * Node layout (18 bytes, used as int16[9]):
 *   [+0]  normal_x  [+2]  normal_y  [+4]  normal_z
 *   [+6]  center_x  [+8]  center_y  [+10] center_z
 *   [+12] left_offset_hi (i16; 0 = leaf)
 *   [+14] right_offset_hi (i16; at leaves, this is the mesh index)
 *
 * Branch step:
 *   1. Compute (relative*) - center_*, with shift correction from
 *      relativeshift (shared exponent).
 *   2. Dot with plane normal. If dot < 0: recurse RIGHT first, walk LEFT.
 *      Else recurse LEFT first, walk RIGHT.
 *
 * Leaf step:
 *   Dereferences componentblockptr[mesh_idx]. Recomputes eyez via
 *   pos_xyz (if has_position). Probes detail header for INT_MAX skip
 *   marker. If eyez <= mesh.draw_distance, appends mesh_idx to comp[].
 * ========================================================================== */
// FUNCTION: TIE 0x1B414
ShipModelMesh* draw_gettreeorder(int* bsp_node) {
	BSPNode* node = (BSPNode*)bsp_node;
	BSPNode* leaf_node = node;

	while (node->left_off != 0) { /* nonzero = branch */
		leaf_node = node;
		int pt_side, pt_fwd, pt_up;
		if (relativeshift >= 0) {
			if (relativeshift == 0) {
				pt_side = (int16_t)(relativex - node->center_x);
				pt_fwd = (int16_t)(relativey - node->center_y);
				pt_up = (int16_t)(relativez - node->center_z);
			} else {
				pt_side = relativex - (node->center_x >> relativeshift);
				pt_fwd = relativey - (node->center_y >> relativeshift);
				pt_up = relativez - (node->center_z >> relativeshift);
			}
		} else {
			int sh = -(int8_t)relativeshift;
			pt_side = (relativex >> sh) - node->center_x;
			pt_fwd = (relativey >> sh) - node->center_y;
			pt_up = (relativez >> sh) - node->center_z;
		}

		int plane_dot = clamp_q30((int16_t)pt_up * node->normal_z + (int16_t)pt_fwd * node->normal_y +
								  (int16_t)pt_side * node->normal_x);

		int next_off;
		if (((plane_dot >> 15) & 0x8000) != 0) {
			/* Camera on negative side: recurse RIGHT, tail-walk LEFT. */
			draw_gettreeorder((int*)((uint8_t*)node + node->right_off));
			next_off = node->left_off;
		} else {
			draw_gettreeorder((int*)((uint8_t*)node + node->left_off));
			next_off = node->right_off;
		}
		node = (BSPNode*)((uint8_t*)node + next_off);
	}

	leaf_node = node;
	/* At leaves, right_off field holds the mesh index. */
	int16_t leaf_mesh_idx = node->right_off;
	ShipModelMesh* mesh = &componentblockptr[leaf_mesh_idx];
	int comp_eyez = objecteyez;

	if (mesh->has_position) {
		int rel = clamp_q30(rotworldeyeC3 * mesh->pos_up + rotworldeyeB3 * mesh->pos_fwd +
							rotworldeyeA3 * mesh->pos_side);
		comp_eyez = (rel >> 16) + objecteyez;

		if (shipdetailvalue == 1) {
			ShipMeshLOD* probe = (ShipMeshLOD*)((uint8_t*)mesh + mesh->render_offset);
			if (probe->distance == 0x7FFFFFFF) {
				/* Skip-marker: prune the leaf entirely. */
				return mesh;
			}
		}
	}

	if (shipdetailvalue == -1)
		comp_eyez >>= 1;

	if (comp_eyez <= mesh->draw_distance)
		comp[numberofcomp++] = (uint16_t)leaf_node->right_off;
	return mesh;
}

/* ============================================================================
 * draw_drawcomplexobject
 * ----------------------------------------------------------------------------
 * Top-level entry for rendering a multi-mesh BSP-tree object.
 *
 * 1. Resolve ship_idx (mesh objects: objects[i].field_0[4]; static
 *    objects >= OBJ_REF_STATIC_BASE: staticobjects[idx-OBJ_REF_STATIC_BASE].species.
 * 2. drawpol_setmarkingcolors for decals.
 * 3. draw_lockshipfileptrs to set object/component pointers.
 * 4. Cull: if objecteyez >= ShipModelData.render_distance, bail.
 * 5. Walk lod_records to pick the BSP root for this distance.
 * 6. create_getworldposition; compute rel-vec; bit-scale into
 *    relativeshift.
 * 7. Rotate to ship local frame via craft{S,f,U}.
 * 8. relativeshift -= model_scale_shift.
 * 9. numberofcomp=0; draw_gettreeorder(bsp_root + 2 byte header skip);
 *    draw_drawcraft.
 * 10. Restore decal palette.
 * ========================================================================== */
// FUNCTION: TIE 0x1B12C
int draw_drawcomplexobject(int obj_idx) {
	uint16_t obj_idx_u16 = (uint16_t)obj_idx;
	uint16_t ship_idx;

	if (obj_idx_u16 < OBJ_REF_STATIC_BASE) {
		ship_idx = objects[obj_idx_u16].ship_idx;
		drawpol_setmarkingcolors(objects[obj_idx_u16].decal_color);
	} else {
		/* Static slot: decode via (ref - OBJ_REF_STATIC_BASE). */
		uint16_t static_idx = obj_idx_u16 - OBJ_REF_STATIC_BASE;
		ship_idx = (static_idx < NUM_STATIC_OBJECTS) ? staticobjects[static_idx].species : 0;
	}
	draw_lockshipfileptrs(ship_idx);

	/* Debug: per-frame counters for draw_drawcomplexobject. The first
	 * call each frame also prints render_distance / eyez / bound_hwidth
	 * for the first craft slot (obj < OBJ_REF_STATIC_BASE) we see. */
	{

		dbg_dcx_total++;
		if (!objectblockptr)
			dbg_dcx_null_obp++;
		else if (objecteyez >= objectblockptr->render_distance)
			dbg_dcx_culled_rd++;
		if (dbg_dcx_first_obj == 0xFFFFu && obj_idx_u16 < OBJ_REF_STATIC_BASE) {
			dbg_dcx_first_obj = obj_idx_u16;
			dbg_dcx_first_ship_idx = ship_idx;
			dbg_dcx_first_rd = objectblockptr ? (int)objectblockptr->render_distance : -1;
			dbg_dcx_first_eyez = (int)objecteyez;
		}
	}

	if (!objectblockptr || objecteyez >= objectblockptr->render_distance)
		return drawpol_setmarkingcolors(0), 0;

	/* Walk the ship-level LOD dispatch table picking the BSP root for
	 * the current eye-z distance. Each LODRecord is 6 bytes. */
	_Static_assert(sizeof(LODRecord) == 6, "LODRecord must be 6 bytes");
	_Static_assert(sizeof(ShipModelMesh) == 64, "ShipModelMesh must be 64 bytes");
	LODRecord* lod = objectblockptr->lod_records;
	uint8_t* bsp_root = (uint8_t*)lod;
	for (uint16_t i = 0; i < objectblockptr->num_lods; ++i) {
		bsp_root = (uint8_t*)&lod[i] + lod[i].bsp_offset;
		if ((int32_t)lod[i].z_max < objecteyez)
			break;
	}

	create_getworldposition(obj_idx_u16, 0);

	int dx_scaled = 2 * (camera.x - worldlocx);
	int dy_scaled = 2 * (camera.y - worldlocy);
	int dz_scaled = 2 * (camera.z - worldlocz);

	int dx_abs = (int16_t)((camera.x - worldlocx) >> 15);
	int dy_abs = (int16_t)((camera.y - worldlocy) >> 15);
	int dz_abs = (int16_t)((camera.z - worldlocz) >> 15);
	if ((((camera.x - worldlocx) >> 15) & 0x8000) != 0)
		dx_abs = -dx_abs;
	if ((dy_abs & 0x8000) != 0)
		dy_abs = -dy_abs;
	if ((dz_abs & 0x8000) != 0)
		dz_abs = -dz_abs;

	uint16_t dx_abs_w = (uint16_t)(2 * dx_abs);
	uint16_t dy_abs_w = (uint16_t)(2 * dy_abs);
	uint16_t dz_abs_w = (uint16_t)(2 * dz_abs);
	relativeshift = -1;
	do {
		do {
			dx_abs_w >>= 1;
			dy_abs_w >>= 1;
			dz_abs_w >>= 1;
			dx_scaled >>= 1;
			dy_scaled >>= 1;
			dz_scaled >>= 1;
			++relativeshift;
		} while (dx_abs_w);
	} while (dy_abs_w || dz_abs_w);

	int rel_side =
		clamp_q30((int16_t)dz_scaled * craftS3 + (int16_t)dy_scaled * craftS2 + (int16_t)dx_scaled * craftS1);
	relativex = (int16_t)(rel_side >> 15);

	int rel_fwd =
		clamp_q30((int16_t)dz_scaled * craftf3 + (int16_t)dy_scaled * craftf2 + (int16_t)dx_scaled * craftf1);
	relativey = -(int16_t)(rel_fwd >> 15);

	int rel_up =
		clamp_q30((int16_t)dz_scaled * craftU3 + (int16_t)dy_scaled * craftU2 + (int16_t)dx_scaled * craftU1);
	relativez = (int16_t)(rel_up >> 15);

	relativeshift -= (int16_t)(int8_t)objectblockptr->model_scale_shift;
	numberofcomp = 0;
	draw_gettreeorder((int*)(bsp_root + 2));
	draw_drawcraft(obj_idx_u16, ship_idx, (int16_t)dz_scaled);

	return drawpol_setmarkingcolors(0), 0;
}

/* Draw visible craft components with their articulation, markings, and target
 * highlighting. Critically damaged fuselages may also emit a lightning
 * billboard. Restores currenttarget before returning its saved value. */
// FUNCTION: TIE 0x1B690
int draw_drawcraft(int obj_idx, uint32_t ship_flag, int eyez) {
	uint16_t obj_idx_u16 = (uint16_t)obj_idx;
	int saved_currenttarget = currenttarget;
	int16_t bolt_angle_cached = 0;
	int bolt_angle_set = 0;

	parentobject = obj_idx_u16;
	highlightcolor = 0;
	if (obj_idx_u16 == bluetarget) {
		currenttarget = obj_idx_u16;
		highlightcolor = 1;
	}

	draw_lockshipfileptrs(ship_flag);
	/* model_scale_shift==2 routes drawpol through transfm2_geteyecoordsS2
	 * (>> 14 instead of >> 16, 4× eye-space contribution) by pushing
	 * parentobject's HIBYTE past 0x50 — see drawpol.c:1265/1331-1334.
	 * Capital-class ships (e.g. VSD) ship with this value. */
	if (objectblockptr && (int8_t)objectblockptr->model_scale_shift == 2) {
		parentobject = (parentobject & 0x00FF) | ((parentobject + 0x7000) & 0xFF00);
		if (mission.train_craft_type)
			currenttarget = (currenttarget & 0x00FF) | ((currenttarget + 0x7000) & 0xFF00);
	}

	for (uint16_t comp_iter = 0; comp_iter < numberofcomp; ++comp_iter) {
		uint16_t comp_idx = comp[comp_iter];
		ShipModelMesh* mesh = &componentblockptr[comp_idx];
		int16_t mesh_type = (int16_t)mesh->mesh_type;
		solidindex = comp_idx;

		if (highlightcolor == 2)
			highlightcolor = 0;

		/* Sub-component target highlight check. */
		if (comp_idx == currenttargetcomp) {
			if (highlightcolor)
				goto label20;
			highlightcolor = 2;
			goto label20;
		}
		if (currenttargetcomp < (int)objectblockptr->num_meshes) {
			if (mesh->has_position > 1 || (mesh->has_position == 1 && mesh_type == 1 /*MESH_MainHull*/)) {
				ShipModelMesh* tgt_comp = &componentblockptr[currenttargetcomp];
				if (mesh->has_position == tgt_comp->has_position &&
					mesh_type == (int16_t)tgt_comp->mesh_type && !highlightcolor) {
					highlightcolor = 2;
				}
			}
		}
	label20:;
		int16_t rot_angle = 0;
		if (obj_idx_u16 < NUM_CRAFTS) {
			if (craftptr->mesh_state[comp_idx] != MESH_STATE_VISIBLE)
				continue; /* hidden / blown off -- skip */
			rot_angle = craftptr->mesh_rotation[comp_idx];
			if ((mesh->rotation_offset || mission.train_craft_type) && rot_angle)
				fview_componentrotation((int16_t)(rot_angle << 8), mesh);
			else
				rot_angle = 0;
		}

		uint8_t saved_drawmarkingsflag = drawmarkingsflag;
		/* Species 17 wing meshes hide their insignia for non-friendly
		 * craft. Binary @0x1b884: `a2 == 17` where a2 is ship_flag
		 * (second arg, DX). Mirrors the parallel override in
		 * ANIM_drawverysimpleobject at anim.c:407. */
		if (ship_flag == 17 && mesh_type == 2 /*MESH_Wing*/ && obj_idx_u16 < OBJ_REF_STATIC_BASE)
			drawmarkingsflag = (objects[obj_idx_u16].side == 0);

		uint16_t saved_polycnt = shipdetailpolycnt;
		uint16_t saved_curtarget = currenttarget;
		if (currenttarget != 0xFFFFu && highlightcolor == 2 && (currenttarget & 0x200) != 0) {
			currenttarget = parentobject;
		}

		int eyez_arg = objecteyez;
		int eyey_arg = objecteyey;
		int eyex_arg = objecteyex;
		const uint16_t* poly = draw_getcompdetailptr(mesh, objecteyez);
		drawpol_drawpolyobject(poly, eyex_arg, eyey_arg, eyez_arg);

		shipdetailpolycnt = saved_polycnt;
		drawmarkingsflag = saved_drawmarkingsflag;
		currenttarget = saved_curtarget;

		if (rot_angle)
			fview_restorerotation();

		/* Lightning arc on Fuselage (mesh_type==3) for live craft. */
		if (mesh_type == 3 /*MESH_Fuselage*/ && obj_idx_u16 < NUM_CRAFTS) {
			/* mesh_state[num_meshes] is the byte just past the per-mesh
			 * state range; the binary overlays it as the lightning anim
			 * frame index (0..24 indexes lightning[25]). */
			uint8_t last_state = craftptr->mesh_state[objectblockptr->num_meshes];
			if (last_state >= 25)
				continue;
			AnimOp lightning_obj = lightning[last_state];
			if (!animop_is_bitmap(lightning_obj))
				continue; /* on a header/jump frame -- no bolt this tick */
			if (!bolt_angle_set) {
				int A3_abs = rotworldeyeA3 < 0 ? -rotworldeyeA3 : rotworldeyeA3;
				int B3_abs = rotworldeyeB3 < 0 ? -rotworldeyeB3 : rotworldeyeB3;
				int arc_dx, arc_dy;
				if (A3_abs >= B3_abs) {
					arc_dx = rotworldeyeB1;
					arc_dy = rotworldeyeB2;
				} else {
					arc_dx = rotworldeyeA1;
					arc_dy = rotworldeyeA2;
				}
				if (arc_dx >= 0)
					bolt_angle_cached = -trig2_arctan(arc_dy, arc_dx);
				else
					bolt_angle_cached = trig2_arctan(arc_dy, -arc_dx);
				bolt_angle_set = 1;
			}
			int16_t roll_val = objects[obj_idx_u16].roll;
			uint32_t screen_x_full = transfm2_getscreencoordx(objecteyex, objecteyez);
			int16_t screen_x_w = (int16_t)screen_x_full;
			int screen_x_h = (int)screen_x_full >> 16;
			int16_t bolt_angle = (int16_t)(roll_val + bolt_angle_cached);
			if (screen_x_h <= 0 && screen_x_h >= -1) {
				uint32_t screen_y_full = transfm2_getscreencoordy(objecteyey, objecteyez);
				int sy_h = (int)screen_y_full >> 16;
				if (sy_h <= 0 && sy_h >= -1) {
					int half_pd = (int)pixelsdeep >> 1;
					anim_add_bitmap_draw(parentobject, lightning_obj, 256, screen_x_w,
										 (int16_t)(half_pd - ((int)screen_y_full - half_pd)), objecteyez,
										 bolt_angle);
					/* SNAPSHOT capture — does NOT affect classic render.
					 * Same calcscale call the engine's anim_draw_bitmap
					 * would do (with damage_factor = 256 since lightning
					 * passes a fixed scale to anim_add_bitmap_draw).
					 * Bolt is anchored at parent craft world origin in
					 * anim_draw_bitmap; emit reads world_*_prev. */
					uint8_t lb_sp = animop_bitmap_species(lightning_obj);
					uint16_t lb_bw = species_table[lb_sp].bound_hwidth;
					uint16_t lb_psc = (uint16_t)rotscale_calcscale(objecteyez, lb_bw, 256);
					TieBillboardCapture_Lightning(obj_idx_u16, lightning_obj, lb_psc, lb_bw, bolt_angle);
				}
			}
		}
	}

	currenttarget = saved_currenttarget;
	return saved_currenttarget;
}

// FUNCTION: TIE98 0x417C40 DRAW_drawcraft
static void draw_drawcraft_tie98(uint16_t object_ref, uint16_t model_type) {
	const uint16_t saved_current_target = currenttarget;
	int16_t bolt_angle = 0;
	int bolt_angle_set = 0;

	parentobject = object_ref;
	highlightcolor = 0;
	if (object_ref == bluetarget) {
		currenttarget = object_ref;
		highlightcolor = 1;
	}

	const int mesh_count = modelmesh_getcount(model_type);
	for (int mesh_index = 0; mesh_index < mesh_count; ++mesh_index) {
		solidindex = (int16_t)mesh_index;
		const int mesh_type = modelmesh_gettype(model_type, mesh_index);
		if (highlightcolor == 2)
			highlightcolor = 0;
		if (currenttargetcomp == mesh_index && highlightcolor == 0)
			highlightcolor = 2;

		if (object_ref >= NUM_CRAFTS)
			continue;
		CraftData* craft = objects[object_ref].craft_ptr;
		if (craft->mesh_state[mesh_index] != MESH_STATE_VISIBLE || mesh_type != TIE_MESH_FUSELAGE)
			continue;

		const AnimOp lightning_op = lightning[craft->mesh_state[mesh_count]];
		if (!animop_is_bitmap(lightning_op))
			continue;
		if (!bolt_angle_set) {
			int axis_x;
			int axis_y;
			const int abs_a3 = rotworldeyeA3 < 0 ? -rotworldeyeA3 : rotworldeyeA3;
			const int abs_b3 = rotworldeyeB3 < 0 ? -rotworldeyeB3 : rotworldeyeB3;
			if (abs_a3 >= abs_b3) {
				axis_x = rotworldeyeB1;
				axis_y = rotworldeyeB2;
			} else {
				axis_x = rotworldeyeA1;
				axis_y = rotworldeyeA2;
			}
			bolt_angle = axis_x >= 0 ? (int16_t)-trig2_arctan(axis_y, axis_x) : trig2_arctan(axis_y, -axis_x);
			bolt_angle_set = 1;
		}

		const uint32_t screen_x = (uint32_t)transfm2_getscreencoordx(objecteyex, objecteyez);
		const int screen_x_high = (int32_t)screen_x >> 16;
		if (screen_x_high > 0 || screen_x_high < -1)
			continue;
		const uint32_t screen_y = (uint32_t)transfm2_getscreencoordy(objecteyey, objecteyez);
		const int screen_y_high = (int32_t)screen_y >> 16;
		if (screen_y_high > 0 || screen_y_high < -1)
			continue;
		const int half_height = pixelsdeep >> 1;
		const int16_t rotation = (int16_t)(objects[object_ref].roll + bolt_angle);
		anim_add_bitmap_draw(parentobject, lightning_op, 0x100, (int16_t)screen_x,
							 (int16_t)(2 * half_height - (int32_t)screen_y), objecteyez, rotation);
		const uint8_t bitmap_species = animop_bitmap_species(lightning_op);
		const uint16_t bound_hwidth = species_table[bitmap_species].bound_hwidth;
		const uint16_t pixel_scale = (uint16_t)rotscale_calcscale(objecteyez, bound_hwidth, 0x100);
		TieBillboardCapture_Lightning(object_ref, lightning_op, pixel_scale, bound_hwidth, rotation);
	}
	currenttarget = saved_current_target;
}

// FUNCTION: TIE98 0x417BE0
void draw_process_object_components_tie98(uint16_t object_ref) {
	const uint16_t model_type = object_ref >= OBJ_REF_STATIC_BASE
									? staticobjects[object_ref - OBJ_REF_STATIC_BASE].species
									: objects[object_ref].ship_idx;
	draw_drawcraft_tie98(object_ref, model_type);
}

/* Resolve the model format's 0x7F00 vertex back-references. The original
 * shared arena allowed references beyond one locked model; separate model
 * allocations do not. When bounds are available, an invalid reference is
 * logged once and clamped before the caller dereferences it. */
typedef struct {
	uint16_t ship_idx;             /* loser_ship_idx: ship whose model is locked */
	const uint8_t* buf_lo;         /* model buffer base (model_handle), NULL = unknown */
	const uint8_t* buf_hi;         /* model buffer end (base + size) */
	const uint8_t* b_detail_ptr;   /* selected LOD detail header */
	const uint8_t* b_poly_list;    /* poly/vertex table base */
	const uint8_t* edge_list_base; /* plane + vlist_offset */
	uint16_t a_face_info;          /* clamped face index */
	uint16_t b_rot_angle;          /* b_detail_ptr[2] */
	uint8_t b_numpolys;            /* b_detail_ptr[4] */
	uint8_t edge_idx;              /* edge_list_base[1] (vertex index) */
} PolyDepthDiag;

static int s_polydepthsort_oob_logged;

static const int16_t* draw_polydepth_walk(const uint8_t* start, int axis, const PolyDepthDiag* d) {
	const int16_t* p = (const int16_t*)start;

	if (!d->buf_lo) {
		/* Bounds unknown -- replicate the original unchecked walk. */
		while (((*p) & 0xFF00) == 0x7F00)
			p -= 3 * (((int)(uint8_t)*p) >> 1);
		return p;
	}

	while ((const uint8_t*)p >= d->buf_lo && (const uint8_t*)p + 2 <= d->buf_hi && ((*p) & 0xFF00) == 0x7F00)
		p -= 3 * (((int)(uint8_t)*p) >> 1);

	if ((const uint8_t*)p < d->buf_lo || (const uint8_t*)p + 2 > d->buf_hi) {
		if (!s_polydepthsort_oob_logged) {
			s_polydepthsort_oob_logged = 1;
			TieDiagnostics_Log(TIE_LOG_WARN,
							   "[draw_polydepthsort] vertex-walk OOB on axis %d: ship_idx=%u "
							   "model_buf=[%p..%p) size=%ld | detail@+%ld b_numpolys=%u "
							   "a_face_info=%u b_rot_angle=%u | poly_list@+%ld "
							   "edge_list_base@+%ld edge_idx=%u | start@+%ld final@+%ld "
							   "(over-run %ld)\n",
							   axis, (unsigned)d->ship_idx, (const void*)d->buf_lo, (const void*)d->buf_hi,
							   (long)(d->buf_hi - d->buf_lo), (long)(d->b_detail_ptr - d->buf_lo),
							   (unsigned)d->b_numpolys, (unsigned)d->a_face_info, (unsigned)d->b_rot_angle,
							   (long)(d->b_poly_list - d->buf_lo), (long)(d->edge_list_base - d->buf_lo),
							   (unsigned)d->edge_idx, (long)(start - d->buf_lo),
							   (long)((const uint8_t*)p - d->buf_lo), (long)((const uint8_t*)p - d->buf_hi));
		}
		/* Clamp to a guaranteed in-bounds slot so the caller's deref is safe. */
		p = (const int16_t*)d->buf_lo;
	}
	return p;
}

/* Resolve ambiguous XTRANS2 depth ordering. Category flags handle fixed
 * priority cases; mesh overlaps compare the camera vector against both
 * polygon planes after resolving 0x7F00 vertex back-references. */
// FUNCTION: TIE 0x1BBF4
uint16_t draw_polydepthsort(uint16_t a_face_info, uint16_t obj_a, uint16_t a_obj_id_field,
							uint16_t a_parent_category, int a_eyex, int a_eyey, uint16_t b_face_info,
							uint16_t obj_b, uint16_t b_parent_category, uint16_t b_obj_id_field) {
	(void)a_obj_id_field;
	(void)b_obj_id_field;

	if (bpflightflag)
		return 0;

	uint16_t result_obj = obj_b;
	/* Object returned by the mismatch path. */
	uint16_t mismatch_obj = obj_a;
	int via_special_70 = 0;
	uint16_t bound_hwidth = 0, a_bound_size = 0;
	uint16_t loser_ship_idx = 0, a_ship_idx = 0, ship_idx = 0;
	int b_world_y = 0, b_world_z = 0, b_world_x = 0, a_world_y = 0;
	(void)b_world_y;
	(void)b_world_z;
	(void)b_world_x;
	(void)a_world_y;

	uint16_t a_cat_hi = (uint16_t)(a_parent_category >> 8) & 0xFF;
	uint16_t b_cat_hi = (uint16_t)(b_parent_category >> 8) & 0xFF;

	/* --- Step 1: A-side category dispatch + load A's world position. */
	if (a_cat_hi < 0x30u) {
		if (a_cat_hi >= 0x10u) {
			if (a_cat_hi <= 0x10u || a_cat_hi == 0x20u)
				return result_obj;
			/* fall through to B-side dispatch (LABEL_21) */
		} else if (a_cat_hi != 0) {
			/* fall through to B-side dispatch */
		} else {
			/* A is regular mesh -- LABEL_18: load A's world position.
			 * NOTE: assign the function-scope a_ship_idx (NOT a new
			 * shadow) so the value survives to the A-wins branch at
			 * LABEL_68 where it is needed as the OWNER's ship index. */
			uint16_t a_obj_byte = (uint8_t)a_parent_category;
			a_world_y = objects[a_obj_byte].world_y;
			a_ship_idx = objects[a_obj_byte].ship_idx;
			a_eyex = objects[a_obj_byte].world_x;
			a_eyey = objects[a_obj_byte].world_z;
			if (a_ship_idx == 89)
				a_ship_idx = objects[a_obj_byte].ship_type_override;
			a_bound_size = species_table[a_ship_idx].bound_hwidth;
		}
	} else if (a_cat_hi > 0x30u) {
		if (a_cat_hi >= 0x70u) {
			if (a_cat_hi > 0x70u) {
				if (a_cat_hi == 0x78u)
					return obj_b;
				/* fall through */
			} else {
				/* a_cat_hi == 0x70: load A position (LABEL_18 path).
				 * Same shadowing-fix as the regular-mesh branch above:
				 * assign the function-scope a_ship_idx, not a new local. */
				uint16_t a_obj_byte = (uint8_t)a_parent_category;
				a_world_y = objects[a_obj_byte].world_y;
				a_ship_idx = objects[a_obj_byte].ship_idx;
				a_eyex = objects[a_obj_byte].world_x;
				a_eyey = objects[a_obj_byte].world_z;
				if (a_ship_idx == 89)
					a_ship_idx = objects[a_obj_byte].ship_type_override;
				a_bound_size = species_table[a_ship_idx].bound_hwidth;
			}
		} else if (a_cat_hi != 0x38u) {
			/* fall through */
		}
	} else {
		/* a_cat_hi == 0x30: neither wins. */
		return result_obj;
	}

	/* --- Step 2: B-side category dispatch + load B's world position. */
	if (b_cat_hi < 0x30u) {
		if (b_cat_hi >= 0x10u) {
			if (b_cat_hi <= 0x10u || b_cat_hi == 0x20u)
				return obj_a;
			/* fall through to overlap test */
		} else if (b_cat_hi == 0) {
			uint16_t b_obj_byte = (uint8_t)b_parent_category;
			b_world_x = objects[b_obj_byte].world_x;
			b_world_y = objects[b_obj_byte].world_y;
			b_world_z = objects[b_obj_byte].world_z;
			uint16_t b_ship_idx = objects[b_obj_byte].ship_idx;
			ship_idx = b_ship_idx;
			if (b_ship_idx == 89)
				ship_idx = objects[b_obj_byte].ship_type_override;
			bound_hwidth = species_table[ship_idx].bound_hwidth;
		}
	} else if (b_cat_hi <= 0x30u) {
		return obj_a;
	} else if (b_cat_hi < 0x70u) {
		if (b_cat_hi == 0x38u)
			return obj_a;
	} else if (b_cat_hi > 0x70u) {
		if (b_cat_hi == 0x78u)
			return obj_a;
	}

	/* --- LABEL_37: side selection. Choose which side "wins" the swap. */
	if (a_cat_hi || (uint8_t)a_parent_category < NUM_CRAFTS) {
		if (!b_cat_hi && (uint8_t)b_parent_category >= NUM_CRAFTS)
			bound_hwidth = 0;
	} else {
		if (!b_cat_hi && (uint8_t)b_parent_category >= NUM_CRAFTS)
			return result_obj;
		a_bound_size = 0;
	}

	craftptr = objects[(uint8_t)a_parent_category].craft_ptr;
	CraftData* b_craftptr = objects[(uint8_t)b_parent_category].craft_ptr;
	int relationship = 0;
	if ((uint8_t)a_parent_category < NUM_CRAFTS) {
		int16_t a_link = craftptr->tow_slave_ref;
		if (a_link == (uint8_t)b_parent_category || a_link == (int16_t)b_parent_category ||
			(craftptr->mode_byte == 18 && craftptr->mode_subbyte == 2 &&
			 craftptr->ai_target_ref == (uint8_t)b_parent_category))
			relationship = 1;
	}
	if ((uint8_t)b_parent_category < NUM_CRAFTS) {
		int16_t b_link = b_craftptr->tow_slave_ref;
		if (b_link == (uint8_t)a_parent_category || b_link == (int16_t)a_parent_category ||
			(b_craftptr->mode_byte == 18 && b_craftptr->mode_subbyte == 2 &&
			 b_craftptr->ai_target_ref == (uint8_t)a_parent_category))
			relationship = 2;
	}

	int owner_world_x = 0, owner_world_z = 0, owner_world_y = 0;
	uint16_t owner_obj_id_field = 0;
	FlightObject* owner_obj = NULL;
	int v_eyex = a_eyex, v_eyey = a_eyey;

	if (relationship == 2 ||
		(relationship != 1 && a_cat_hi != 0x70u && (a_bound_size <= bound_hwidth || b_cat_hi == 0x70u))) {
		/* B wins -- swap into the slot. Binary at 0x1BFE9 also reassigns
		 * var_6C (the face-index slot) from a_face_info to b_face_info;
		 * without this swap the polygon-plane lookup below indexes B's
		 * polygon list with A's face index, producing a wrong plane and
		 * a heap-OOB on the edge_list_base[1]/i chain. */
		a_face_info = b_face_info;
		loser_ship_idx = ship_idx;
		ship_idx = a_ship_idx;
		uint16_t b_obj_byte = (uint8_t)b_parent_category;
		craftptr = objects[b_obj_byte].craft_ptr;
		owner_obj = &objects[b_obj_byte];
		owner_world_y = b_world_y;
		owner_obj_id_field = b_obj_id_field;
		uint16_t swap_tmp = obj_a;
		owner_world_x = b_world_x;
		v_eyex = obj_b; /* binary: a_eyex = obj_b -- treat obj_b as the X */
		(void)v_eyex;
		owner_world_z = b_world_z;
		result_obj = swap_tmp;
		mismatch_obj = obj_b; /* binary: v85 = a8 in B-wins */
		if (b_cat_hi == 0x70u)
			via_special_70 = 1;
	} else {
		/* A wins. */
		owner_world_x = a_eyex;
		v_eyex = b_world_x;
		loser_ship_idx = a_ship_idx;
		owner_world_z = a_eyey;
		owner_obj_id_field = a_obj_id_field;
		v_eyey = b_world_z;
		owner_obj = &objects[(uint8_t)a_parent_category];
		owner_world_y = a_world_y;
		a_world_y = b_world_y;
		if (a_cat_hi == 0x70u)
			via_special_70 = 1;
	}

	/* --- LABEL_68: relative-vector compute + bit-scaling normalize. */
	int other_dx = v_eyex - owner_world_x;
	int other_dy = a_world_y - owner_world_y;
	int camera_dx = camera.x - owner_world_x;
	int camera_dy = camera.y - owner_world_y;
	int other_dz = v_eyey - owner_world_z;
	int camera_dz = camera.z - owner_world_z;
	int other_dx_abs = other_dx >> 14;
	int other_dy_abs = other_dy >> 14;
	int camera_dz_save = camera_dz;
	int norm_shift = 0;
	int other_dz_abs = other_dz >> 14;
	if ((other_dx_abs & 0x8000) != 0)
		other_dx_abs = -other_dx_abs;
	if ((other_dy_abs & 0x8000) != 0)
		other_dy_abs = -other_dy_abs;
	if ((other_dz_abs & 0x8000) != 0)
		other_dz_abs = -other_dz_abs;
	do {
		do {
			camera_dx >>= 1;
			other_dy >>= 1;
			other_dx >>= 1;
			camera_dy >>= 1;
			other_dz >>= 1;
			other_dx_abs = (uint16_t)other_dx_abs >> 1;
			camera_dz_save >>= 1;
			other_dy_abs = (uint16_t)other_dy_abs >> 1;
			other_dz_abs = (uint16_t)other_dz_abs >> 1;
			++norm_shift;
		} while ((uint16_t)other_dx_abs);
	} while ((uint16_t)other_dy_abs || (uint16_t)other_dz_abs);

	/* Watcom unaligned dword loads in the original decomp:
	 *   *(int*)&owner_obj->X >> 16 reads the int16 at X+2 (the next field).
	 * Translated below using the actual field, per CLAUDE.md guidance. */
	int other_proj_side =
		clamp_q30((int16_t)other_dz * owner_obj->side_z + (int16_t)other_dy * owner_obj->side_y +
				  (int16_t)other_dx * owner_obj->side_x);
	int other_proj_side_hi = other_proj_side >> 15;

	int other_proj_fwd =
		clamp_q30((int16_t)other_dz * owner_obj->fwd_z + (int16_t)other_dy * owner_obj->fwd_y +
				  (int16_t)other_dx * owner_obj->fwd_x);
	int other_proj_fwd_neg_hi = -(other_proj_fwd >> 15);

	int other_proj_up = clamp_q30((int16_t)other_dz * owner_obj->up_z + (int16_t)other_dy * owner_obj->up_y +
								  (int16_t)other_dx * owner_obj->up_x);
	int other_proj_up_hi = other_proj_up >> 15;

	int cam_proj_side =
		clamp_q30((int16_t)camera_dz_save * owner_obj->side_z + (int16_t)camera_dy * owner_obj->side_y +
				  (int16_t)camera_dx * owner_obj->side_x);
	int cam_proj_side_hi = cam_proj_side >> 15;

	int cam_proj_fwd =
		clamp_q30((int16_t)camera_dz_save * owner_obj->fwd_z + (int16_t)camera_dy * owner_obj->fwd_y +
				  (int16_t)camera_dx * owner_obj->fwd_x);
	int cam_proj_fwd_neg_hi = -(cam_proj_fwd >> 15);

	int cam_proj_up = clamp_q30((int16_t)camera_dz_save * owner_obj->up_z +
								(int16_t)camera_dy * owner_obj->up_y + (int16_t)camera_dx * owner_obj->up_x);
	int cam_proj_up_hi = cam_proj_up >> 15;

	int final_shift = via_special_70 ? (norm_shift - 1) : (norm_shift + 1);

	/* --- Size-based fast reject if both species satisfy bound check.
	 * Watcom unaligned dword loads in the original decomp:
	 *   *(int*)&spec_data[N].dock_fwd >> 16 reads spec_data[N].dock_passive_light (next field).
	 *   *(int*)&spec_data[N].dock_passive_heavy >> 16 reads spec_data[N].dock_active_light. */
	if (relationship != 0) {
		draw_lockshipfileptrs(ship_idx);
		int speed_match =
			spec_data[spec_getspecnum(ship_idx)].dock_passive_light == (objectblockptr->speed_default >> 17);
		if (speed_match) {
			draw_lockshipfileptrs(loser_ship_idx);
			int d2lo_match = spec_data[spec_getspecnum(loser_ship_idx)].dock_active_light ==
							 (objectblockptr->shield_default >> 17);
			if (d2lo_match) {
				if (cam_proj_up_hi < (objectblockptr->shield_default >> 16 >> final_shift))
					return result_obj;
				return mismatch_obj;
			}
		}
	}

	/* --- Full polygon-plane test. */
	draw_lockshipfileptrs(loser_ship_idx);
	ShipModelMesh* b_mesh = &componentblockptr[owner_obj_id_field];
	fview_newcalcrotate(owner_obj->roll, owner_obj->heading, owner_obj->pitch, 0, owner_obj);
	int8_t b_detail_marker = (int8_t)craftptr->mesh_rotation[owner_obj_id_field];
	if (b_detail_marker && (b_mesh->rotation_offset || mission.train_craft_type))
		fview_componentrotation((int16_t)((int)b_detail_marker << 8), b_mesh);

	const uint8_t* b_detail_ptr = (const uint8_t*)draw_getcompdetailptr(b_mesh, craftptr->eye_z_cache);
	uint8_t b_numpolys = b_detail_ptr[4];
	if (b_numpolys <= a_face_info)
		a_face_info = (uint16_t)(b_numpolys - 1);
	const uint8_t* b_poly_list = b_detail_ptr + b_numpolys + 17;
	uint16_t b_rot_angle = b_detail_ptr[2];
	const PolyFace* plane = (const PolyFace*)(b_poly_list + 8 * a_face_info + 12 * b_rot_angle);

	int16_t plane_normal_x = plane->normal_x;
	int16_t plane_normal_y = plane->normal_y;
	int16_t plane_normal_z = plane->normal_z;
	const uint8_t* edge_list_base = (const uint8_t*)plane + plane->vlist_offset;

	/* Bounds of the locked model buffer (loser_ship_idx is the ship whose
	 * model was locked above). Used only by the diagnostic walk below. */
	const uint8_t *model_lo = NULL, *model_hi = NULL;
	if (loser_ship_idx < NUM_SPECIES) {
		model_lo = (const uint8_t*)species_table[loser_ship_idx].model_handle;
		if (model_lo)
			model_hi = model_lo + species_model_handle_sizes[loser_ship_idx];
	}

	int16_t edge_pt_x, edge_pt_y, edge_pt_z;
	if (model_lo &&
		((const uint8_t*)edge_list_base + 1 < model_lo || (const uint8_t*)edge_list_base + 1 >= model_hi)) {
		/* The face's vlist_offset placed edge_list_base itself outside the
		 * model buffer (a bad plane lookup). Log the inputs once and use
		 * zero edge points (a deterministic, arbitrary depth tiebreak). */
		if (!s_polydepthsort_oob_logged) {
			s_polydepthsort_oob_logged = 1;
			TieDiagnostics_Log(TIE_LOG_WARN,
							   "[draw_polydepthsort] edge_list_base OOB: ship_idx=%u "
							   "model_buf=[%p..%p) size=%ld | detail@+%ld b_numpolys=%u "
							   "a_face_info=%u b_rot_angle=%u | poly_list@+%ld plane@+%ld "
							   "vlist_offset=%d edge_list_base@+%ld\n",
							   (unsigned)loser_ship_idx, (const void*)model_lo, (const void*)model_hi,
							   (long)(model_hi - model_lo), (long)(b_detail_ptr - model_lo),
							   (unsigned)b_numpolys, (unsigned)a_face_info, (unsigned)b_rot_angle,
							   (long)(b_poly_list - model_lo), (long)((const uint8_t*)plane - model_lo),
							   (int)plane->vlist_offset, (long)(edge_list_base - model_lo));
		}
		edge_pt_x = edge_pt_y = edge_pt_z = 0;
	} else {
		PolyDepthDiag diag = {
			.ship_idx = loser_ship_idx,
			.buf_lo = model_lo,
			.buf_hi = model_hi,
			.b_detail_ptr = b_detail_ptr,
			.b_poly_list = b_poly_list,
			.edge_list_base = edge_list_base,
			.a_face_info = a_face_info,
			.b_rot_angle = b_rot_angle,
			.b_numpolys = b_numpolys,
			.edge_idx = edge_list_base[1],
		};
		const int16_t* i = draw_polydepth_walk(b_poly_list + 6 * diag.edge_idx, 0, &diag);
		edge_pt_x = (int16_t)((int)*i >> final_shift);
		const int16_t* j = draw_polydepth_walk(b_poly_list + 6 * diag.edge_idx + 2, 1, &diag);
		edge_pt_y = (int16_t)((int)*j >> final_shift);
		const int16_t* k = draw_polydepth_walk(b_poly_list + 6 * diag.edge_idx + 4, 2, &diag);
		edge_pt_z = (int16_t)((int)*k >> final_shift);
	}

	int16_t cam_proj_side_dx = (int16_t)(cam_proj_side_hi - edge_pt_x);
	int16_t other_proj_side_dx = (int16_t)(other_proj_side_hi - edge_pt_x);
	int16_t other_proj_fwd_dx = (int16_t)(other_proj_fwd_neg_hi - edge_pt_y);
	int16_t cam_proj_fwd_dx = (int16_t)(cam_proj_fwd_neg_hi - edge_pt_y);
	int16_t cam_proj_up_dx = (int16_t)(cam_proj_up_hi - edge_pt_z);

	int dot_a = clamp_q30((int16_t)(other_proj_up_hi - edge_pt_z) * plane_normal_z +
						  other_proj_fwd_dx * plane_normal_y + other_proj_side_dx * plane_normal_x);
	int dot_other_hi = dot_a >> 15;
	int dot_b = clamp_q30(cam_proj_up_dx * plane_normal_z + cam_proj_fwd_dx * plane_normal_y +
						  cam_proj_side_dx * plane_normal_x);

	if (((dot_other_hi ^ (dot_b >> 15)) & 0x8000) == 0)
		return result_obj;
	return mismatch_obj;
}
