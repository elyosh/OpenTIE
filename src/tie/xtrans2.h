#ifndef __XTRANS2_H__
#define __XTRANS2_H__

#include <stdint.h>

/*
 * XTRANS2 — translucent / per-edge run-table rendering engine.
 *
 * Terminal stage of the TIE 3D pipeline. Consumes the active-edge list
 * produced by TRACE2_drawface, performs per-scanline depth sorting and
 * Gouraud-interpolated pixel emission.
 *
 * Per-frame flow:
 *   1. XTRANS2_clearruntable    — lazy-init shade/material tables and
 *                                 reset the per-scanline left/right run
 *                                 extents.
 *   2. XTRANS2_initxtrans       — per-viewport scalars, clear TRACE2's
 *                                 rowheaders[], reset pool cursors.
 *   3. (DRAWPOL emits polygons; TRACE2 fills rowheaders[] and the edge
 *      pools; TRANSFM2_classifyedges sets flags/xy in XTRANS2's
 *      per-edge cache.)
 *   4. XTRANS2_drawxtrans       — main scanline loop (age edges,
 *                                 bubble-sort, merge row starts, walk
 *                                 mask run-length stream, call outputxt
 *                                 to emit shaded pixel runs).
 *
 * Object identifier encoding used throughout:
 *   0           : background / deep space
 *   [1..0x7F]   : regular mesh object (paired with XTRANS2_ObjectRecord)
 *   [0x80..0xEF]: flat-polygon / translucent marker (index into
 *                 flatcolors / flatx / flaty / flatz / flatparentobj /
 *                 flatcomponentnum — `obj - 0x80` gives the flat index)
 *   [0xF0..0xFF]: transparent / marking chain (edgeid addresses a linked
 *                 list of swap records in the xtransdataptr blob).
 *   0xFFFF      : pending-sort sentinel (resolved via findnearest).
 *
 * Fixed-point conventions for TRACE2_EdgeInfo:
 *   info->x is 24.8 screen x (divide by 256 for pixel column).
 *   info->lt is a 16.1 signed lighting value; >>1 yields the raw 16.0.
 */

/* --- Edge-flags bitmask written by TRANSFM2_classifyedges + TRACE2_drawface --- */
/*
 * Classifier bits 0x01..0x08 (position vs viewport):
 *   0x01 = OFF_RIGHT  (fully past x = pixelswide)
 *   0x02 = OFF_LEFT   (fully past x = 0)
 *   0x04 = OFF_SCREEN (y-range empty or outside [0,pixelsdeep))
 *   0x08 = VALID      (edge crosses the visible viewport)
 *
 * 0x10 is never set in the shipped demo or retail binary — the
 * corresponding test in TRACE2_drawface is dead code (kept here as a
 * bit-literal named constant to match the binary layout).
 *
 * TRACE2 runtime bits (OR'd by TRACE2_drawface):
 *   0x20 = HAS_HEADER   (EdgeHeader allocated this frame)
 *   0x40 = SLOPE_CACHED (edgeslope{hi,lo,frac} populated)
 *   0x80 = YDOM         (y-dominant edge after slope classification)
 *
 * 0x80 is also ASSIGNED (= 0x80 directly, not OR'd) by classifyedges as a
 * sentinel meaning "both endpoints behind near plane, skip entirely".
 * TRACE2_drawface tests `pt == 0x80` exactly (sentinel) separately from
 * `pt & 0x80` (YDOM bit among others).
 */
typedef enum {
	XTRANS2_EDGEFLAG_OFF_RIGHT = 0x01,
	XTRANS2_EDGEFLAG_OFF_LEFT = 0x02,
	XTRANS2_EDGEFLAG_OFF_SCREEN = 0x04,
	XTRANS2_EDGEFLAG_VALID = 0x08,
	XTRANS2_EDGEFLAG_UNUSED_10 = 0x10, /* dead in shipped binary */
	XTRANS2_EDGEFLAG_HAS_HEADER = 0x20,
	XTRANS2_EDGEFLAG_SLOPE_CACHED = 0x40,
	XTRANS2_EDGEFLAG_YDOM = 0x80,
	/* legacy aliases */
	XTRANS2_EDGEFLAG_DEGEN = XTRANS2_EDGEFLAG_OFF_SCREEN,
	XTRANS2_EDGEFLAG_REVERSED = XTRANS2_EDGEFLAG_UNUSED_10,
} XTRANS2_EdgeFlags;

/* --- Per-object record embedded in the xtransdataptr blob -------------- */
/*
 * Offset into the blob is objectptrs[objid] (u16). The bounding box is
 * in screen-eye space (signed 16-bit); face_covers[] lists up to 5 object
 * ids whose bbox is known to entirely occlude/be occluded by this one
 * (used as a fast-path in getinfront). face_ypos[f] is the scanline
 * at which face f was last opened/closed (negative means "never") and
 * face_flags[2*f] is the material/shade slot for face f (high bit
 * encodes solid-fill vs Gouraud-interpolated).
 */
#pragma pack(push, 2)
typedef struct xtrans2_ObjectRecord {
	int16_t bbox_xmin;        /* +0x00 */
	int16_t bbox_ymin;        /* +0x02 */
	int16_t bbox_zmin;        /* +0x04 */
	int16_t bbox_xmax;        /* +0x06 */
	int16_t bbox_ymax;        /* +0x08 */
	int16_t bbox_zmax;        /* +0x0A */
	uint16_t parent_category; /* +0x0C; high byte is category, low byte zero */
	uint16_t obj_id_field;    /* +0x0E */
	uint8_t field_10[2];      /* +0x10 — reserved padding */
	uint8_t face_covers[5];   /* +0x12 */
	uint8_t field_17;         /* +0x17 */
	int32_t face_ypos[128];   /* +0x18 — last y pos per face (-1 = never) */
	uint8_t face_flags[256];  /* +0x218 — 2 bytes per face: [0]=shade, [1]=rt-edge */
} xtrans2_ObjectRecord;       /* sizeof = 0x318 = 792 bytes */
#pragma pack(pop)

/* --- Globals ---------------------------------------------------------- */

/* Lazy-init flags consumed by clearruntable. */
extern uint8_t xtrans2_dithercolorinitflag;
extern uint8_t xtrans2_materialrgbinitflag;

/* 4-byte constant sentinel used as an operand scratch by TRACE2 inlines
 * (the linker collapses repeated `dd -1` loads to this label). */
extern uint32_t xtrans2_minusone;

/* Linear framebuffer base used by outputxt / drawxtrans. Owned by XTRANS2
 * per watdbg; other modules (XVESA, PANEL) read through it. */
extern uint8_t* xtrans2_videobaseptr;

/* Mask buffer offset (16-bit signed). -16384 (0xC000) outside PIP,
 * -8192  (0xE000) while a PIP viewport is active. */
extern int16_t maskbufptr;

/* Current side-buffer slots. Pointer pair is switched between
 * leftsidedata1 / rightsidedata1 (main viewport) and
 * leftsidedata2 / rightsidedata2 (PIP viewport). Each slot holds
 * pixelsdeep int32_t values (left/right pixel extents per scanline). */
extern int32_t* leftside;
extern int32_t* rightside;

/* Per-viewport run tables. 1/2 suffix selects main vs PIP. Each entry is
 * one scanline's left/right pixel-column extent (int32_t). 1920 bytes =
 * 480 scanlines, matching watdbg's OPAQUE[1920] sizing. */
extern int32_t leftsidedata1[480];
extern int32_t leftsidedata2[480];
extern int32_t rightsidedata1[480];
extern int32_t rightsidedata2[480];

/* Scan-out cursor state, consumed by drawxtrans / outputxt. */
extern uint8_t* logbufbaseptr;
extern int32_t logbufypos;

/* Per-edge cache. Filled by TRANSFM2_classifyedges and TRACE2_drawface.
 * edgept1/edgept2 are pointers into the screen-xy ring from
 * TRANSFM2_getscreencoords. Sizes per watdbg: _edgept1[1024 bytes]=256 ptrs,
 * _edgept2[512 bytes]=128 ptrs. */
extern int32_t* edgept1[256];
extern int32_t* edgept2[128];
extern int32_t edgexdiff[128];
extern int32_t edgeydiff[128];
extern int8_t edgexsign[128];
extern int8_t edgeysign[128];
extern int16_t edgeslopehi[128];
extern int16_t edgeslopelo[128];
extern int16_t edgeslopefrac[128];
extern uint8_t edgeflags[128];
extern void* edgeflagptr[128]; /* TRACE2 EdgeHeader per-edge pointer */

/* Flat-polygon tables (filled by TRACE2_drawscreencoords). */
extern uint16_t flatobjnum;
extern uint8_t flatcolors[128];
extern uint8_t flatcomponentnum[128];
extern uint16_t flatparentobj[128];
extern int16_t flatz[128];
extern int16_t flatx[128];
extern int16_t flaty[128];

/* Object-heap state (128 slots). objflag[i] is the inverse-index into
 * objheap[] (0 = not in heap, 0xFF = frontmost). objectcount[] tracks
 * live faces per object; objectminface[] is the lowest open face index;
 * objectminedgeptr[] is the TRACE2_EdgeHeader of that face's left edge. */
extern uint8_t objflag[256]; /* aliased byte_204BC8 = objflag[128] */
extern uint8_t objectcount[128];
extern uint8_t objectminface[128];
extern void* objectminedgeptr[128];
extern uint16_t objheap[128];

/* Dither / material blending tables (lazy-filled by clearruntable). */
extern uint8_t dithercolors[9984];  /* 39 mats * 16 steps * 4 perms * 4 B */
extern uint8_t materialrgbhi[2496]; /* 39 mats * 64 blend steps */
extern uint8_t materialrgblo[2496];

/* Starburst hash storage. */
extern uint8_t starhashtable[2048];

/* Eye-space vertex ring shared with TRANSFM2/TRACE2/DRAWPOL.
 * 1536 bytes = 128 vertices * 12 bytes (3*int32 per DRAWPOL_EyeVertex).
 * Also transiently holds a 6*int32 bbox at offset 0 (filled by
 * transfm2_geteyeminmax, then overwritten by geteyecoords). The
 * verified access width across the engine is int32 (store width
 * 32-bit in geteyeminmax/geteyecoords). */
extern int32_t eyexyzdata[384];

/* Per-scanline working state. These live in contiguous BSS and are used
 * as single scalars (not arrays). Layout follows watdbg exactly. */
extern int32_t newx;           /* next edge's x (pixel col) */
extern uint8_t* maskptr;       /* mask-RLE cursor into xtransdataptr */
extern uint32_t videoypos;     /* byte offset within VESA window */
extern int32_t startx_mod_54;  /* binary-level 'startx' — renamed to
								* avoid collision with TRACE2's startx */
extern int32_t starty_mod_54;  /* matching 'starty' companion */
extern int32_t newlt;          /* next edge's lighting value */
extern uint32_t objid;         /* current edge's object id */
extern uint32_t face2;         /* current edge's second face id */
extern uint32_t face1;         /* current edge's first face id */
extern int32_t maskx;          /* current mask-run x transition */
extern int32_t currentypos;    /* current scanline index */
extern int32_t runx;           /* current edge's pixel column (x>>8) */
extern int32_t endx;           /* run end column for outputxt */
extern int32_t endy_mod_54;    /* companion (unused by XTRANS2 itself) */
extern uint32_t edgeid;        /* current edge id */
extern uint32_t pixdeepshft24; /* pixelsdeep << 24 */
extern void* tempptr;          /* reusable temporary */
extern void* currptr;          /* reusable temporary */
extern void* currptr2;         /* reusable temporary — outputxt right-edge cache */
extern void* currentedgeptr;   /* current edge under processing */
extern void* lastptr;          /* reusable temporary */
extern void* headerlist;       /* active-edge list head for current scanline */

extern int16_t tempslope;
extern int16_t numlastrow;
extern uint16_t curobjid; /* frontmost object id (0xFFFF = needs resort) */
extern int16_t oxtlightinc;
extern int16_t lightcount;
extern uint16_t twicepixelsdeep;
extern uint16_t lastheap;     /* top-of-heap index */
extern uint16_t pixdeepshft8; /* pixelsdeep << 8 (low-word snapshot) */
extern uint16_t pixwideshft7; /* pixelswide << 7 */

extern int8_t maskflag; /* sign flips each mask run */
extern uint8_t popflag; /* deferred-pop count */
extern uint8_t xtflagvalue;
extern uint8_t delflag;
extern int16_t changesign;

/* --- API -------------------------------------------------------------- */

void xtrans2_clearruntable(void);
void xtrans2_initxtrans(void);
void xtrans2_drawxtrans(void);
void xtrans2_processedge(void);
void xtrans2_closeobject(void);
void xtrans2_openobject(void);
void xtrans2_outputxt(void);
uint16_t xtrans2_findnearest(void);
uint16_t xtrans2_getinfront(uint16_t obj_a, uint16_t obj_b);

#endif
