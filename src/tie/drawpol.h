#ifndef __DRAWPOL_H__
#define __DRAWPOL_H__

#include <stdint.h>

/*
 * DRAWPOL — polygon-rendering front-end.
 *
 * Top of the 3D object pipeline. DRAW_drawcraft / DRAW_drawlaser call
 * drawpol_drawpolyobject with the raw poly_data block from the .LFD
 * ship file; this module dispatches by type (billboard / line-object /
 * 3D mesh), transforms vertices to eye/screen space via TRANSFM2,
 * walks the face list (BSP for 0x82/0x83, flat for 0x80/0x81) calling
 * trace2_drawface / drawpol_drawlineface per face, and emits marking
 * (decal) sub-polygons via drawpol_drawmarkings.
 *
 * This header owns all DRAWPOL globals per watdbg
 * (docs/watdbg-prototypes.txt lines 1350-1419), plus the small structs
 * (PolyFace, PolyMeshHeader, DRAWPOL_LocalLight) used on its hot path.
 * PolyFace is cross-module shared with draw.c.
 */

/* ========================================================================
 * Structs
 * ==================================================================== */

/* PolyFace is defined in draw.h (cross-module shared). */
#include "tie/draw.h"

/* 5-byte header for every poly_data block. The 'type' byte selects format:
 *   0xFF         = billboard/sprite (numedges field holds scale: 0/8/16)
 *   0x40 / 0x41  = line-object (edges, no faces)
 *   0x80..0x83   = 3D mesh (bit0=precomputed-local; bit1=BSP tree) */
typedef struct PolyMeshHeader {
	uint8_t type;
	uint8_t dedup_flag; /* one-shot: 3D mesh path rewrites 0x7F00 vertex refs then clears */
	uint8_t numpoints;
	uint8_t numedges; /* sprite: stores scale (0/8/16) */
	uint8_t numfaces;
} PolyMeshHeader;

/* Per-light record in localLights[]. 16 bytes × 8 lights. */
typedef struct DRAWPOL_LocalLight {
	int32_t x; /* world X (to subtract from vertex world X) */
	int32_t y;
	int32_t z;
	int32_t range; /* intensity/range; used as squared-distance threshold and gain */
} DRAWPOL_LocalLight;

/* Local/world-space vertex (6 bytes: 3 int16). Stored in the .LFD poly
 * data stream and walked by DRAWPOL via firstpointoff / firstvertnorm. */
typedef struct PolyVert {
	int16_t x;
	int16_t y;
	int16_t z;
} PolyVert;

/*
 * 3-byte BSP-tree node header used by drawpol_dobsptree. Distinct from
 * the 18-byte BSPNode walked by draw_gettreeorder (defined in tie.h):
 * this is a per-face control record embedded in the .LFD poly_data
 * stream, while BSPNode lives in the per-mesh painter's-order tree.
 *
 * `next_off` is a self-relative byte offset reused across the four walk
 * modes (sibling advance, two-sided recursion, normal-rejected branch,
 * tail fallback). A zero/negative value terminates the current walk;
 * `node + 3` skips the header to the body.
 */
#pragma pack(push, 1)
typedef struct BSPFaceNode {
	uint8_t face_idx;
	int16_t next_off;
} BSPFaceNode;
#pragma pack(pop)

/* Eye-space vertex (12 bytes: 3 int32). Output of TRANSFM2_geteyecoords;
 * stored contiguously in _eyexyzdata (xtrans2.c owned). */
typedef struct DRAWPOL_EyeVertex {
	int32_t x;
	int32_t y;
	int32_t z;
} DRAWPOL_EyeVertex;

/* Scratch buffer for marking (decal) rendering. See drawpol.c for layout. */
typedef struct DRAWPOL_MarkingEyeData {
	DRAWPOL_EyeVertex scratch;   /* line-endpoint stash (was dword_E24B8/BC/C0) */
	DRAWPOL_EyeVertex verts[17]; /* up to 16 marking vertices + polygon-close slot */
} DRAWPOL_MarkingEyeData;

/* Marking/decal palette modes (drawpol_setmarkingcolors argument). */
typedef enum MarkingMode {
	MARKING_OFF = 0,
	MARKING_NORMAL = 1,
	MARKING_ALT = 2,
} MarkingMode;

/* ========================================================================
 * Per-vertex / per-edge setup arrays (filled by TRANSFM2_classifyedges
 * before TRACE2 sees them). Walked 0-based in C; the Watcom binary emits
 * compile-time `_array - stride` base addresses to support its 1-based
 * indexing idiom -- that's a code-gen artifact, not a data-layout concept.
 * ==================================================================== */
extern int32_t* calcflag[128];    /* NULL if vertex behind near plane, else ptr to screen-xy pair */
extern uint16_t vertexlight[128]; /* per-vertex light intensity (0xFFFF = not yet computed) */
extern uint8_t* firstvertptr;     /* base of vertex-list stream for current face */

/* edgeflags[], edgept1[], edgept2[], flatx/y/z, flatcolors, flatcomponentnum,
 * flatparentobj, eyexyzdata are owned by xtrans2.c per watdbg -- see xtrans2.h. */

/* ========================================================================
 * Screen-xy ring filled by TRANSFM2_getscreencoords.
 * ==================================================================== */
extern int32_t* firstscreenxy;
extern int32_t* lastscreenxy;
extern int32_t* newscreenxy;
extern int32_t *minscreenx, *maxscreenx;
extern int32_t *minscreeny, *maxscreeny;

/* ========================================================================
 * Current polygon context (set by DRAWPOL, read by TRACE2/XTRANS2).
 * ==================================================================== */
extern int16_t numpoints;
extern int16_t numedges;
extern int16_t samexcnt;
extern int16_t sameycnt;
extern int16_t counter;
extern uint8_t color;
extern uint16_t facenumber;
extern uint16_t objectnum;
extern uint16_t parentobject;
extern uint8_t gauraudflag; /* note watdbg spelling: 'gauraudflag' */
extern uint16_t layervalue;
extern int16_t numfaces;
extern int16_t polycnt;
extern int16_t threedflag;
extern uint8_t closerthansizeflag;
extern uint8_t doublesideflag;
extern uint8_t numeyezpos;
extern uint8_t lightval;
/* flatobjnum / polyidbyte / edgeidbyte / objectedgeword live in their
 * watdbg-owning headers (xtrans2.h for flatobjnum, trace2.h for the rest).
 * thickness: drawln2.c-owned per watdbg; see drawln2.h. */

/* Object position / world (shared with TRANSFM2). worldx/worldy/worldz
 * are tie.c-owned per watdbg; declared in tie.h. */
extern int32_t objectx, objecty, objectz;
extern int16_t objectxhi, objectyhi, objectzhi;
extern int16_t objectxlo, objectylo, objectzlo;

/* rotlightX/Y/Z and thicknessMultiple are tie.c-owned per watdbg;
 * declared in tie.h. */

/* ========================================================================
 * Poly-data layout pointers for the current object (3D mesh path).
 * All typed so plain [N] indexing walks one structure element:
 *   firstcoloroff[f]     = face-color byte          (uint8)
 *   firstpointoff[v]     = vertex position  {x,y,z} (PolyVert, 6 B)
 *   firstvertnorm[v]     = vertex normal    {x,y,z} (PolyVert, 6 B)
 *   firstfaceoff[f]      = face header              (PolyFace,  8 B)
 *
 * For the 3D-mesh path, firstpointoff initially points at a 2-vertex
 * bbox (min XYZ, max XYZ) used by transfm2_geteyeminmax; after the
 * frustum cull it's advanced by 2 PolyVert slots to the actual vertex
 * array. firstvertnorm then sits at firstpointoff + numpoints. */
extern uint8_t* firstcoloroff;
extern PolyVert* firstpointoff;
extern PolyVert* firstvertnorm;
extern PolyFace* firstfaceoff;

/* 'firsteyexyz' is the typed view of eyexyzdata (owned by xtrans2.c, see
 * xtrans2.h) as eye vertices. The underlying buffer is transiently reused:
 * first holds a 6-int32 bbox filled by transfm2_geteyeminmax, then
 * overwritten with per-vertex eye coords (3*int32 each) by geteyecoords. */
extern DRAWPOL_EyeVertex* firsteyexyz;

/* Per-face visibility mask (indexed 0..numfaces-1).
 *   bit 0 (0x01) = regular visible face
 *   bit 2 (0x04) = face passed the on-screen test (draw it)
 *   bit 4 (0x10) = backface but two-sided (ambient lighting only) */
extern uint8_t facevisflag[256];

/* Object-record write cursor into xtransdataptr.
 *   newobjectdef: byte offset (0..0xC000 budget).
 *   curobjptr:    pointer view into xtransdataptr+newobjectdef.
 *   objectdef:    drawpol-private in the binary (static in watdbg); accessed
 *                 only within drawpol.c so it is NOT re-exported here. */
extern uint16_t newobjectdef;
extern uint8_t* curobjptr;

/* ========================================================================
 * Material / highlight / marking tables (DRAWPOL-owned per watdbg).
 *
 * materialcolors[720]: 45 material ramps * 16 step LUT. Indexed as
 *   materialcolors[16 * slot + step]; the low bit of each slot is an
 *   "unlit" flag preserved by getlightvalue.
 * markingdefs[3][16]: 3 shade ramps (OFF / NORMAL / ALT) for the marking
 *   material. setmarkingcolors copies markingdefs[mode][0..15] into
 *   materialcolors[208..223] (= row 13, the marking shade ramp).
 * markcoloroffset[72]: per-material-slot signed offset added to the face's
 *   base color in DRAWPOL_getlightvalue. Index 14 doubles as `markingstate`
 *   -- DRAWPOL_setmarkingcolors writes 0/-1/+1 there to drive the
 *   marking-material 3-frame animation. Other entries stay 0 (identity).
 * targetmapping[39]:  material-index -> target-highlight table.
 * highlightmapping[9]: 3 highlight groups * 3 shade offsets.
 * traininggatecolors[4]: gate-color overlay selected by gatecolor.
 * ==================================================================== */
extern uint8_t materialcolors[720];
extern uint8_t markingdefs[3][16];
extern int8_t markcoloroffset[72];
/* Read-only LUTs (compile-time const in drawpol.c, mirroring the retail
 * binary's .data tables at 0xC1BEC / 0xC1C13 / 0xC1C1C). targetmapping
 * uses Watcom base-1 indexing in the binary -- callers use
 * `targetmapping[(c & 0x3F) - 1]` with a guard for c == 0. */
extern const uint8_t targetmapping[39];
extern const uint8_t highlightmapping[9];
extern const uint8_t traininggatecolors[4];
/* markingstate (the marking-material animation cycle: 0=off, -1=normal,
 * +1=alt) lives at markcoloroffset[14] in the binary. Use that slot
 * directly rather than a separate variable so the read in
 * DRAWPOL_getlightvalue's `markcoloroffset[c & 0x3F]` picks it up for
 * face base color 14. */
#define markingstate (markcoloroffset[14])

/* Global light rig (directional + up to 8 local point lights). */
extern int32_t lightX, lightY, lightZ;
extern int32_t localLightCnt;
extern DRAWPOL_LocalLight localLights[8];

/* linelight1/linelight2/point1ptr: drawln2.c-owned per watdbg; see drawln2.h. */

/* Marking book-keeping. */
extern uint8_t* farmarkingptr[128];
extern uint16_t markingnumber[128];
extern uint16_t markingptr[128];
extern uint16_t objectptrs[128];
extern DRAWPOL_MarkingEyeData markingeyedata;
extern uint8_t markcnt;
extern uint16_t nummarks;
/* gatecolor is tie.c-owned per watdbg; declared in tie.h. */

/* Flat-object scratch: owned by xtrans2.c per watdbg -- see xtrans2.h. */

/* Currently-rendering object id (for XTRANS2 attribution). */
extern uint16_t solidindex;

/* someznegflag is trace2.c-owned per watdbg; declared in trace2.h. */

/* ========================================================================
 * API
 * ==================================================================== */

/* Install the 16-entry marking/decal palette row for this draw pass.
 * Copies markingdefs[mode*16 + 0..15] into materialcolors[208..223]
 * (marking-color materialcolors row) and sets markingstate. */
void drawpol_setmarkingcolors(MarkingMode mode);

/* Backface-cull test for a single face. Returns 0 (front-facing) or 2
 * (back-facing); caller stores (ret >> 1) into facevisflag[face_idx]. */
uint16_t drawpol_checknormal(uint16_t face_idx);

/* Compute the 8-bit palette color for a face, applying material remap,
 * training-gate overlay, target highlight, per-vertex Gouraud lighting
 * (if gauraudflag && face.flags & 0x40), or flat-face lighting. */
int16_t drawpol_getlightvalue(int16_t color_byte, uint16_t face_idx);

/* Render one wireframe edge (2-vertex face) with perspective-corrected
 * thickness. Reads edge record via firstvertptr, endpoint cache via
 * edgept1/edgept2, per-vertex light via vertexlight[]. */
void drawpol_drawlineface(void);

/* Render all markings (barycentric decal N-gons) on one face. */
void drawpol_drawmarkings(uint16_t face_idx);

/* Recursive BSP-tree painter's-sort walker. Variable-stride node stream:
 *   +0 (u8) face index, +1 (i16) self-relative offset to subtree/sibling.
 * Four modes keyed by facevisflag[face]; see function for details. */
uint8_t* drawpol_dobsptree(uint8_t* node);

/* Dead code in the demo: quad-rasterization helper, no callers. */
void drawpol_drawsurfacepoly(int32_t* scratch, char color_code);

/* Top-level renderer: parses poly_data header and dispatches by type.
 * Pointer is 16-bit aligned (the binary passes a WORD *), but the code
 * accesses it as both byte stream (header/face_colors) and word array
 * (point triples / vertex stream). */
void drawpol_drawpolyobject(const uint16_t* poly_data, int32_t obj_x, int32_t obj_y, int32_t obj_z);

#endif
