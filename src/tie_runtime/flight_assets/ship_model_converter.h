#ifndef TIE_COMMON_REMASTER_FLIGHT_SHIPMODEL_CONVERTER_H
#define TIE_COMMON_REMASTER_FLIGHT_SHIPMODEL_CONVERTER_H

/* Converts ShipModelData painter geometry into triangulated render data.
 * Output allocations are released with TieShipModelConverter_Free. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "aeron/scene/mesh_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* GPU vertex layout consumed by the classic mesh shader. */
typedef struct TieFlightVertex {
	float pos[3];           /* craft-local int16 units, NOT pre-scaled */
	float normal[3];        /* Face normal (Q15 unit vector → float [-1, 1]).
							 * Always populated. Used for flat shading
							 * (the dominant path) and as the fallback
							 * when gouraud is disabled or the face is
							 * not Gouraud-eligible. */
	float vertex_normal[3]; /* Corner normal rebuilt by Aeron's
							 * angle-weighted connected-fan algorithm.
							 * Engine-OUTWARD direction.
							 * Drives smooth shading on Gouraud-
							 * eligible faces (flag byte bit 0x40 set)
							 * when the engine's gouraudflag is on. */
	float color;            /* face base color as float (raw palette idx, 0..127) */
	float material_id;      /* materialcolors row (= color & 0x3F as float, 0..44) */
	float mesh_index;       /* 0-based index into TieFlightShipModel.mesh_rot —
							 * drives per-mesh component rotation (turrets,
							 * articulated parts), and also indexes the
							 * MeshTableUniforms tables (visibility,
							 * highlight, markings, emissive) in the VS. */
	float face_flags;       /* Packed face flag-byte bits used by the
							 * Gouraud path. As float because vertex
							 * attributes are float-only in the GPU
							 * binding we use; round to int in the shader.
							 *   bit 0 (0x01): face flag bit 0x40 is set
							 *                (Gouraud-eligible).
							 *   bit 1 (0x02): face flag_byte == 194
							 *                (= 0x80|0x40|0x02 — the
							 *                two-sided unlit-edge
							 *                sentinel; preserves
							 *                negative dot instead of
							 *                clamping; drawpol.c:559). */
	/* Face-local 2D coordinates of this vertex in the parent face's
	 * (u, v) basis (origin = face vertex 0, u along edge 0→1, v
	 * orthogonal in the face plane). All three corners of a fan
	 * triangle write their own projection, so perspective-correct
	 * interpolation gives the fragment shader the fragment's exact
	 * face-local UV — fed into the per-fragment decal point-in-poly
	 * test that replaces the marking VBO. */
	float face_u;
	float face_v;
	/* Window into TieFlightShipModel.decals[] for this face. `decal_count == 0`
	 * → no markings on this face, the fragment shader skips the loop
	 * at zero cost. Floats because the vertex format is float-only in
	 * the GPU binding; round to int in the shader. */
	float decal_offset;
	float decal_count;
} TieFlightVertex;

/* Per-face decal in face-local coordinates. Two vertices identify a line;
 * its thickness base occupies the material high byte. The bounding box
 * rejects fragments before the point-in-polygon test. Layout is 32-byte,
 * std430-compatible. */
typedef struct TieFlightDecal {
	uint32_t vert_offset;
	uint32_t vert_count;
	uint32_t color;
	uint32_t material;
	float bbox_min_u;
	float bbox_min_v;
	float bbox_max_u;
	float bbox_max_v;
} TieFlightDecal;

/* A single decal-polygon vertex in the parent face's (u, v) basis. */
typedef struct TieFlightDecalVertex {
	float u;
	float v;
} TieFlightDecalVertex;

/* Four vertices expand a line into a screen-facing quad. Each carries both
 * consistently ordered endpoints plus a corner selector. Lit Gouraud lines
 * use endpoint normals; other lines carry the face normal. */
typedef struct TieFlightLineVertex {
	float pos_a[3];       /* line endpoint A, craft-local int16 units */
	float pos_b[3];       /* line endpoint B, craft-local int16 units */
	float normal[3];      /* see comment above for which normal */
	float endpoint;       /* 0.0 → this vertex sits at A, 1.0 → at B */
	float side;           /* -1.0 or +1.0 (quad side selector) */
	float color;          /* same encoding as TieFlightVertex.color */
	float material_id;    /* same encoding as TieFlightVertex.material_id */
	float thickness_base; /* engine TieShipModelConverterPolyFaceHeader line-body u16; passed
						   * straight through for perspective-scaled
						   * thickness in the shader. */
	float mesh_index;     /* same role as TieFlightVertex.mesh_index —
						   * antennas attached to a rotating turret
						   * rotate along with it. */
} TieFlightLineVertex;    /* 60 bytes */

/* Decals use face-local coordinates and are composited by the parent face's
 * fragment shader. This confines them to the face without depth bias. Line
 * markings are expanded to face-local rectangles during conversion. */

/* Line-geometry LOD segment — one per ShipMeshLOD entry in the source
 * blob. For lasers, the blob's LOD table maps eye-space distances to
 * progressively coarser meshes (e.g. 5 bands → 3 → 1 for retail bolts).
 * `distance_view` is the native engine threshold (`ShipMeshLOD.distance`),
 * with `INFINITY` for the last
 * (terminator) entry. The renderer picks the first segment whose
 * threshold is ≥ the bolt's eye-z, mirroring `draw_getdetailptr`. */
typedef struct TieFlightShipModelLineLod {
	uint32_t index_offset; /* first index in line_indices */
	uint32_t index_count;  /* number of indices in this LOD */
	float distance_view;
} TieFlightShipModelLineLod;

typedef struct TieFlightShipModel {
	TieFlightVertex* vertices;
	uint16_t* indices;
	uint32_t vertex_count;
	uint32_t index_count;
	/* Per-mesh rotation table — one entry per source ShipModelMesh.
	 * Renderer uses (mesh_count, mesh_rot) together with the
	 * per-craft mesh_rotation angle to build the per-(craft, mesh)
	 * affine transform applied before craft_to_world. */
	AeronMeshRot* mesh_rot;
	uint32_t mesh_count;
	/* Line-quad geometry for the engine's vcount==2 line faces
	 * (drawpol_drawlineface — antennas, struts, wire details).
	 * The shader expands each 4-vertex/6-index unit into a
	 * screen-facing thin quad whose pixel width tracks the engine's
	 * (thicknessMultiple × base) / (eye_z >> 8). Renderer owns the
	 * separate vertex format because line vertices need attributes
	 * (other_pos, side, thickness_base) the triangle vertex format
	 * doesn't carry. */
	TieFlightLineVertex* line_vertices;
	uint16_t* line_indices;
	uint32_t line_vertex_count;
	uint32_t line_index_count;
	/* Line-geometry LOD segments. NULL/0 means "no LOD chain": draw
	 * the entire line_indices range unconditionally (antennas / hull
	 * line accents follow this path). Laser bolts populate this table
	 * with one entry per source ShipMeshLOD, sharing one VBO across
	 * all LODs; the renderer indexes into line_indices via each
	 * segment's `index_offset` + `index_count`. */
	TieFlightShipModelLineLod* line_lods;
	uint32_t line_lod_count;
	/* Decal records + their (u, v) vertex pool — uploaded as
	 * graphics-stage storage buffers and sampled per-fragment by the
	 * mesh fragment shader. See "Markings (decals)" comment above. */
	TieFlightDecal* decals;
	TieFlightDecalVertex* decal_verts;
	uint32_t decal_count;
	uint32_t decal_vert_count;
	/* AABB in craft-local space. Useful for view-frustum culling and
	 * lighting radius selection. */
	float bound_min[3];
	float bound_max[3];
	/* ShipModelData.model_scale_shift (file byte +0x1E). At exactly 2,
	 * classic routes vertex rasterisation through
	 * transfm2_geteyecoordsS2 via DRAW_drawcraft's HIBYTE bump on
	 * parentobject — a 4× eye-space contribution. Consumer applies the
	 * multiplier on craft_to_world. */
	uint8_t model_scale_shift;
} TieFlightShipModel;

/* Convert a ShipModelData blob. `blob` points at the
 * 2-byte file prefix + ShipModelData header (i.e. the raw bytes
 * returned by an independently read LFD entry). `blob_size` bounds every
 * offset validation.
 *
 * Returns true on success (*out* populated, ownership transferred to
 * caller). Returns false on any parse error; *out* zeroed. */
bool TieShipModelConverter_Build(const void* blob, size_t blob_size, float smooth_angle_degrees,
								 TieFlightShipModel* out);

/* Convert a laser-bolt poly blob. Lasers (weapon
 * species 137..154) ship through draw.c's laser_species_poly[] table
 * instead of fediskio — their blobs start directly with a ShipMeshLOD[]
 * table at offset 0 (no ShipModelData header), and the poly the LOD
 * points to is a top-level line object (header byte 0x40 or 0x41,
 * the same format DRAWPOL_drawpolyobject's line-object path consumes).
 *
 * Highest-detail LOD (the first table entry) gets parsed; output is
 * line-quad geometry only (line_vertices/line_indices populated;
 * vertices/indices empty so the triangle pipeline skips this species).
 *
 * Returns true on success. Returns false on any parse error (out
 * zeroed). */
bool TieShipModelConverter_BuildLaser(const void* blob, size_t blob_size, TieFlightShipModel* out);

void TieShipModelConverter_Free(TieFlightShipModel* m);

#ifdef __cplusplus
}
#endif

#endif
