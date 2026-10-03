#include "tie/draw.h"
#include "tie/anim.h"
#include "tie/bpflight.h"
#include "tie/create.h"
#include "tie/drawpol.h"
#include "tie/fview.h"
#include "tie/laser.h"   /* WEAPON_SPECIES_COUNT, WEAPON_SPECIES_BASE */
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
#include "tie/xtrans2.h" /* flatobjnum */
#include "tie_runtime/runtime/wide_arithmetic.h"
#include "tie_runtime/snapshot/snapshot_billboards.h" /* SNAPSHOT-ONLY billboard capture */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* ============================================================================
 * Module-owned globals (per watdbg attribution to draw.c).
 * ========================================================================== */

// GLOBAL: TIE95 0xD3590
uint16_t comp[40];
// GLOBAL: TIE95 0xD35E0
uint16_t highlightcolor;
// GLOBAL: TIE95 0xD35E2
uint16_t numberofcomp;
// GLOBAL: TIE95 0xD35E4
int16_t relativeshift;
// GLOBAL: TIE95 0xD35E8
int16_t relativex;
// GLOBAL: TIE95 0xD35EA
int16_t relativey;
// GLOBAL: TIE95 0xD35E6
int16_t relativez;

// GLOBAL: TIE98 0x4E3D10
static Vec3f g_hyperspaceStreakQuadVertices[4] = {
	{ 64.0f, 0.0f, 0.0f },
	{ 64.0f, -256.0f, 0.0f },
	{ -64.0f, -256.0f, 0.0f },
	{ -64.0f, 0.0f, 0.0f },
};
typedef struct HyperspaceStreakFacePayloadTIE98 {
	int32_t edgeCount;
	FaceRecordTIE98 face;
	Vec3f faceNormal;
	FaceTextureGradientsTIE98 textureGradients;
} HyperspaceStreakFacePayloadTIE98;
/* The streak quad's OPT nodes and payloads form one contiguous block, as in
 * OpenXvT. The face's vertex-normal indices run past the single normal into
 * the following padding and node, as in the original. */
typedef struct HyperspaceStreakEmbeddedModelDataTIE98 {
	Tie98OptNode verticesNode;
	OptTexCoordTIE98 texCoords[4];
	Tie98OptNode texCoordsNode;
	Vec3f normal;
	int32_t normalNodePadding;
	Tie98OptNode normalsNode;
	HyperspaceStreakFacePayloadTIE98 facePayload;
	Tie98OptNode faceNode;
	Tie98OptNode* childNodes[4];
	Tie98OptNode rootNode;
	Tie98OptNode* rootNodes[1];
	int32_t trailingPadding;
} HyperspaceStreakEmbeddedModelDataTIE98;
// GLOBAL: TIE98 0x4E3D40
static HyperspaceStreakEmbeddedModelDataTIE98 g_hyperspaceStreakEmbeddedModelData = {
	{ NULL, TIE98_OPT_NODE_MESH_VERTICES, 0, NULL, { 4 }, g_hyperspaceStreakQuadVertices },
	{ { 1.0f, 0.0f }, { 1.0f, 1.0f }, { 0.0f, 1.0f }, { 0.0f, 0.0f } },
	{ NULL,
	  TIE98_OPT_NODE_TEXTURE_COORDINATES,
	  0,
	  NULL,
	  { 4 },
	  g_hyperspaceStreakEmbeddedModelData.texCoords },
	{ 0.0f, 0.0f, 1.0f },
	0,
	{ NULL, TIE98_OPT_NODE_VERTEX_NORMALS, 0, NULL, { 1 }, &g_hyperspaceStreakEmbeddedModelData.normal },
	{
		4,
		{ { 0, 1, 2, 3 }, { 0, 1, 2, 3 }, { 0, 0, 0, 0 }, { 0, 1, 2, 3 } },
		{ 0.0f, 0.0f, 1.0f },
		{ { 1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f } },
	},
	{ NULL, TIE98_OPT_NODE_FACE_DATA, 0, NULL, { 1 }, &g_hyperspaceStreakEmbeddedModelData.facePayload },
	{
		&g_hyperspaceStreakEmbeddedModelData.verticesNode,
		&g_hyperspaceStreakEmbeddedModelData.texCoordsNode,
		&g_hyperspaceStreakEmbeddedModelData.normalsNode,
		&g_hyperspaceStreakEmbeddedModelData.faceNode,
	},
	{ NULL,
	  TIE98_OPT_NODE_GROUP,
	  4,
	  g_hyperspaceStreakEmbeddedModelData.childNodes,
	  { 4 },
	  g_hyperspaceStreakEmbeddedModelData.childNodes },
	{ &g_hyperspaceStreakEmbeddedModelData.rootNode },
	0,
};
// GLOBAL: TIE98 0x4E3E68
static Tie98OptimizedPolyObject g_hyperspaceModelHeaderPatch = {
	0, 1, g_hyperspaceStreakEmbeddedModelData.rootNodes, NULL, 0, 0,
};
// GLOBAL: TIE98 0x591E30
uint32_t g_hyperspaceStreakLength;
// GLOBAL: TIE98 0x6268F4
uint16_t draw_object_mesh_count;
typedef char CheckLODRecordSize[sizeof(LODRecord) == 6 ? 1 : -1];
typedef char CheckShipModelMeshSize[sizeof(ShipModelMesh) == 64 ? 1 : -1];
/* ============================================================================
 * External references (cross-module globals/functions not declared elsewhere).
 * ========================================================================== */

/* objects[] FlightObject array, indexed by obj_idx. (defined in tie.c) */

/* hyperstardata + the per-star color byte at hyperstardata+0x14 are owned
 * by species.c (extern declared in species.h, included above). */

/* staticobjects[NUM_STATIC_OBJECTS] is owned by create.c (see tie.h). */

/* PolyFace moved to draw.h (cross-module shared with drawpol.c). */

/* ============================================================================
 * Helpers
 * ========================================================================== */

/* ============================================================================
 * draw_Lockshipfileptrs
 * ----------------------------------------------------------------------------
 * Resolve ship_idx → ship file pointer. Set the three module-global
 * pointers used by DRAW/FVIEW/COLLIDE/etc. Returns the byte size of the
 * LOD-records sub-table (= 6 * num_lods).
 * ========================================================================== */
// FUNCTION: TIE95 0x1AF50
int draw_Lockshipfileptrs(uint16_t ship_idx) {
	LandruHandle handle = species_table[ship_idx].model_handle;
	void* raw = xmemhdl_Lock_Handle(handle);
	ShipModelData* base;
	int compblock_offset;

	xmemhdl_Unlock_Handle(handle);

	if (!raw)
		return 0;

	/* Skip the two-byte file prefix. */
	base = (ShipModelData*)((uint8_t*)raw + 2);
	shipimageptr = base;
	objectblockptr = base;

	compblock_offset = 6 * base->num_lods;
	componentblockptr = (ShipModelMesh*)&base->lod_records[base->num_lods];
	return compblock_offset;
}

/* ============================================================================
 * draw_getcomponentptr
 * ----------------------------------------------------------------------------
 * Resolve mesh by locked ship file + comp_idx. Sets componentblockptr.
 * Returns &mesh + mesh.render_offset (per-mesh detail-LOD table base).
 * ========================================================================== */
// FUNCTION: TIE95 0x1AFB4
ShipMeshLOD* draw_getcomponentptr(uint8_t* ship_file, uint16_t comp_idx) {
	ship_file += 2;
	ship_file += ((ShipModelData*)ship_file)->num_lods * sizeof(struct LODRecord);
	ship_file += sizeof(ShipModelData);
	componentblockptr = (ShipModelMesh*)ship_file + comp_idx;
	return (ShipMeshLOD*)((uint8_t*)componentblockptr + componentblockptr->render_offset);
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
// FUNCTION: TIE95 0x1AFF0
const uint16_t* draw_getcompdetailptr(ShipModelMesh* comp, int base_z) {
	uint16_t saved_polycnt = shipdetailpolycnt;

	int16_t side, fwd, up;
	int rel_z;
	ShipMeshLOD* lod;
	const uint16_t* result;

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

	rel_z = rotworldeyeC3 * up + rotworldeyeB3 * fwd + rotworldeyeA3 * side;
	if (rel_z >= 0x40000000)
		rel_z = 1073676288;
	if (rel_z <= -1073741824)
		rel_z = -1073676288;

	lod = (ShipMeshLOD*)((uint8_t*)comp + comp->render_offset);
	result = draw_getdetailptr(lod, (rel_z >> 16) + base_z);
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
// FUNCTION: TIE95 0x1B0A8
const uint16_t* draw_getdetailptr(ShipMeshLOD* lod_table, int z_threshold) {
	int detail_mode = (uint16_t)shipdetailvalue;
	ShipMeshLOD* p;
	const uint8_t* poly_header;
	ShipMeshLOD* cur;

	if (detail_mode == 0xFFFF) {
		z_threshold >>= 1;
		detail_mode = 0;
	}

	cur = lod_table;
	p = cur;
	while (z_threshold > cur->distance) {
		cur++;
		p = cur;
	}

	if (detail_mode <= 0)
		return (const uint16_t*)((uint8_t*)cur + cur->offset);

	if (p->distance == 0x7FFFFFFF)
		return (const uint16_t*)((uint8_t*)cur + cur->offset);

	poly_header = (const uint8_t*)cur + cur->offset;
	if ((poly_header[0] & 0xFE) == 0x40 || poly_header[4] > (int)shipdetailpolycnt)
		++cur;

	return (const uint16_t*)((uint8_t*)cur + cur->offset);
}

/* ============================================================================
 * draw_drawcomplexobject
 * ----------------------------------------------------------------------------
 * Top-level entry for rendering a multi-mesh BSP-tree object.
 *
 * 1. Resolve ship_idx (mesh objects: objects[i].field_0[4]; static
 *    objects >= OBJ_REF_STATIC_BASE: staticobjects[idx-OBJ_REF_STATIC_BASE].species.
 * 2. drawpol_setmarkingcolors for decals.
 * 3. draw_Lockshipfileptrs to set object/component pointers.
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
// FUNCTION: TIE95 0x1B12C
void draw_drawcomplexobject(uint16_t obj_idx) {
	uint8_t* bsp_root;
	uint16_t num_lods;
	LODRecord* lod;
	uint16_t i;
	int dy;
	int dx;
	uint16_t ship_idx;
	int dz;
	int dx_abs;
	int dy_abs;
	int dz_abs;
	uint16_t dx_bits;
	uint16_t dy_bits;
	uint16_t dz_bits;
	int sign;
	int sx;
	int sy;
	int sz;
	int rel;

	if (obj_idx >= (int)OBJ_REF_STATIC_BASE) {
#ifdef TIE_MODERN
		if (obj_idx - OBJ_REF_STATIC_BASE >= NUM_STATIC_OBJECTS)
			ship_idx = 0;
		else
#endif
			ship_idx = staticobjects[obj_idx - OBJ_REF_STATIC_BASE].species;
	} else {
		ship_idx = objects[obj_idx].ship_idx;
		drawpol_setmarkingcolors(objects[obj_idx].decal_color);
	}
	draw_Lockshipfileptrs(ship_idx);

#ifdef TIE_MODERN
	if (!objectblockptr) {
		drawpol_setmarkingcolors(0);
		return;
	}
#endif
	if (objecteyez < objectblockptr->render_distance) {
		/* Walk the ship-level LOD dispatch table picking the BSP root for
		 * the current eye-z distance. Each LODRecord is 6 bytes. */
		lod = objectblockptr->lod_records;
		num_lods = objectblockptr->num_lods;
		bsp_root = (uint8_t*)lod;
		for (i = 0; i < num_lods; ++i) {
			bsp_root = (uint8_t*)&lod[i] + lod[i].bsp_offset;
			if (objecteyez > (int32_t)lod[i].z_max)
				break;
		}

		create_getworldposition(obj_idx, 0);

		dx = camera.x - worldlocx;
		dy = camera.y - worldlocy;
		dz = camera.z - worldlocz;
		dx <<= 1;
		dy <<= 1;
		dz <<= 1;

		dx_abs = dx >> 16;
		dy_abs = dy >> 16;
		dz_abs = dz >> 16;
		sign = dx_abs & 0x8000;
		if ((int)(uint16_t)sign)
			dx_abs = -dx_abs;
		sign = dy_abs & 0x8000;
		if ((int)(uint16_t)sign)
			dy_abs = -dy_abs;
		sign = dz_abs & 0x8000;
		if ((int)(uint16_t)sign)
			dz_abs = -dz_abs;

		dx_bits = 2 * dx_abs;
		dy_bits = 2 * dy_abs;
		dz_bits = 2 * dz_abs;
		relativeshift = -1;
		do {
			dx_bits >>= 1;
			dy_bits >>= 1;
			dz_bits >>= 1;
			dx >>= 1;
			dy >>= 1;
			dz >>= 1;
			++relativeshift;
		} while (dx_bits || dy_bits || dz_bits);

		sz = (int16_t)dz;
		sy = (int16_t)dy;
		sx = (int16_t)dx;

		rel = math2_dot3(craftS1, sx, craftS2, sy, craftS3, sz);
		if (rel >= 0x40000000)
			rel = 1073676288;
		if (rel <= -1073741824)
			rel = -1073676288;
		relativex = rel >> 15;

		rel = math2_dot3(craftf1, sx, craftf2, sy, craftf3, sz);
		if (rel >= 0x40000000)
			rel = 1073676288;
		if (rel <= -1073741824)
			rel = -1073676288;
		relativey = rel >> 15;
		relativey = -relativey;

		rel = math2_dot3(craftU1, sx, craftU2, sy, craftU3, sz);
		if (rel >= 0x40000000)
			rel = 1073676288;
		if (rel <= -1073741824)
			rel = -1073676288;
		relativez = rel >> 15;

		relativeshift -= (int8_t)objectblockptr->model_scale_shift;
		numberofcomp = 0;
		draw_gettreeorder((int*)(bsp_root + 2));
		draw_drawcraft(obj_idx, ship_idx);
	}

	drawpol_setmarkingcolors(0);
}

// FUNCTION: TIE98 0x417EC0
void draw_drawlaser_tie98(uint16_t laser_obj_idx) {
	FlightObject* object = &objects[laser_obj_idx];
	int32_t camera_x;
	int32_t camera_y;
	int32_t camera_z;
	int32_t up_dot;
	int32_t side_dot;
	int16_t saved_roll;

	parentobject = laser_obj_idx;
	camera_x = camera.x - object->world_x;
	camera_y = camera.y - object->world_y;
	camera_z = camera.z - object->world_z;
	/* Retail shifts each full product before the wrapping 32-bit sum. */
	up_dot = (int32_t)((uint32_t)math2_mul_q15(camera_x, object->up_x) +
					   (uint32_t)math2_mul_q15(camera_y, object->up_y) +
					   (uint32_t)math2_mul_q15(camera_z, object->up_z));
	side_dot = (int32_t)((uint32_t)math2_mul_q15(camera_x, object->side_x) +
						 (uint32_t)math2_mul_q15(camera_y, object->side_y) +
						 (uint32_t)math2_mul_q15(camera_z, object->side_z));
	saved_roll = object->roll;
	object->roll += (int16_t)(trig2_arctan(up_dot, side_dot) - 0x4000);
	object->orient_dirty = 1;
	fview_newcalcrotate(object->roll, object->pitch, object->heading, 0, object);
	FlightModel_Draw_Object(object);
	object->roll = saved_roll;
	object->orient_dirty = 1;
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
// FUNCTION: TIE95 0x1B414
void draw_gettreeorder(int* bsp_node) {
	BSPNode* node = (BSPNode*)bsp_node;

	for (;;) {
		BSPNode* current = node;

		if (current->left_off == 0) {
			/* Leaf: right_off holds the mesh index. */
			ShipModelMesh* mesh = &componentblockptr[current->right_off];
			int comp_eyez = objecteyez;

			if (mesh->has_position) {
				int rel = math2_dot3_q15_clamped(rotworldeyeA3, rotworldeyeB3, rotworldeyeC3, mesh->pos_side,
												 mesh->pos_fwd, mesh->pos_up);
				comp_eyez += rel >> 1;

				if ((uint16_t)shipdetailvalue == 1) {
					ShipMeshLOD* probe = (ShipMeshLOD*)((uint8_t*)mesh + mesh->render_offset);
					if (probe->distance == 0x7FFFFFFF) {
						/* Skip-marker: prune the leaf entirely. */
						return;
					}
				}
			}

			if (shipdetailvalue == -1)
				comp_eyez >>= 1;

			if (comp_eyez <= mesh->draw_distance)
				comp[numberofcomp++] = current->right_off;
			return;
		} else {
			int16_t pt_side, pt_fwd, pt_up;
			int plane_dot;

			if (relativeshift < 0) {
				int sh = -relativeshift;
				pt_side = (relativex >> sh) - current->center_x;
				pt_fwd = (relativey >> sh) - current->center_y;
				pt_up = (relativez >> sh) - current->center_z;
			} else if (relativeshift > 0) {
				pt_side = relativex - (current->center_x >> relativeshift);
				pt_fwd = relativey - (current->center_y >> relativeshift);
				pt_up = relativez - (current->center_z >> relativeshift);
			} else {
				pt_side = relativex - current->center_x;
				pt_fwd = relativey - current->center_y;
				pt_up = relativez - current->center_z;
			}

			plane_dot = math2_dot3_q15_clamped(pt_side, pt_fwd, pt_up, current->normal_x, current->normal_y,
											   current->normal_z);

			if ((int16_t)plane_dot >= 0) {
				draw_gettreeorder((int*)((uint8_t*)node + current->left_off));
				node = (BSPNode*)((uint8_t*)node + current->right_off);
			} else {
				/* Camera on negative side: recurse RIGHT, tail-walk LEFT. */
				draw_gettreeorder((int*)((uint8_t*)node + current->right_off));
				node = (BSPNode*)((uint8_t*)node + current->left_off);
			}
		}
	}
}

// FUNCTION: TIE98 0x42F990
// DRAW_drawhyperstar
void draw_drawhyperstar_tie98(int16_t star_idx) {
	const int saved_bilinear = g_bilinearEnabled;
	FlightObject saved_object = objects[0];
	const Tie98OptimizedPolyObject* saved_model_override = g_flightModelOverride;

	g_hyperspaceStreakQuadVertices[1].y = (float)(g_hyperspaceStreakLength >> 1);
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
	objects[0].heading = 0;
	objects[0].pitch = 0x4000;
	objects[0].orient_dirty = 1;
	fview_newcalcrotate(objects[0].roll, 0x4000, 0, 0, &objects[0]);
	g_flightModelOverride = &g_hyperspaceModelHeaderPatch;
	FlightModel_Draw_Object(&objects[0]);
	g_flightModelOverride = saved_model_override;
	g_bilinearEnabled = saved_bilinear;
	objects[0] = saved_object;
}

/* Draw visible craft components with their articulation, markings, and target
 * highlighting. Critically damaged fuselages may also emit a lightning
 * billboard. Restores currenttarget before returning. */
// FUNCTION: TIE95 0x1B690
void draw_drawcraft(uint16_t obj_idx, uint16_t ship_flag) {
	uint16_t saved_currenttarget;
	int16_t bolt_angle_set;
	int16_t bolt_angle;
	uint16_t comp_iter;

	parentobject = obj_idx;
	saved_currenttarget = currenttarget;
#ifdef TIE_MODERN
	/* The original never clears this flag before its first test. */
	bolt_angle_set = 0;
#endif
	highlightcolor = 0;
	if (obj_idx == bluetarget) {
		currenttarget = obj_idx;
		highlightcolor = 1;
	}

	draw_Lockshipfileptrs(ship_flag);
	/* model_scale_shift==2 routes drawpol through transfm2_geteyecoordsS2
	 * (>> 14 instead of >> 16, 4× eye-space contribution) by pushing
	 * parentobject's HIBYTE past 0x50 — see drawpol.c:1265/1331-1334.
	 * Capital-class ships (e.g. VSD) ship with this value. */
	if ((int8_t)objectblockptr->model_scale_shift == 2) {
		parentobject += 0x7000;
		if (mission.train_craft_type)
			currenttarget += 0x7000;
	}

	for (comp_iter = 0; comp_iter < numberofcomp; ++comp_iter) {
		uint16_t comp_idx = comp[comp_iter];
		ShipModelMesh* mesh = &componentblockptr[comp_idx];
		uint16_t mesh_type = mesh->mesh_type;
		int16_t saved_drawmarkingsflag;
		int16_t rot_angle;
		uint16_t saved_curtarget;
		uint16_t saved_polycnt;

		solidindex = comp_idx;

		if (highlightcolor == 2)
			highlightcolor = 0;

		/* Sub-component target highlight check. */
		if (comp_idx == currenttargetcomp) {
			if (!highlightcolor)
				highlightcolor = 2;
		} else if (currenttargetcomp < (int16_t)objectblockptr->num_meshes) {
			if (mesh->has_position > 1 || (mesh->has_position == 1 && mesh_type == 1 /*MESH_MainHull*/)) {
				ShipModelMesh* tgt_comp = &componentblockptr[currenttargetcomp];
				if (mesh->has_position == tgt_comp->has_position && mesh_type == tgt_comp->mesh_type &&
					!highlightcolor) {
					highlightcolor = 2;
				}
			}
		}
		if (obj_idx < NUM_CRAFTS) {
			if ((uint16_t)craftptr->mesh_state[comp_idx] != MESH_STATE_VISIBLE)
				continue; /* hidden / blown off -- skip */
			rot_angle = craftptr->mesh_rotation[comp_idx];
			if (mesh->rotation_offset || mission.train_craft_type) {
				if (rot_angle)
					fview_componentrotation((uint16_t)(rot_angle << 8), mesh);
			} else {
				rot_angle = 0;
			}
		} else {
			rot_angle = 0;
		}

		saved_drawmarkingsflag = drawmarkingsflag;
		/* Species 17 wing meshes hide their insignia for non-friendly
		 * craft. Binary @0x1b884: `a2 == 17` where a2 is ship_flag
		 * (second arg, DX). Mirrors the parallel override in
		 * ANIM_drawverysimpleobject at anim.c:407. */
		if (ship_flag == 17 && mesh->mesh_type == 2 /*MESH_Wing*/ && obj_idx < 0x3800) {
			if (objects[obj_idx].side == 0)
				drawmarkingsflag = 1;
			else
				drawmarkingsflag = 0;
		}

		saved_polycnt = shipdetailpolycnt;
		saved_curtarget = currenttarget;
		if (currenttarget != 0xFFFFu && highlightcolor == 2 && (currenttarget & 0x200) != 0) {
			currenttarget = parentobject;
		}

		drawpol_drawpolyobject(draw_getcompdetailptr(mesh, objecteyez), objecteyex, objecteyey, objecteyez);

		shipdetailpolycnt = saved_polycnt;
		drawmarkingsflag = saved_drawmarkingsflag;
		currenttarget = saved_curtarget;

		if (rot_angle)
			fview_restorerotation();

		/* Lightning arc on Fuselage (mesh_type==3) for live craft. */
		if (mesh_type == 3 /*MESH_Fuselage*/ && obj_idx < NUM_CRAFTS) {
			/* mesh_state[num_meshes] is the byte just past the per-mesh
			 * state range; the binary overlays it as the lightning anim
			 * frame index (0..24 indexes lightning[25]). */
			uint8_t lightning_frame = craftptr->mesh_state[objectblockptr->num_meshes];
			AnimOp lightning_obj;
			int32_t screen_x;
			int32_t screen_y;
			int half_pd;

#ifdef TIE_MODERN
			if (lightning_frame >= 25)
				continue;
#endif
			lightning_obj = lightning[lightning_frame];
			if (lightning_obj < 0x8000 || lightning_obj >= 0xFF00)
				continue; /* on a header/jump frame -- no bolt this tick */
			if (!bolt_angle_set) {
				int32_t arc_dx, arc_dy;
				int32_t abs_b3 = rotworldeyeB3;
				int32_t abs_a3 = rotworldeyeA3;
				if (abs_a3 < 0)
					abs_a3 = -abs_a3;
				if (abs_b3 < 0)
					abs_b3 = -abs_b3;
				if (abs_a3 < abs_b3) {
					arc_dx = rotworldeyeA1;
					arc_dy = rotworldeyeA2;
				} else {
					arc_dx = rotworldeyeB1;
					arc_dy = rotworldeyeB2;
				}
				if (arc_dx < 0)
					bolt_angle = trig2_arctan(arc_dy, -arc_dx);
				else
					bolt_angle = -trig2_arctan(arc_dy, arc_dx);
				bolt_angle_set = 1;
			}
			/* The roll accumulates into the cached angle each emit. */
			bolt_angle += objects[obj_idx].roll;
			screen_x = transfm2_getscreenx(objecteyex, objecteyez);
			if ((screen_x >> 16) > 0 || (screen_x >> 16) < -1)
				continue;
			screen_y = transfm2_getscreeny(objecteyey, objecteyez);
			if ((screen_y >> 16) > 0 || (screen_y >> 16) < -1)
				continue;
			half_pd = pixelsdeep >> 1;
			half_pd -= screen_y - half_pd;
			anim_add_bitmap_draw(parentobject, lightning_obj, 256, (int16_t)screen_x, (int16_t)half_pd,
								 objecteyez, bolt_angle);
#ifdef TIE_MODERN
			{
				/* SNAPSHOT capture — does NOT affect classic render.
				 * Same calcscale call the engine's anim_draw_bitmap
				 * would do (with damage_factor = 256 since lightning
				 * passes a fixed scale to anim_add_bitmap_draw).
				 * Bolt is anchored at parent craft world origin in
				 * anim_draw_bitmap; emit reads world_*_prev. */
				uint8_t lb_sp = (uint8_t)((lightning_obj & 0x7FFFu) >> 7);
				uint16_t lb_bw = species_table[lb_sp].bound_hwidth;
				uint16_t lb_psc = (uint16_t)rotscale_calcscale(objecteyez, lb_bw, 256);
				TieBillboardCapture_Lightning(obj_idx, lightning_obj, lb_psc, lb_bw, bolt_angle);
			}
#endif
		}
	}

	currenttarget = saved_currenttarget;
}

/* ============================================================================
 * draw_drawlaser
 * ----------------------------------------------------------------------------
 * Draw a single laser bolt (FlightObject as a single polygon) from the
 * built-in projectiledataptrs[] model; the lockshipfileptrs fallback
 * covers the NULL slot.
 * ========================================================================== */
// FUNCTION: TIE95 0x1BAB4
void draw_drawlaser(uint16_t laser_obj_idx) {
	uint16_t ship_idx;
	ShipMeshLOD* poly_table;
	const uint16_t* poly;
	int eyex, eyey, eyez;

	parentobject = laser_obj_idx;
	ship_idx = objects[laser_obj_idx].ship_idx;

	poly_table = (ShipMeshLOD*)projectiledataptrs[ship_idx - WEAPON_SPECIES_BASE];
	if (!poly_table) {
		draw_Lockshipfileptrs(ship_idx);
		poly_table = (ShipMeshLOD*)((uint8_t*)componentblockptr + componentblockptr->render_offset);
	}

	eyey = objecteyey;
	eyex = objecteyex;
	eyez = objecteyez;
	poly = draw_getdetailptr(poly_table, objecteyez);
	drawpol_drawpolyobject(poly, eyex, eyey, eyez);
}

/* ============================================================================
 * draw_drawhyperstar
 * ----------------------------------------------------------------------------
 * Hyperspace starburst sprite at eye-space objecteyex/y/z. parentobject is
 * tagged OBJ_REF_STATIC_BASE + star_idx (the "static/flat-poly" slice of
 * the obj-ref namespace; see tie.h). byte_DC3AC = (star_idx & 3) - 4.
 * Saves/restores flatobjnum so the caller's flat-poly ring is unaffected.
 * ========================================================================== */
// FUNCTION: TIE95 0x1BB28
void draw_drawhyperstar(uint16_t star_idx) {
	uint16_t saved;

	parentobject = (uint16_t)(star_idx + OBJ_REF_STATIC_BASE);
	hyperstardata[0x14] = (star_idx & 3) + 0xFC;
	saved = flatobjnum;
	drawpol_drawpolyobject((const uint16_t*)hyperstardata, objecteyex, objecteyey, objecteyez);
	flatobjnum = saved;
}

/* ============================================================================
 * draw_drawbackdropimage
 * ----------------------------------------------------------------------------
 * Rotated/scaled backdrop blit (planet, large-distance ship sprite).
 * Reads species[ship_idx].model_handle for the bitmap blob and
 * species[ship_idx].bitmap_data for the palette remap.
 * ========================================================================== */
// FUNCTION: TIE95 0x1BB70
// FUNCTION: TIE98 0x417FF0
uint16_t draw_drawbackdropimage(uint16_t ship_idx, int16_t screen_x, int16_t screen_y, uint16_t angle) {
	const uint8_t* bitmap_base;
	const uint8_t* image;

	reverseflag = 1;
	worldz = 0x100000;
	if (TIE_FLIGHT_TIE98)
		objecteyez = 0x7FFFFFFF;
	bitmap_base = (const uint8_t*)xmemhdl_Lock_Handle(species_table[ship_idx].model_handle);
	xmemhdl_Unlock_Handle(species_table[ship_idx].model_handle);
#ifdef TIE_MODERN
	/* PORT: neither original checks for a missing backdrop bitmap. */
	if (!bitmap_base)
		return 0;
#endif

	/* Retail bitmaps use a two-level offset to their palette and image data. */
	image = bitmap_base + *(const uint32_t*)(bitmap_base + 16);
	image = bitmap_base + *(const uint32_t*)image;

#if defined(TIE98) || defined(TIE_MODERN)
	if (TIE_DISPLAY_DX5 && g_useHardware3D) {
		RenderQuad_DrawRotatedSprite(angle, screen_x, screen_y, 0x100, image);
		return 0;
	}
#endif
	rotscale_preparefastdraw(angle, 2);
	rotscale_preparecolor((const char*)image);
	return rotscale_rotatescaleimage(screen_x, screen_y, 0x100, image);
}

// FUNCTION: TIE98 0x417C40
// DRAW_drawcraft
static void draw_drawcraft_tie98(int object_arg, int model_arg) {
	const uint16_t object_ref = (uint16_t)object_arg;
	const uint16_t model_type = (uint16_t)model_arg;
	uint16_t saved_current_target;
	int16_t bolt_angle;
	int16_t bolt_angle_set;
	uint16_t mesh_index;

#ifdef TIE_MODERN
	/* PORT: TIE98 never clears the cached-bolt-angle flag. */
	bolt_angle_set = 0;
#endif
	parentobject = object_ref;
	highlightcolor = 0;
	saved_current_target = currenttarget;
	if (object_ref == bluetarget) {
		currenttarget = object_ref;
		highlightcolor = 1;
	}

	draw_object_mesh_count = (uint16_t)modelmesh_getobjecttypemeshcount(model_type);
	for (mesh_index = 0; mesh_index < draw_object_mesh_count; ++mesh_index) {
		uint16_t mesh_type;

		solidindex = mesh_index;
		mesh_type = (uint16_t)modelmesh_getobjecttypemeshtype(model_type, mesh_index);
		if (highlightcolor == 2)
			highlightcolor = 0;
		if (currenttargetcomp == mesh_index && highlightcolor == 0)
			highlightcolor = 2;

		if (object_ref < NUM_CRAFTS) {
			uint16_t mesh_state = 0;
			if (objects[object_ref].craft_ptr)
				mesh_state = objects[object_ref].craft_ptr->mesh_state[mesh_index];
			if (mesh_state != MESH_STATE_VISIBLE)
				continue;
		}

		if (mesh_type == TIE_MESH_FUSELAGE && object_ref < NUM_CRAFTS) {
			uint16_t lightning_slot = draw_object_mesh_count;
			uint16_t lightning_frame = 0;
			AnimOp lightning_op;
			int32_t screen_x;
			int32_t screen_y;
			int half_height;

			if (objects[object_ref].craft_ptr)
				lightning_frame = objects[object_ref].craft_ptr->mesh_state[lightning_slot];
#ifdef TIE_MODERN
			if (lightning_frame >= 25)
				continue;
#endif
			lightning_op = lightning[lightning_frame];
			if (lightning_op < 0x8000 || lightning_op >= 0xFF00)
				continue;
			if (!bolt_angle_set) {
				int32_t arc_dx, arc_dy;
				int16_t angle;
				int32_t abs_a3 = rotworldeyeA3;
				int32_t abs_b3 = rotworldeyeB3;
				if (abs_a3 < 0)
					abs_a3 = -abs_a3;
				if (abs_b3 < 0)
					abs_b3 = -abs_b3;
				if (abs_a3 < abs_b3) {
					arc_dx = rotworldeyeA1;
					arc_dy = rotworldeyeA2;
				} else {
					arc_dx = rotworldeyeB1;
					arc_dy = rotworldeyeB2;
				}
				if (arc_dx < 0)
					angle = trig2_arctan(arc_dy, -arc_dx);
				else
					angle = -trig2_arctan(arc_dy, arc_dx);
				bolt_angle = angle;
				bolt_angle_set = 1;
			}
			/* The roll accumulates into the cached angle each emit. */
			bolt_angle += objects[object_ref].roll;
			screen_x = transfm2_getscreenx(objecteyex, objecteyez);
			if ((screen_x & ~0xFFFF) > 0 || (screen_x & ~0xFFFF) < -0x10000)
				continue;
			screen_y = transfm2_getscreeny(objecteyey, objecteyez);
			if ((screen_y & ~0xFFFF) > 0 || (screen_y & ~0xFFFF) < -0x10000)
				continue;
			half_height = pixelsdeep >> 1;
			anim_add_bitmap_draw(parentobject, lightning_op, 0x100, (int16_t)screen_x,
								 (int16_t)(2 * half_height - screen_y), objecteyez, bolt_angle);
#ifdef TIE_MODERN
			{
				/* SNAPSHOT capture — does NOT affect classic render. */
				uint8_t bitmap_species = (uint8_t)((lightning_op & 0x7FFFu) >> 7);
				uint16_t bound_hwidth = species_table[bitmap_species].bound_hwidth;
				uint16_t pixel_scale = (uint16_t)rotscale_calcscale(objecteyez, bound_hwidth, 0x100);
				TieBillboardCapture_Lightning(object_ref, lightning_op, pixel_scale, bound_hwidth,
											  bolt_angle);
			}
#endif
		}
	}
	currenttarget = saved_current_target;
}

// FUNCTION: TIE98 0x417BE0
void draw_drawcomplexobject_tie98(uint16_t object_ref) {
	uint16_t model_type;

	if (object_ref >= OBJ_REF_STATIC_BASE)
		model_type = staticobjects[object_ref - OBJ_REF_STATIC_BASE].species;
	else
		model_type = objects[object_ref].ship_idx;
	draw_drawcraft_tie98(object_ref, model_type);
}

/* Resolve ambiguous XTRANS2 depth ordering. Category flags handle fixed
 * priority cases; mesh overlaps compare the camera vector against both
 * polygon planes after resolving 0x7F00 vertex back-references. */
// FUNCTION: TIE95 0x1BBF4
uint16_t draw_polydepthsort(uint16_t a_face_info, uint16_t obj_a, uint16_t a_parent_category,
							uint16_t a_obj_id_field, uint16_t b_face_info, uint16_t obj_b,
							uint16_t b_parent_category, uint16_t b_obj_id_field) {
	uint16_t front_obj;
	uint16_t back_obj;
	uint8_t a_cat;
	uint8_t a_slot;
	uint8_t b_cat;
	uint8_t b_slot;
	uint8_t via_special_70;
	uint8_t norm_shift;
	uint8_t relationship;
	uint16_t a_ship_idx;
	uint16_t b_ship_idx;
	uint16_t owner_ship_idx;
	uint16_t a_size;
	uint16_t b_size;
	uint16_t face;
	uint16_t obj_id_field;
	int a_x, a_y, a_z;
	int b_x, b_y, b_z;
	int owner_x, owner_y, owner_z;
	int other_x, other_y, other_z;
	int other_dx, other_dy, other_dz;
	int camera_dx, camera_dy, camera_dz;
	uint16_t dx_abs, dy_abs, dz_abs;
	int dot;
	int other_side, other_fwd, other_up;
	int cam_side, cam_fwd;
	int16_t cam_up;
	FlightObject* owner_obj;
	CraftData* b_craftptr;

	if (bpflightflag)
		return obj_a;

	back_obj = obj_a;
	front_obj = obj_b;
	a_cat = a_parent_category >> 8;
	a_slot = (uint8_t)a_parent_category;
	via_special_70 = 0;
	a_size = 0;
	b_size = 0;
	a_ship_idx = 0;
	b_ship_idx = 0;
	a_x = a_y = a_z = 0;
	b_x = b_y = b_z = 0;

	switch (a_cat) {
		case 0x00:
		case 0x70:
			a_x = objects[a_slot].world_x;
			a_ship_idx = objects[a_slot].ship_idx;
			a_y = objects[a_slot].world_y;
			a_z = objects[a_slot].world_z;
			if (objects[a_slot].ship_idx == 89)
				a_ship_idx = objects[a_slot].ship_type_override;
			a_size = species_table[a_ship_idx].bound_hwidth;
			break;
		case 0x10:
		case 0x20:
		case 0x30:
		case 0x38:
			return front_obj;
		case 0x78:
			return obj_b;
	}

	b_cat = b_parent_category >> 8;
	b_slot = (uint8_t)b_parent_category;
	switch (b_cat) {
		case 0x00:
		case 0x70:
			b_x = objects[b_slot].world_x;
			b_y = objects[b_slot].world_y;
			b_z = objects[b_slot].world_z;
			b_ship_idx = objects[b_slot].ship_idx;
			if (objects[b_slot].ship_idx == 89)
				b_ship_idx = objects[b_slot].ship_type_override;
			b_size = species_table[b_ship_idx].bound_hwidth;
			break;
		case 0x10:
		case 0x20:
		case 0x30:
		case 0x38:
		case 0x78:
			return back_obj;
	}

	/* Non-craft meshes never win on size. */
	if (a_cat == 0 && a_slot >= NUM_CRAFTS) {
		if (b_cat == 0 && b_slot >= NUM_CRAFTS)
			return front_obj;
		a_size = 0;
	} else if (b_cat == 0 && b_slot >= NUM_CRAFTS) {
		b_size = 0;
	}

	craftptr = objects[a_slot].craft_ptr;
	b_craftptr = objects[b_slot].craft_ptr;
	relationship = 0;
	if (a_slot < NUM_CRAFTS) {
		if ((uint16_t)craftptr->tow_slave_ref == b_slot || craftptr->tow_slave_ref == b_parent_category ||
			(craftptr->mode_byte == 18 && craftptr->mode_subbyte == 2 &&
			 (uint16_t)craftptr->ai_target_ref == b_slot))
			relationship = 1;
	}
	if (b_slot < NUM_CRAFTS) {
		if ((uint16_t)b_craftptr->tow_slave_ref == a_slot || b_craftptr->tow_slave_ref == a_parent_category ||
			(b_craftptr->mode_byte == 18 && b_craftptr->mode_subbyte == 2 &&
			 (uint16_t)b_craftptr->ai_target_ref == a_slot))
			relationship = 2;
	}

	if (relationship == 2 || (relationship != 1 && a_cat != 0x70 && (a_size <= b_size || b_cat == 0x70))) {
		/* B owns the plane test; A is the other object. */
		owner_ship_idx = b_ship_idx;
		b_ship_idx = a_ship_idx;
		face = b_face_info;
		owner_obj = &objects[b_slot];
		craftptr = objects[b_slot].craft_ptr;
		obj_id_field = b_obj_id_field;
		owner_x = b_x;
		owner_y = b_y;
		owner_z = b_z;
		other_x = a_x;
		other_y = a_y;
		other_z = a_z;
		back_obj = obj_b;
		front_obj = obj_a;
		if (b_cat == 0x70)
			via_special_70 = 1;
	} else {
		owner_ship_idx = a_ship_idx;
		face = a_face_info;
		owner_obj = &objects[a_slot];
		obj_id_field = a_obj_id_field;
		owner_x = a_x;
		owner_y = a_y;
		owner_z = a_z;
		other_x = b_x;
		other_y = b_y;
		other_z = b_z;
		if (a_cat == 0x70)
			via_special_70 = 1;
	}

	other_dx = other_x - owner_x;
	camera_dx = camera.x - owner_x;
	camera_dy = camera.y - owner_y;
	camera_dz = camera.z - owner_z;
	norm_shift = 0;
	other_dy = other_y - owner_y;
	other_dz = other_z - owner_z;
	dx_abs = other_dx >> 14;
	dy_abs = other_dy >> 14;
	dz_abs = other_dz >> 14;
	if (dx_abs & 0x8000)
		dx_abs = -dx_abs;
	if (dy_abs & 0x8000)
		dy_abs = -dy_abs;
	if (dz_abs & 0x8000)
		dz_abs = -dz_abs;
	do {
		other_dx >>= 1;
		camera_dx >>= 1;
		camera_dy >>= 1;
		other_dy >>= 1;
		other_dz >>= 1;
		camera_dz >>= 1;
		dx_abs >>= 1;
		dy_abs >>= 1;
		norm_shift++;
		dz_abs >>= 1;
	} while (dx_abs || dy_abs || dz_abs);

	/* Project both vectors into the owner's local frame. */
	other_side = math2_dot3_q15_clamped(owner_obj->side_x, owner_obj->side_y, owner_obj->side_z,
										(int16_t)other_dx, (int16_t)other_dy, (int16_t)other_dz);
	other_fwd = -math2_dot3_q15_clamped(owner_obj->fwd_x, owner_obj->fwd_y, owner_obj->fwd_z,
										(int16_t)other_dx, (int16_t)other_dy, (int16_t)other_dz);
	other_up = math2_dot3_q15_clamped(owner_obj->up_x, owner_obj->up_y, owner_obj->up_z, (int16_t)other_dx,
									  (int16_t)other_dy, (int16_t)other_dz);

	cam_side = math2_dot3_q15_clamped(owner_obj->side_x, owner_obj->side_y, owner_obj->side_z,
									  (int16_t)camera_dx, (int16_t)camera_dy, (int16_t)camera_dz);
	cam_fwd = -math2_dot3_q15_clamped(owner_obj->fwd_x, owner_obj->fwd_y, owner_obj->fwd_z,
									  (int16_t)camera_dx, (int16_t)camera_dy, (int16_t)camera_dz);
	cam_up = (int16_t)math2_dot3_q15_clamped(owner_obj->up_x, owner_obj->up_y, owner_obj->up_z,
											 (int16_t)camera_dx, (int16_t)camera_dy, (int16_t)camera_dz);

	if (via_special_70)
		norm_shift--;
	else
		norm_shift++;

	/* Docked pairs: compare the camera height against the dock anchor. */
	if (relationship) {
		draw_Lockshipfileptrs(b_ship_idx);
		if (spec_data[spec_getspecnum(b_ship_idx)].dock_passive_light ==
			objectblockptr->speed_default >> 17) {
			draw_Lockshipfileptrs(owner_ship_idx);
			if (spec_data[spec_getspecnum(owner_ship_idx)].dock_active_light ==
				objectblockptr->shield_default >> 16 >> 1) {
				if (cam_up < objectblockptr->shield_default >> 16 >> norm_shift)
					return front_obj;
				return back_obj;
			}
		}
	}

	/* Full polygon-plane test. 0x7F00 vertex entries refer back to an
	 * earlier vertex component. */
	{
		ShipModelMesh* mesh;
		uint16_t rotation;
		const uint8_t* detail;
		uint8_t lod;
		uint16_t num_polys;
		const uint8_t* poly_list;
		const PolyFace* plane;
		const uint8_t* vlist;
		int16_t normal_x;
		int16_t normal_y;
		int16_t normal_z;
		const int16_t* vertex;
		int16_t edge;
		int16_t other_side_dx, other_fwd_dx, other_up_dx;
		int16_t cam_side_dx, cam_fwd_dx, cam_up_dx;
		int other_dot;

		draw_Lockshipfileptrs(owner_ship_idx);
		mesh = &componentblockptr[obj_id_field];
		fview_newcalcrotate(owner_obj->roll, owner_obj->pitch, owner_obj->heading, 0, owner_obj);
		rotation = craftptr->mesh_rotation[obj_id_field];
		if (rotation && (mesh->rotation_offset || mission.train_craft_type))
			fview_componentrotation((uint16_t)(rotation << 8), mesh);
		detail = (const uint8_t*)draw_getcompdetailptr(mesh, craftptr->eye_z_cache);
		lod = detail[2];
		num_polys = detail[4];
		if (num_polys <= face)
			face = num_polys - 1;
		poly_list = detail + num_polys + 17;
		plane = (const PolyFace*)(poly_list + 12 * lod + 8 * face);
		normal_x = plane->normal_x;
		vlist = (const uint8_t*)plane + plane->vlist_offset;
		normal_y = plane->normal_y;
		normal_z = plane->normal_z;

		for (vertex = (const int16_t*)(poly_list + 6 * vlist[1]); (*vertex & 0xFF00) == 0x7F00;
			 vertex -= 3 * ((*vertex & 0xFF) >> 1))
			;
		edge = *vertex >> norm_shift;
		other_side_dx = other_side - edge;
		cam_side_dx = cam_side - edge;
		for (vertex = (const int16_t*)(poly_list + 6 * vlist[1] + 2); (*vertex & 0xFF00) == 0x7F00;
			 vertex -= 3 * ((*vertex & 0xFF) >> 1))
			;
		edge = *vertex >> norm_shift;
		other_fwd_dx = other_fwd - edge;
		cam_fwd_dx = cam_fwd - edge;
		for (vertex = (const int16_t*)(poly_list + 6 * vlist[1] + 4); (*vertex & 0xFF00) == 0x7F00;
			 vertex -= 3 * ((*vertex & 0xFF) >> 1))
			;
		edge = *vertex >> norm_shift;
		other_up_dx = other_up - edge;
		cam_up_dx = cam_up - edge;

		other_dot =
			math2_dot3_q15_clamped(normal_x, normal_y, normal_z, other_side_dx, other_fwd_dx, other_up_dx);
		dot = math2_dot3_q15_clamped(normal_x, normal_y, normal_z, cam_side_dx, cam_fwd_dx, cam_up_dx);
		if ((int16_t)(dot ^ other_dot) >= 0)
			return front_obj;
	}
	return back_obj;
}
