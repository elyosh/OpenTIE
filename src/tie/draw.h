#ifndef __DRAW_H__
#define __DRAW_H__

#include "tie/tie.h" /* ShipModelData / ShipModelMesh forward typedefs */
#include <stdint.h>

/*
 * DRAW — mid-level 3D craft renderer. Sits above DRAWPOL (polygon emit)
 * and below the frame loop.
 *
 * Per-frame flow when rendering a craft:
 *   draw_drawcomplexobject(obj_idx)
 *     draw_lockshipfileptrs(ship_idx)              -- resolve handle, set
 *                                                     {ship,object,component}block_ptr
 *     pick LOD root by objecteyez vs ShipModelData.lod_records
 *     create_getworldposition(obj_idx, 0)
 *     compute camera-relative position with shared exponent in relative*
 *     draw_gettreeorder(bsp_root)                  -- BSP painter sort,
 *                                                     fills comp[]
 *     draw_drawcraft(obj_idx, ship_flag, eyez)     -- per-mesh emit loop
 *
 * draw_polydepthsort is the polygon-plane tie-breaker called by
 * XTRANS2_getinfront when bbox separation fails.
 */

/* PolyFace — 8-byte per-face header shared by DRAW and DRAWPOL. The normal
 * vector is used for backface culling and lighting; vlist_offset is a
 * SIGNED self-relative byte offset to the face's vertex/edge topology
 * stream (flags+vcount header at +0, first vertex index at +3, 3-byte
 * per-vertex records after, with 0x7F00 continuation markers for shared
 * edges). The vertex stream may live before OR after the face header in
 * the model file, so the offset is genuinely signed. Verified against
 * the binary which uses `sar reg, 10h` (signed shift) at every read
 * site: DRAW_polydepthsort 0x1C4D8, DRAWPOL_dobsptree 0x1E3F9 / 0x1E4FB,
 * DRAWPOL_checknormal, DRAWPOL_drawpolyobject. DRAWPOL walks it as
 * vertices; DRAW_polydepthsort walks it as edges. */
#pragma pack(push, 2)
typedef struct PolyFace {
	int16_t normal_x;     /* +0x00 */
	int16_t normal_y;     /* +0x02 */
	int16_t normal_z;     /* +0x04 */
	int16_t vlist_offset; /* +0x06: signed self-relative byte offset to vertex/edge list */
} PolyFace;
#pragma pack(pop)

/* ---- DRAW-owned globals (per watdbg attribution to draw.c) ---- */
extern uint16_t comp[40];       /* BSP-visible mesh indices, filled by gettreeorder */
extern uint16_t highlightcolor; /* 0=normal, 1=bluetarget, 2=sub-component target */
extern uint16_t numberofcomp;   /* live count in comp[] */
extern int16_t relativeshift;   /* shared binary exponent for (relativex,y,z) */
extern int16_t relativex;       /* camera-relative position in ship local frame */
extern int16_t relativey;
extern int16_t relativez;

/* ---- API ---- */

/* Lock species[ship_idx].model_handle to obtain ship file pointer. Sets
 * shipimageptr / objectblockptr / componentblockptr. Returns the byte
 * size of the LOD-records sub-table (= 6 * num_lods). */
int draw_lockshipfileptrs(uint16_t ship_idx);

/* Resolve a mesh by ship_base + comp_idx. Sets componentblockptr.
 * Returns &mesh.lod_table (per-mesh detail-LOD table base). */
ShipMeshLOD* draw_getcomponentptr(ShipModelData* ship_base, uint16_t comp_idx);

/* Pick the polygon-detail pointer for the given mesh at the given base z.
 * Anchors on comp->pos_xyz (if has_position) or comp->center_*; rotates
 * via rotworldeye*3; clamps; calls draw_getdetailptr. Restores the
 * shipdetailpolycnt that drawcraft might have shifted. */
const uint16_t* draw_getcompdetailptr(ShipModelMesh* comp, int base_z);

/* Walk the per-mesh LOD dispatch table; return polygon header pointer
 * for z_threshold. shipdetailvalue selects the detail tier (axis is
 * INVERTED relative to the UI — -1 = HIGH detail, 0 = normal,
 * +1 = LOW detail; see tie.h's extern doc for the per-tier behaviour). */
const uint16_t* draw_getdetailptr(ShipMeshLOD* lod_table, int z_threshold);

/* Top-level entry for rendering a multi-mesh BSP-tree object. */
int draw_drawcomplexobject(int obj_idx);

/* Recursive BSP painter's-sort: appends visible mesh indices into comp[]
 * and increments numberofcomp. */
ShipModelMesh* draw_gettreeorder(int* bsp_node);

/* Draw all meshes in comp[0..numberofcomp-1] for one craft. Handles
 * highlight, damage skip, mesh rotation, decal flag, lightning arc. */
int draw_drawcraft(int obj_idx, uint32_t ship_flag, int eyez);
void draw_process_object_components_tie98(uint16_t object_ref);

/* Single-polygon draw for a laser bolt. */
void draw_drawlaser(uint16_t laser_obj_idx);
void draw_drawlaser_tie98(uint16_t laser_obj_idx);

/* Hyperspace starburst sprite. */
void draw_drawhyperstar(int16_t star_idx);
void draw_sync_tie98_hyperstar_state(void);
void draw_drawhyperstar_tie98(int16_t star_idx);

/* Rotated/scaled backdrop blit (planet/big-ship sprite). */
uint16_t draw_drawbackdropimage(uint16_t ship_idx, int16_t screen_x, int16_t screen_y, uint16_t angle);
uint16_t draw_drawbackdropimage_tie98(uint16_t ship_idx, int16_t screen_x, int16_t screen_y, uint16_t angle);

/* Polygon-plane depth tie-breaker. Returns obj_a or obj_b (the frontmost). */
uint16_t draw_polydepthsort(uint16_t a_face_info, uint16_t obj_a, uint16_t a_obj_id_field,
							uint16_t a_parent_category, int a_eyex, int a_eyey, uint16_t b_face_info,
							uint16_t obj_b, uint16_t b_parent_category, uint16_t b_obj_id_field);

#endif
