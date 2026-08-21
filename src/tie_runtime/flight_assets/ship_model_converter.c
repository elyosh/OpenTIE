/* Vertex values with a 0x7F high byte refer to an earlier slot selected by
 * half the low byte. Conversion resolves these references before upload. */

#include "tie_runtime/flight_assets/ship_model_converter.h"

#include "aeron/mesh_normals.h"

#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

/* Local packed definitions for the ShipModelData wire layout. */

#pragma pack(push, 2)
typedef struct TieShipModelConverterPolyVertexI16 {
	int16_t x, y, z;
} TieShipModelConverterPolyVertexI16;

typedef struct TieShipModelConverterPolyFaceHeader {
	int16_t normal_x, normal_y, normal_z;
	/* SIGNED self-relative byte offset to the face's vertex/edge body
	 * (draw.h:42). Body may sit before OR after the face header. */
	int16_t vlist_offset;
} TieShipModelConverterPolyFaceHeader;

typedef struct TieShipModelConverterLodRecord {
	uint16_t bsp_offset;
	uint32_t z_max;
} TieShipModelConverterLodRecord;

typedef struct TieShipModelConverterMesh {
	uint16_t mesh_type;
	int16_t flags;
	uint8_t pad_04[6];
	uint16_t explosion_scale;
	int32_t draw_distance;
	int16_t center_side, center_fwd, center_up;
	int16_t pad_16;
	int16_t bbox_min_side, bbox_min_fwd, bbox_min_up;
	int16_t bbox_max_side, bbox_max_fwd, bbox_max_up;
	uint8_t pad_24[7];
	uint8_t num_hardpoints;
	uint16_t render_offset;
	uint16_t hardpoint_offset;
	uint8_t pad_30[2];
	uint16_t rotation_offset;
	int16_t has_position;
	int16_t pos_side, pos_fwd, pos_up;
	uint8_t pad_3C[4];
} TieShipModelConverterMesh;

typedef struct TieShipModelConverterMeshLod {
	int32_t distance;
	uint16_t offset;
} TieShipModelConverterMeshLod;

/* On-disk component rotation block referenced from a mesh's
 * rotation_offset (see fview.h::ComponentRotData). 12 bytes int16.
 * The engine's basis-index order is (1, 2, 3) = (side, fwd, up); the
 * struct's field names mirror the int_engine convention literally. */
typedef struct TieShipModelConverterRotationData {
	int16_t pivot_value; /* basis 1 (side) */
	int16_t pivot_x;     /* basis 2 (fwd) */
	int16_t pivot_z;     /* basis 3 (up) */
	int16_t axis_y;      /* axis component 0 (passed to build_rodrigues first arg) */
	int16_t axis_x;      /* axis component 1 (second arg) */
	int16_t axis_z;      /* axis component 2 (third arg) */
} TieShipModelConverterRotationData;

typedef struct TieShipModelConverterData {
	uint16_t prefix;
	uint16_t _pad_02;
	uint16_t width, height, depth, length;
	int32_t render_distance;
	uint16_t _pad_10;
	int32_t shield_default;
	uint16_t _pad_16;
	int32_t speed_default;
	uint8_t num_meshes;
	uint8_t num_lods;
	uint8_t model_scale_shift;
	uint8_t _pad_1F;
	TieShipModelConverterLodRecord lod_records[]; /* num_lods entries at +0x20 */
} TieShipModelConverterData;
#pragma pack(pop)

_Static_assert(sizeof(TieShipModelConverterMesh) == 64, "ShipModelMesh layout drift");
_Static_assert(sizeof(TieShipModelConverterLodRecord) == 6, "LODRecord layout drift");
_Static_assert(sizeof(TieShipModelConverterMeshLod) == 6, "ShipMeshLOD layout drift");
_Static_assert(sizeof(TieShipModelConverterPolyFaceHeader) == 8, "PolyFace layout drift");
_Static_assert(sizeof(TieShipModelConverterPolyVertexI16) == 6, "PolyVert layout drift");
_Static_assert(sizeof(TieShipModelConverterRotationData) == 12, "ComponentRotData layout drift");

/* ---------------------------------------------------------------------------
 * Dynamic output buffers.
 * --------------------------------------------------------------------------- */

typedef struct TieShipModelConverterVertexBuffer {
	TieFlightVertex* data;
	uint32_t count;
	uint32_t cap;
} TieShipModelConverterVertexBuffer;

typedef struct TieShipModelConverterLineVertexBuffer {
	TieFlightLineVertex* data;
	uint32_t count;
	uint32_t cap;
} TieShipModelConverterLineVertexBuffer;

typedef struct TieShipModelConverterDecalVertexBuffer {
	TieFlightDecalVertex* data;
	uint32_t count;
	uint32_t cap;
} TieShipModelConverterDecalVertexBuffer;

typedef struct TieShipModelConverterDecalBuffer {
	TieFlightDecal* data;
	uint32_t count;
	uint32_t cap;
} TieShipModelConverterDecalBuffer;

typedef struct TieShipModelConverterIndexBuffer {
	uint16_t* data;
	uint32_t count;
	uint32_t cap;
} TieShipModelConverterIndexBuffer;

static bool TieShipModelConverter_VertbufPush(TieShipModelConverterVertexBuffer* vb,
											  const TieFlightVertex* v) {
	if (vb->count >= vb->cap) {
		uint32_t newcap = vb->cap ? vb->cap * 2 : 256;
		TieFlightVertex* nd = (TieFlightVertex*)realloc(vb->data, newcap * sizeof *nd);
		if (!nd)
			return false;
		vb->data = nd;
		vb->cap = newcap;
	}
	vb->data[vb->count++] = *v;
	return true;
}

static bool TieShipModelConverter_IdxbufPush(TieShipModelConverterIndexBuffer* ib, uint16_t idx) {
	if (ib->count >= ib->cap) {
		uint32_t newcap = ib->cap ? ib->cap * 2 : 512;
		uint16_t* nd = (uint16_t*)realloc(ib->data, newcap * sizeof *nd);
		if (!nd)
			return false;
		ib->data = nd;
		ib->cap = newcap;
	}
	ib->data[ib->count++] = idx;
	return true;
}

static bool TieShipModelConverter_LinevertbufPush(TieShipModelConverterLineVertexBuffer* vb,
												  const TieFlightLineVertex* v) {
	if (vb->count >= vb->cap) {
		uint32_t newcap = vb->cap ? vb->cap * 2 : 64;
		TieFlightLineVertex* nd = (TieFlightLineVertex*)realloc(vb->data, newcap * sizeof *nd);
		if (!nd)
			return false;
		vb->data = nd;
		vb->cap = newcap;
	}
	vb->data[vb->count++] = *v;
	return true;
}

static bool TieShipModelConverter_DecalvertbufPush(TieShipModelConverterDecalVertexBuffer* vb,
												   TieFlightDecalVertex v) {
	if (vb->count >= vb->cap) {
		uint32_t newcap = vb->cap ? vb->cap * 2 : 64;
		TieFlightDecalVertex* nd = (TieFlightDecalVertex*)realloc(vb->data, newcap * sizeof *nd);
		if (!nd)
			return false;
		vb->data = nd;
		vb->cap = newcap;
	}
	vb->data[vb->count++] = v;
	return true;
}

static bool TieShipModelConverter_DecalrecbufPush(TieShipModelConverterDecalBuffer* rb, TieFlightDecal r) {
	if (rb->count >= rb->cap) {
		uint32_t newcap = rb->cap ? rb->cap * 2 : 32;
		TieFlightDecal* nd = (TieFlightDecal*)realloc(rb->data, newcap * sizeof *nd);
		if (!nd)
			return false;
		rb->data = nd;
		rb->cap = newcap;
	}
	rb->data[rb->count++] = r;
	return true;
}

/* === Per-face (u, v) basis ========================================
 * Engine markings live in face-local coordinates: each decal vertex
 * is a barycentric blend over three of the parent face's vertices.
 * The fragment-shader overlay path needs the same coordinates the
 * decals were authored in. We build an orthonormal (u, v) basis on
 * the face plane (origin at face vertex 0, u along edge 0→1,
 * v = n × u — orthonormal because n and u are unit and orthogonal)
 * and project both the face vertices and the decal vertices into
 * that basis. Interpolated face_u/face_v in the fragment shader is
 * the fragment's true face-local position because the plane is
 * affine and TieFlightVertex.pos uses the same (u_axis, v_axis) frame
 * by construction.
 *
 * If the projected face winds clockwise in (u, v), flip v_axis so
 * the (u, v) frame matches the engine's face polygon winding — this
 * matters for line-marking rectangle construction (we want a
 * consistent left-of-line normal). */

/* Face vertex count occupies six bits; scratch arrays cover its full range. */
#define TIE_FLIGHT_FACE_MAX_VERTS 63
/* Marking decal cap stays at 16: the engine encodes raw_vcount > 16
 * as a thick-line marker (decal_np forced to 2 + thickness derived
 * from raw_vcount - 16, drawpol.c:679-685), so polygon decals are
 * genuinely bounded at 16 by the wire format. */
#define TIE_FLIGHT_DECAL_MAX_VERTS 16

typedef struct TieShipModelConverterFaceBasis {
	float orig[3];
	float u_axis[3];
	float v_axis[3];
	bool ok;
	/* True when TieShipModelConverter_FaceUvBasis had to flip v_axis to make the polygon
	 * wind CCW in (u, v). After the flip `u_axis × v_axis = -fn`,
	 * which means a CCW triangle in (u, v) has geometric normal
	 * `-fn` — opposite the authored face normal. The triangle emit
	 * loop reverses winding for these faces so all emitted triangles
	 * end up with `geometric_normal · fn > 0`, making the rasterizer's
	 * SV_IsFrontFace classification consistent with the authored
	 * normal direction. Without this, ~half the faces emit with
	 * inverted winding and downstream view-dependent shading paths
	 * (PBR, normal mapping, anything using `dot(N, V)`)
	 * see normals that point away from the camera on visible
	 * fragments. */
	bool v_flipped;
} TieShipModelConverterFaceBasis;

/* Build the (u, v) basis for a face whose vertex slot list lives at
 * `face_body_off + 1` (each slot = 2 bytes: vidx, edge). Returns
 * basis.ok = false if the face is degenerate (<3 valid vertices or
 * edge 0→1 has zero length). */
static TieShipModelConverterFaceBasis TieShipModelConverter_FaceUvBasis(const uint8_t* poly_data,
																		size_t poly_max, size_t face_body_off,
																		int face_vc, const float fn[3],
																		const int16_t* pos_resolved,
																		int numpoints) {
	TieShipModelConverterFaceBasis fb = { 0 };
	if (face_vc < 3 || face_vc > TIE_FLIGHT_FACE_MAX_VERTS)
		return fb;
	if (face_body_off + 1 + (size_t)(2 * face_vc) > poly_max)
		return fb;

	float face2d_u[TIE_FLIGHT_FACE_MAX_VERTS];
	float face2d_v[TIE_FLIGHT_FACE_MAX_VERTS];

	/* Vertex 0 = basis origin. */
	const uint8_t v0i = poly_data[face_body_off + 1];
	if (v0i >= numpoints)
		return fb;
	fb.orig[0] = (float)pos_resolved[3 * v0i + 0];
	fb.orig[1] = (float)pos_resolved[3 * v0i + 1];
	fb.orig[2] = (float)pos_resolved[3 * v0i + 2];

	/* u_axis = normalize(face_v[1] - face_v[0]). */
	const uint8_t v1i = poly_data[face_body_off + 1 + 2];
	if (v1i >= numpoints)
		return fb;
	fb.u_axis[0] = (float)pos_resolved[3 * v1i + 0] - fb.orig[0];
	fb.u_axis[1] = (float)pos_resolved[3 * v1i + 1] - fb.orig[1];
	fb.u_axis[2] = (float)pos_resolved[3 * v1i + 2] - fb.orig[2];
	const float ulen2 =
		fb.u_axis[0] * fb.u_axis[0] + fb.u_axis[1] * fb.u_axis[1] + fb.u_axis[2] * fb.u_axis[2];
	if (ulen2 < 1e-6f)
		return fb; /* degenerate edge 0→1 */
	const float inv_ulen = 1.0f / (float)__builtin_sqrtf(ulen2);
	fb.u_axis[0] *= inv_ulen;
	fb.u_axis[1] *= inv_ulen;
	fb.u_axis[2] *= inv_ulen;

	/* v_axis = fn × u_axis. Both unit, orthogonal ⇒ v_axis unit. */
	fb.v_axis[0] = fn[1] * fb.u_axis[2] - fn[2] * fb.u_axis[1];
	fb.v_axis[1] = fn[2] * fb.u_axis[0] - fn[0] * fb.u_axis[2];
	fb.v_axis[2] = fn[0] * fb.u_axis[1] - fn[1] * fb.u_axis[0];

	/* Flip v_axis if the face winds CW in (u, v) — keeps the basis
	 * consistent with the engine's face winding for downstream
	 * geometry (the line-marking rectangle's perpendicular direction
	 * relies on a CCW frame). */
	face2d_u[0] = 0.0f;
	face2d_v[0] = 0.0f;
	for (int s = 1; s < face_vc; ++s) {
		const uint8_t vidx = poly_data[face_body_off + 1 + 2 * s];
		if (vidx >= numpoints)
			return fb;
		const float dx = (float)pos_resolved[3 * vidx + 0] - fb.orig[0];
		const float dy = (float)pos_resolved[3 * vidx + 1] - fb.orig[1];
		const float dz = (float)pos_resolved[3 * vidx + 2] - fb.orig[2];
		face2d_u[s] = dx * fb.u_axis[0] + dy * fb.u_axis[1] + dz * fb.u_axis[2];
		face2d_v[s] = dx * fb.v_axis[0] + dy * fb.v_axis[1] + dz * fb.v_axis[2];
	}
	float signed_area2 = 0.0f;
	for (int s = 0; s < face_vc; ++s) {
		const int s1 = (s + 1) % face_vc;
		signed_area2 += face2d_u[s] * face2d_v[s1] - face2d_u[s1] * face2d_v[s];
	}
	if (signed_area2 < 0.0f) {
		fb.v_axis[0] = -fb.v_axis[0];
		fb.v_axis[1] = -fb.v_axis[1];
		fb.v_axis[2] = -fb.v_axis[2];
		fb.v_flipped = true;
	}
	fb.ok = true;
	return fb;
}

/* Project a 3D craft-local point into the face's (u, v) basis. */
static TieFlightDecalVertex TieShipModelConverter_ProjectUv(const TieShipModelConverterFaceBasis* fb,
															const float p[3]) {
	const float dx = p[0] - fb->orig[0];
	const float dy = p[1] - fb->orig[1];
	const float dz = p[2] - fb->orig[2];
	TieFlightDecalVertex out;
	out.u = dx * fb->u_axis[0] + dy * fb->u_axis[1] + dz * fb->u_axis[2];
	out.v = dx * fb->v_axis[0] + dy * fb->v_axis[1] + dz * fb->v_axis[2];
	return out;
}

/* ---------------------------------------------------------------------------
 * Vertex-stream dedup. The engine's drawpol path mutates the input
 * stream's 0x7F00 markers once on first draw; we don't want to mutate
 * a shared species blob, so resolve into a private copy.
 *
 * Marker layout (per drawpol.c:1227-1243): if any of the 3 int16
 * components of a vertex has high byte 0x7F, the low byte / 2 is the
 * number of vertex slots to look back. The back-reference reads the
 * matching component from that earlier vertex.
 * --------------------------------------------------------------------------- */
static void TieShipModelConverter_DedupVertexStream(const TieShipModelConverterPolyVertexI16* src,
													int numpoints, int16_t* out_xyz) {
	for (int v = 0; v < numpoints; ++v) {
		for (int k = 0; k < 3; ++k) {
			uint16_t w = (uint16_t)(((const int16_t*)src)[3 * v + k]);
			if ((w & 0xFF00u) == 0x7F00u) {
				int back = (int)(w & 0xFFu) >> 1;
				int ref = v - back;
				if (ref < 0)
					ref = 0;
				out_xyz[3 * v + k] = out_xyz[3 * ref + k];
			} else {
				out_xyz[3 * v + k] = (int16_t)w;
			}
		}
	}
}

/* ---------------------------------------------------------------------------
 * Marking section locator + body walker.
 *
 * The marking section sits immediately after all face bodies (each
 * `1 + 2*vcount + 3` bytes long), optionally after the BSP tree
 * (3 bytes per face when polyobject type has bit 1 set). Layout:
 *   +0   i16   mcount      (number of marking attachments)
 *   +2   per-attachment record (3 bytes × mcount):
 *          +0  u8    face_idx
 *          +1  i16   link  (signed byte offset to the marking body)
 * --------------------------------------------------------------------------- */

#define TIE_FLIGHT_MARKING_MAX 127 /* engine bound is 0x80 */

typedef struct TieShipModelConverterMarkingRef {
	uint8_t face_idx;
	size_t body_off;
} TieShipModelConverterMarkingRef;

/* Walk faces past `fixed_end`, jump over the optional BSP table, then
 * read the marking record list. Returns the count of valid records
 * collected into `out_refs` (capped at TIE_FLIGHT_MARKING_MAX). 0 on
 * absent / malformed marking section — caller treats that as "no
 * decals for this mesh". */
static int TieShipModelConverter_FindMarkings(const uint8_t* poly_data, size_t poly_max, size_t fixed_end,
											  int numfaces, uint8_t type,
											  TieShipModelConverterMarkingRef* out_refs) {
	size_t fb_off = fixed_end;
	for (int k = 0; k < numfaces; ++k) {
		if (fb_off >= poly_max)
			return 0;
		uint8_t flag = poly_data[fb_off];
		int vc = flag & 0x3F;
		fb_off += (size_t)(2 * vc + 4);
	}
	if (type & 0x02)
		fb_off += (size_t)3 * numfaces;
	if (fb_off + 2 > poly_max)
		return 0;
	const int16_t mcount = (int16_t)(poly_data[fb_off] | (poly_data[fb_off + 1] << 8));
	if (mcount <= 0)
		return 0;
	const int capped = (mcount > TIE_FLIGHT_MARKING_MAX) ? TIE_FLIGHT_MARKING_MAX : (int)mcount;
	const size_t records_off = fb_off + 2;
	int n = 0;
	for (int mi = 0; mi < capped; ++mi) {
		const size_t rec_off = records_off + (size_t)(3 * mi);
		if (rec_off + 3 > poly_max)
			break;
		const uint8_t face_idx = poly_data[rec_off + 0];
		const int16_t link = (int16_t)(poly_data[rec_off + 1] | (poly_data[rec_off + 2] << 8));
		const int64_t body_signed = (int64_t)rec_off + (int64_t)link;
		if (body_signed < 0 || (uint64_t)body_signed >= poly_max)
			continue;
		out_refs[n].face_idx = face_idx;
		out_refs[n].body_off = (size_t)body_signed;
		++n;
	}
	return n;
}

/* Resolve one decal vertex via the engine's barycentric blend within
 * the parent face. Each decal vertex is encoded as 3 bytes:
 *   base_idx u8  — offset (in bytes) into the face's vertex slot list
 *                  starting at face_body_off + 1.
 *   w_edge   u8  — weight of the previous slot (base_idx - 2).
 *   w_diag   u8  — weight of the next slot     (base_idx + 2).
 * Engine reads the neighbour slots only when their weight is non-zero;
 * mirror that exactly so an edge-weight=0 record never touches
 * base_idx - 2 (which may be out-of-bounds for legitimate marking
 * data — see drawpol.c:702-722).
 *
 * Returns true on success; out_p is written in craft-local int16 units. */
static bool TieShipModelConverter_ResolveDecalVertPos(const uint8_t* poly_data, size_t poly_max,
													  size_t face_body_off, int numpoints,
													  const int16_t* pos_resolved, uint8_t base, uint8_t we,
													  uint8_t wd, float out_p[3]) {
	const ptrdiff_t base_origin = (ptrdiff_t)face_body_off + 1;

	const ptrdiff_t r0s = base_origin + (ptrdiff_t)(int)base;
	if (r0s < 0 || (size_t)r0s >= poly_max)
		return false;
	const uint8_t raw0 = poly_data[(size_t)r0s];
	if (raw0 >= numpoints)
		return false;
	float v0x = (float)pos_resolved[3 * raw0 + 0];
	float v0y = (float)pos_resolved[3 * raw0 + 1];
	float v0z = (float)pos_resolved[3 * raw0 + 2];

	float ex = 0.0f, ey = 0.0f, ez = 0.0f;
	if (we != 0) {
		const ptrdiff_t res = base_origin + (ptrdiff_t)((int)base - 2);
		if (res < 0 || (size_t)res >= poly_max)
			return false;
		const uint8_t rawe = poly_data[(size_t)res];
		if (rawe >= numpoints)
			return false;
		ex = (float)pos_resolved[3 * rawe + 0] - v0x;
		ey = (float)pos_resolved[3 * rawe + 1] - v0y;
		ez = (float)pos_resolved[3 * rawe + 2] - v0z;
	}
	float dx_ = 0.0f, dy_ = 0.0f, dz_ = 0.0f;
	if (wd != 0) {
		const ptrdiff_t rds = base_origin + (ptrdiff_t)((int)base + 2);
		if (rds < 0 || (size_t)rds >= poly_max)
			return false;
		const uint8_t rawd = poly_data[(size_t)rds];
		if (rawd >= numpoints)
			return false;
		dx_ = (float)pos_resolved[3 * rawd + 0] - v0x;
		dy_ = (float)pos_resolved[3 * rawd + 1] - v0y;
		dz_ = (float)pos_resolved[3 * rawd + 2] - v0z;
	}
	const float fe = (float)we * (1.0f / 32.0f);
	const float fd = (float)wd * (1.0f / 32.0f);
	out_p[0] = v0x + ex * fe + dx_ * fd;
	out_p[1] = v0y + ey * fe + dy_ * fd;
	out_p[2] = v0z + ez * fe + dz_ * fd;
	return true;
}

/* Walk one marking body and emit its decals into (dvb, drb).
 *
 * Decals are emitted IN ORDER — the fragment shader's first-match-wins
 * loop then reproduces the engine's last-write-wins swap-chain
 * (xtrans2_processedge) for overlapping decals in the common case
 * where authors stack non-overlapping decals per face.
 *
 * Soft failures (malformed sub-record, allocation failure) early-exit
 * this body but leave the proc-mesh build viable: a partial marking
 * payload is better than a missing mesh. */
static void TieShipModelConverter_EmitFaceMarkingsBody(const uint8_t* poly_data, size_t poly_max,
													   size_t body_off, size_t face_body_off, int numpoints,
													   const int16_t* pos_resolved,
													   const TieShipModelConverterFaceBasis* fb,
													   TieShipModelConverterDecalVertexBuffer* dvb,
													   TieShipModelConverterDecalBuffer* drb) {
	if (body_off + 1 > poly_max)
		return;
	const uint8_t marktot = poly_data[body_off];
	if (marktot == 0)
		return;
	const size_t colors_off = body_off + 1;
	if (colors_off + marktot > poly_max)
		return;
	const size_t decals_off = colors_off + marktot;

	size_t walker = decals_off;
	int remaining = marktot;
	int decal_i = 0;

	while (remaining > 0 && walker < poly_max) {
		const uint8_t raw_vcount = poly_data[walker];
		if (raw_vcount == 0xFF) {
			/* Depth-cull marker — 5-byte i32 z_cut threshold; HD
			 * doesn't honour z-cull so skip and keep walking. Does
			 * NOT consume a colour slot. */
			walker += 5;
			continue;
		}
		bool is_line;
		int decal_np;
		if (raw_vcount > 16) {
			decal_np = 2;
			is_line = true;
		} else if (raw_vcount == 2) {
			decal_np = 2;
			is_line = true;
		} else {
			decal_np = raw_vcount;
			is_line = false;
		}
		if (decal_np < 2 || decal_np > TIE_FLIGHT_DECAL_MAX_VERTS)
			return;
		const size_t verts_off = walker + 1;
		if (verts_off + (size_t)(3 * decal_np) > poly_max)
			return;

		const uint8_t color = poly_data[colors_off + decal_i];
		const uint8_t material = (uint8_t)(color & 0x3Fu);

		/* Resolve decal vertex positions in 3D, then project into the
		 * face's (u, v) basis. */
		TieFlightDecalVertex dec_uv[TIE_FLIGHT_DECAL_MAX_VERTS];
		bool decal_ok = true;
		for (int p = 0; p < decal_np; ++p) {
			const uint8_t base = poly_data[verts_off + 3 * p + 0];
			const uint8_t we = poly_data[verts_off + 3 * p + 1];
			const uint8_t wd = poly_data[verts_off + 3 * p + 2];
			float p3[3];
			if (!TieShipModelConverter_ResolveDecalVertPos(poly_data, poly_max, face_body_off, numpoints,
														   pos_resolved, base, we, wd, p3)) {
				decal_ok = false;
				break;
			}
			dec_uv[p] = TieShipModelConverter_ProjectUv(fb, p3);
		}

		if (decal_ok) {
			/* Compute face-local UV bbox over the decal's vertices.
			 * For polygons the bbox cull is meaningful; for lines we
			 * still emit it (the FS skips the bbox test on lines —
			 * line hit-test is already cheap). */
			float bbox_min_u = 3.4e38f, bbox_min_v = 3.4e38f;
			float bbox_max_u = -3.4e38f, bbox_max_v = -3.4e38f;
			for (int p = 0; p < decal_np; ++p) {
				if (dec_uv[p].u < bbox_min_u)
					bbox_min_u = dec_uv[p].u;
				if (dec_uv[p].v < bbox_min_v)
					bbox_min_v = dec_uv[p].v;
				if (dec_uv[p].u > bbox_max_u)
					bbox_max_u = dec_uv[p].u;
				if (dec_uv[p].v > bbox_max_v)
					bbox_max_v = dec_uv[p].v;
			}
			if (is_line) {
				/* A two-vertex decal is a line marking. Pack its engine thickness
				 * base in the material high byte; the fragment shader applies the
				 * depth-scaled pixel width in face-local coordinates. */
				const uint32_t thick_base = (raw_vcount > 16) ? (uint32_t)(raw_vcount - 16) : 1u;
				const float du = dec_uv[1].u - dec_uv[0].u;
				const float dv = dec_uv[1].v - dec_uv[0].v;
				const float len2 = du * du + dv * dv;
				if (len2 >= 1e-12f) {
					const uint32_t voff = dvb->count;
					if (!TieShipModelConverter_DecalvertbufPush(dvb, dec_uv[0]) ||
						!TieShipModelConverter_DecalvertbufPush(dvb, dec_uv[1]))
						return;
					TieFlightDecal rec = {
						.vert_offset = voff,
						.vert_count = 2u,
						.color = color,
						.material = (uint32_t)material | ((thick_base & 0xFFu) << 8),
						.bbox_min_u = bbox_min_u,
						.bbox_min_v = bbox_min_v,
						.bbox_max_u = bbox_max_u,
						.bbox_max_v = bbox_max_v,
					};
					if (!TieShipModelConverter_DecalrecbufPush(drb, rec))
						return;
				}
			} else if (decal_np >= 3) {
				const uint32_t voff = dvb->count;
				bool pushed = true;
				for (int p = 0; p < decal_np && pushed; ++p)
					pushed = TieShipModelConverter_DecalvertbufPush(dvb, dec_uv[p]);
				if (!pushed)
					return;
				TieFlightDecal rec = {
					.vert_offset = voff,
					.vert_count = (uint32_t)decal_np,
					.color = color,
					.material = material,
					.bbox_min_u = bbox_min_u,
					.bbox_min_v = bbox_min_v,
					.bbox_max_u = bbox_max_u,
					.bbox_max_v = bbox_max_v,
				};
				if (!TieShipModelConverter_DecalrecbufPush(drb, rec))
					return;
			}
		}

		walker = verts_off + (size_t)(3 * decal_np);
		--remaining;
		++decal_i;
	}
}

/* ---------------------------------------------------------------------------
 * Per-mesh poly walk.
 * --------------------------------------------------------------------------- */

static bool TieShipModelConverter_ExtractMeshPolys(
	const uint8_t* poly_data, size_t poly_max, int16_t mesh_center_side, int16_t mesh_center_fwd,
	int16_t mesh_center_up, uint32_t mesh_index, TieShipModelConverterVertexBuffer* vb,
	TieShipModelConverterIndexBuffer* ib, TieShipModelConverterLineVertexBuffer* lvb,
	TieShipModelConverterIndexBuffer* lib, TieShipModelConverterDecalVertexBuffer* dvb,
	TieShipModelConverterDecalBuffer* drb) {
	if (poly_max < 5)
		return false;
	uint8_t type = poly_data[0];
	/* dedup_flag at +1 — we don't mutate the source blob; the dedup
	 * helper handles markers on its private copy. */
	uint8_t numpoints = poly_data[2];
	/* numedges at +3 — used by line-object path; skipped for 3D mesh. */
	uint8_t numfaces = poly_data[4];

	/* A renderable ship mesh must point at a non-empty 3D polyobject. */
	if (type < 0x80 || type > 0x83)
		return false;
	if (numpoints == 0 || numfaces == 0)
		return false;

	/* Verify the stream is long enough for the fixed portion before
	 * indexing into it. Face bodies sit past the fixed portion at
	 * variable offsets resolved via vlist_offset; we range-check each
	 * one individually below. */
	size_t fixed_end = (size_t)5 + (size_t)numfaces + (size_t)12 /* bbox skipped */
					   + (size_t)6 * numpoints                   /* positions */
					   + (size_t)6 * numpoints                   /* normals */
					   + (size_t)8 * numfaces;                   /* face headers */
	if (fixed_end > poly_max)
		return false;

	const uint8_t* face_colors = poly_data + 5;
	const TieShipModelConverterPolyVertexI16* pts_raw =
		(const TieShipModelConverterPolyVertexI16*)(poly_data + 5 + numfaces + 12);
	const TieShipModelConverterPolyVertexI16* normals = pts_raw + numpoints;
	const TieShipModelConverterPolyFaceHeader* faces =
		(const TieShipModelConverterPolyFaceHeader*)((const uint8_t*)pts_raw + 12 * (size_t)numpoints);

	/* Resolve 0x7F00 position back-references into private storage. Normal
	 * components may legitimately occupy 0x7F00..0x7FFF and must be copied
	 * verbatim rather than interpreted as back-references. */
	/* numpoints is uint8_t so capacity = 256 is the safe upper bound. */
	int16_t pos_resolved[3 * 256];
	int16_t norm_resolved[3 * 256];
	TieShipModelConverter_DedupVertexStream(pts_raw, numpoints, pos_resolved);
	memcpy(norm_resolved, normals, (size_t)numpoints * 6);

	/* Per-vertex normals stay at the engine-stored values here. The
	 * ship-global normal recompute runs after every component is emitted
	 * after all components have emitted their TieFlightVertex stream — it
	 * operates on the union of all components' geometry so cross-
	 * component boundaries get coherent normals that no per-component
	 * pass can produce. */

	/* Pre-walk the marking section once per mesh so we can interleave
	 * decal extraction with face fan emission below (each face's
	 * decals must be CONTIGUOUS in drb so we can window them with a
	 * single decal_offset/decal_count pair). */
	TieShipModelConverterMarkingRef marking_refs[TIE_FLIGHT_MARKING_MAX];
	const int marking_count =
		TieShipModelConverter_FindMarkings(poly_data, poly_max, fixed_end, numfaces, type, marking_refs);

	/* Vertices are already in ship-centered craft-local coordinates. */
	(void)mesh_center_side;
	(void)mesh_center_fwd;
	(void)mesh_center_up;

	/* Per-face emission. For each face:
	 *  - Line face (vcount == 2): emit TieFlightLineVertex band (existing
	 *    path). No decals possible on line faces — engine skips them.
	 *  - Polygon face: build (u, v) basis, emit any markings targeting
	 *    this face into (dvb, drb), then fan-emit triangles carrying
	 *    face_u/face_v and the (decal_offset, decal_count) window.
	 */
	for (int f = 0; f < numfaces; ++f) {
		const TieShipModelConverterPolyFaceHeader* fh = &faces[f];
		ptrdiff_t fh_off = (const uint8_t*)fh - poly_data;
		ptrdiff_t body_signed = fh_off + (ptrdiff_t)fh->vlist_offset;
		if (body_signed < 0 || (size_t)body_signed + 1 > poly_max)
			continue;
		size_t body_off = (size_t)body_signed;
		uint8_t flag_byte = poly_data[body_off];
		int vcount = flag_byte & 0x3F;
		if (vcount < 2)
			continue;

		const uint8_t color = face_colors[f];
		/* Engine indexes materialcolors with (color & 0x7F) per
		 * drawpol_getlightvalue: low 7 bits are the 1-based material
		 * number, bit 7 (0x80) is the "unlit" flag. Shader subtracts 1
		 * to land on a zero-based texture row. */
		const uint8_t material = (uint8_t)(color & 0x7Fu);

		/* Face normal (Q15 unit vector). Stored INWARD in engine
		 * convention (drawpol's `face_dot = stored_fn · light_world`
		 * is positive when lit because both stored_fn and the engine
		 * light direction point INTO the surface). Internal proc-mesh
		 * logic (TieShipModelConverter_FaceUvBasis, winding alignment check) operates on
		 * this engine convention. Negation to geometric outward is
		 * applied at emit time, where GPU vertex data is written. */
		const float fn[3] = {
			(float)fh->normal_x * (1.0f / 32768.0f),
			(float)fh->normal_y * (1.0f / 32768.0f),
			(float)fh->normal_z * (1.0f / 32768.0f),
		};

		/* Line face body:
		 *   +1..+2  u16 thickness_base
		 *   +3      u8  vtx1_idx
		 *   +4      u8  vtx2_idx
		 *   +5      u8  edge_idx (engine cache key)
		 * Emit a screen-facing quad. Gouraud-eligible, lit lines use endpoint
		 * normals; all other lines use the face normal uniformly. */
		if (vcount == 2) {
			if (body_off + 5 > poly_max)
				continue;
			uint16_t thickness_base = (uint16_t)(poly_data[body_off + 1] | (poly_data[body_off + 2] << 8));
			uint8_t v1 = poly_data[body_off + 3];
			uint8_t v2 = poly_data[body_off + 4];
			if (v1 >= numpoints || v2 >= numpoints)
				continue;
			float a[3] = {
				(float)pos_resolved[3 * v1 + 0],
				(float)pos_resolved[3 * v1 + 1],
				(float)pos_resolved[3 * v1 + 2],
			};
			float b[3] = {
				(float)pos_resolved[3 * v2 + 0],
				(float)pos_resolved[3 * v2 + 1],
				(float)pos_resolved[3 * v2 + 2],
			};
			/* Per-vertex normals at the two endpoints (Q15 → float).
			 * Same conversion + zero-source fallback as the polygon
			 * path's vertex_normal write — see that block for the
			 * full rationale. Substitute the line's face normal (fn)
			 * when the engine stored (0,0,0) at an endpoint so the
			 * GPU never sees a zero vector that would NaN-poison
			 * the FS normalize chain. */
			const int16_t v1nx = norm_resolved[3 * v1 + 0];
			const int16_t v1ny = norm_resolved[3 * v1 + 1];
			const int16_t v1nz = norm_resolved[3 * v1 + 2];
			const int16_t v2nx = norm_resolved[3 * v2 + 0];
			const int16_t v2ny = norm_resolved[3 * v2 + 1];
			const int16_t v2nz = norm_resolved[3 * v2 + 2];
			float na[3], nb[3];
			if (v1nx == 0 && v1ny == 0 && v1nz == 0) {
				na[0] = fn[0];
				na[1] = fn[1];
				na[2] = fn[2];
			} else {
				na[0] = (float)v1nx * (1.0f / 32768.0f);
				na[1] = (float)v1ny * (1.0f / 32768.0f);
				na[2] = (float)v1nz * (1.0f / 32768.0f);
			}
			if (v2nx == 0 && v2ny == 0 && v2nz == 0) {
				nb[0] = fn[0];
				nb[1] = fn[1];
				nb[2] = fn[2];
			} else {
				nb[0] = (float)v2nx * (1.0f / 32768.0f);
				nb[1] = (float)v2ny * (1.0f / 32768.0f);
				nb[2] = (float)v2nz * (1.0f / 32768.0f);
			}
			/* Engine Gouraud-eligibility predicate, matching
			 * drawpol.c:538 (`(gouraudflag & flag_byte) & 0x40`)
			 * combined with drawpol_getlightvalue's color & 0x80
			 * gate at :594 (unlit lines fall to the flat path).
			 * gouraudflag is a runtime engine option; bake the
			 * face-flag-only portion here, since proc-mesh runs at
			 * mesh load time and a runtime gouraudflag toggle is
			 * unusual mid-mission. */
			const bool line_is_gouraud = (flag_byte & 0x40) && !(color & 0x80);
			if (lvb->count + 4 > 0xFFFFu)
				return false;
			uint16_t base_idx = (uint16_t)lvb->count;
			const float endpoints[4] = { 0.0f, 0.0f, 1.0f, 1.0f };
			const float sides[4] = { -1.0f, +1.0f, -1.0f, +1.0f };
			for (int k = 0; k < 4; ++k) {
				TieFlightLineVertex lv;
				lv.pos_a[0] = a[0];
				lv.pos_a[1] = a[1];
				lv.pos_a[2] = a[2];
				lv.pos_b[0] = b[0];
				lv.pos_b[1] = b[1];
				lv.pos_b[2] = b[2];
				/* Pick the source normal per the engine-correct
				 * gate. Gouraud-eligible → per-vertex at this
				 * endpoint (so raw_dot varies along the line and
				 * the rasterizer interpolates between the two
				 * endpoints' shades, matching the engine's
				 * vertexlight cache). Non-Gouraud → face normal
				 * (uniform per-line, varied face-to-face). */
				const bool at_b = (endpoints[k] > 0.5f);
				const float* src_normal;
				if (line_is_gouraud) {
					src_normal = at_b ? nb : na;
				} else {
					src_normal = fn;
				}
				/* Engine→standard normal-direction flip; see the
				 * matching note in the triangle emit below. */
				lv.normal[0] = -src_normal[0];
				lv.normal[1] = -src_normal[1];
				lv.normal[2] = -src_normal[2];
				lv.endpoint = endpoints[k];
				lv.side = sides[k];
				lv.color = (float)color;
				lv.material_id = (float)material;
				lv.thickness_base = (float)thickness_base;
				lv.mesh_index = (float)mesh_index;
				if (!TieShipModelConverter_LinevertbufPush(lvb, &lv))
					return false;
			}
			/* Two triangles per line. Index order winds CCW in NDC
			 * (HLSL +Y-up) for every line orientation: the screen-
			 * facing quad's vertex layout is {A-perp, A+perp, B-perp,
			 * B+perp} with `perp` being the 90°-CCW rotation of the
			 * line direction. The naive (0,1,2)/(1,3,2) order winds
			 * CW and makes every line fragment fail the pipeline's
			 * FRONTFACE_COUNTER_CLOCKWISE check — SV_IsFrontFace
			 * comes back false, the shared FS computes
			 * `side_sign * raw_dot = -raw_dot ≤ 0`, lambert saturates
			 * to 0, and every line renders black regardless of its
			 * actual lighting. (0,2,1)/(1,2,3) gives CCW for all
			 * orientations so lines pick up engine-faithful per-line
			 * shading from their face normal. */
			static const uint16_t tri_off[6] = { 0, 2, 1, 1, 2, 3 };
			for (int k = 0; k < 6; ++k)
				if (!TieShipModelConverter_IdxbufPush(lib, (uint16_t)(base_idx + tri_off[k])))
					return false;
			continue;
		}

		if (body_off + 1 + 2 * (size_t)vcount > poly_max)
			continue;

		/* Build the face's (u, v) basis. If basis construction fails
		 * (degenerate face), emit the fan with face_u/face_v = 0 and
		 * no decals — the geometry still draws, just without any
		 * decal overlay for this face. */
		TieShipModelConverterFaceBasis fb = TieShipModelConverter_FaceUvBasis(
			poly_data, poly_max, body_off, vcount, fn, pos_resolved, numpoints);

		/* Emit decals targeting this face. Iterate marking refs in
		 * record order so the resulting per-face decal sequence in drb
		 * preserves the engine's authoring order — the FS's first-hit
		 * loop then approximates the engine's xtrans2 last-write-wins
		 * swap chain (works perfectly for the common non-overlapping
		 * case). */
		const uint32_t decal_off = drb->count;
		if (fb.ok) {
			for (int mi = 0; mi < marking_count; ++mi) {
				if (marking_refs[mi].face_idx != (uint8_t)f)
					continue;
				TieShipModelConverter_EmitFaceMarkingsBody(poly_data, poly_max, marking_refs[mi].body_off,
														   body_off, numpoints, pos_resolved, &fb, dvb, drb);
			}
		}
		const uint32_t decal_cnt = drb->count - decal_off;

		/* Triangulate. Default is fan (cheap, correct for convex
		 * polygons — the dominant case in shipped TIE assets); if
		 * the face is concave the fan spills outside the polygon's
		 * actual silhouette, so we ear-clip instead.
		 *
		 * Both produce vcount - 2 triangles; the only difference is
		 * which set. Ear-clip never fires for triangles (vcount==3)
		 * or for faces with no (u, v) basis (degenerate edge 0→1) —
		 * those drop through to the fan path. Ear-clip can also
		 * stall on a malformed polygon (no remaining ears before
		 * the ring reaches 3 vertices); on stall we abandon the
		 * partial result and fan-triangulate as a safe fallback. */
		int tri_slots[(TIE_FLIGHT_FACE_MAX_VERTS - 2) * 3];
		int n_tris = 0;

		bool try_ear_clip = fb.ok && vcount > 3;
		if (try_ear_clip) {
			/* Project face vertices into the (u, v) plane and test
			 * convexity. If every signed cross product at the
			 * polygon's interior corners is non-negative the polygon
			 * is convex — the fan path is exactly correct and there's
			 * no need to ear-clip. */
			float face2d[TIE_FLIGHT_FACE_MAX_VERTS][2];
			bool all_valid = true;
			for (int s = 0; s < vcount; ++s) {
				uint8_t vidx = poly_data[body_off + 1 + 2 * s];
				if (vidx >= numpoints) {
					all_valid = false;
					break;
				}
				float dx = (float)pos_resolved[3 * vidx + 0] - fb.orig[0];
				float dy = (float)pos_resolved[3 * vidx + 1] - fb.orig[1];
				float dz = (float)pos_resolved[3 * vidx + 2] - fb.orig[2];
				face2d[s][0] = dx * fb.u_axis[0] + dy * fb.u_axis[1] + dz * fb.u_axis[2];
				face2d[s][1] = dx * fb.v_axis[0] + dy * fb.v_axis[1] + dz * fb.v_axis[2];
			}
			if (all_valid) {
				/* Convexity check: TieShipModelConverter_FaceUvBasis arranges the basis
				 * so the polygon winds CCW in (u, v), so a convex
				 * polygon has every (B-A) × (C-B) cross > 0. */
				bool is_convex = true;
				for (int i = 0; i < vcount; ++i) {
					int pi = (i + vcount - 1) % vcount;
					int ni = (i + 1) % vcount;
					float ax = face2d[pi][0], ay = face2d[pi][1];
					float bx = face2d[i][0], by = face2d[i][1];
					float cx = face2d[ni][0], cy = face2d[ni][1];
					float cross = (bx - ax) * (cy - by) - (by - ay) * (cx - bx);
					if (cross < 0.0f) {
						is_convex = false;
						break;
					}
				}
				if (!is_convex) {
					/* Concave path: ear-clip in 2D. O(N³) worst-case
					 * but N ≤ 63 and the loop body is trivial — total
					 * work is dwarfed by the GPU buffer push. */
					int poly[TIE_FLIGHT_FACE_MAX_VERTS];
					int poly_n = vcount;
					for (int i = 0; i < vcount; ++i)
						poly[i] = i;
					int safety = vcount * 2;
					while (poly_n > 3 && safety-- > 0) {
						int ear_at = -1;
						for (int i = 0; i < poly_n; ++i) {
							int pi = poly[(i + poly_n - 1) % poly_n];
							int ci = poly[i];
							int ni = poly[(i + 1) % poly_n];
							float ax = face2d[pi][0], ay = face2d[pi][1];
							float bx = face2d[ci][0], by = face2d[ci][1];
							float cx = face2d[ni][0], cy = face2d[ni][1];
							float cross = (bx - ax) * (cy - by) - (by - ay) * (cx - bx);
							if (cross <= 0.0f)
								continue; /* reflex / collinear */
							bool any_inside = false;
							for (int j = 0; j < poly_n; ++j) {
								int v_ix = poly[j];
								if (v_ix == pi || v_ix == ci || v_ix == ni)
									continue;
								float px = face2d[v_ix][0];
								float py = face2d[v_ix][1];
								float s1 = (bx - ax) * (py - ay) - (by - ay) * (px - ax);
								float s2 = (cx - bx) * (py - by) - (cy - by) * (px - bx);
								float s3 = (ax - cx) * (py - cy) - (ay - cy) * (px - cx);
								if (s1 >= 0.0f && s2 >= 0.0f && s3 >= 0.0f) {
									any_inside = true;
									break;
								}
							}
							if (any_inside)
								continue;
							ear_at = i;
							break;
						}
						if (ear_at < 0)
							break; /* stall — abandon */
						int pi = poly[(ear_at + poly_n - 1) % poly_n];
						int ci = poly[ear_at];
						int ni = poly[(ear_at + 1) % poly_n];
						tri_slots[n_tris * 3 + 0] = pi;
						tri_slots[n_tris * 3 + 1] = ci;
						tri_slots[n_tris * 3 + 2] = ni;
						++n_tris;
						for (int j = ear_at; j < poly_n - 1; ++j)
							poly[j] = poly[j + 1];
						--poly_n;
					}
					if (poly_n == 3 && n_tris == vcount - 3) {
						tri_slots[n_tris * 3 + 0] = poly[0];
						tri_slots[n_tris * 3 + 1] = poly[1];
						tri_slots[n_tris * 3 + 2] = poly[2];
						++n_tris;
					}
					/* If ear-clip stalled, n_tris < vcount - 2; drop
					 * the partial result and fall through to fan. */
					if (n_tris != vcount - 2)
						n_tris = 0;
				}
			}
		}

		if (n_tris == 0) {
			/* Fan: (v0, v1, v2), (v0, v2, v3), ... — vcount - 2 tris. */
			for (int i = 1; i + 1 < vcount; ++i) {
				tri_slots[n_tris * 3 + 0] = 0;
				tri_slots[n_tris * 3 + 1] = i;
				tri_slots[n_tris * 3 + 2] = i + 1;
				++n_tris;
			}
		}

		/* Emit each triangle.
		 *
		 * When the face basis had to flip v_axis (`fb.v_flipped`), the
		 * triangulation produced triangles whose geometric normal is
		 * `-fn` instead of `+fn`. Swap slots 1 and 2 to reverse winding
		 * for those faces so the GPU's CCW = front classification lines
		 * up with the authored normal direction. Degenerate faces
		 * (fb.ok == false) keep the default winding — they have no
		 * meaningful normal-vs-winding relationship anyway. */
		const bool swap_winding = fb.ok && fb.v_flipped;
		for (int t = 0; t < n_tris; ++t) {
			int slot[3];
			slot[0] = tri_slots[t * 3 + 0];
			slot[1] = swap_winding ? tri_slots[t * 3 + 2] : tri_slots[t * 3 + 1];
			slot[2] = swap_winding ? tri_slots[t * 3 + 1] : tri_slots[t * 3 + 2];

			for (int k = 0; k < 3; ++k) {
				uint8_t vidx = poly_data[body_off + 1 + 2 * slot[k]];
				if (vidx >= numpoints) {
					/* Bad index — drop the whole mesh. */
					return false;
				}
				TieFlightVertex out;
				out.pos[0] = (float)pos_resolved[3 * vidx + 0];
				out.pos[1] = (float)pos_resolved[3 * vidx + 1];
				out.pos[2] = (float)pos_resolved[3 * vidx + 2];
				/* Engine stores normals INWARD; shaders expect
				 * geometric OUTWARD (standard PBR). Negate at this
				 * boundary so every downstream consumer sees the
				 * standard direction. Same treatment applied to
				 * vertex_normal below. */
				out.normal[0] = -fn[0];
				out.normal[1] = -fn[1];
				out.normal[2] = -fn[2];
				/* Per-vertex normal from the engine's stored vertex-
				 * normal stream. Engine mesh data legitimately stores
				 * (0, 0, 0) at vertices used only by line / flat-
				 * shaded faces (the engine's integer Gouraud path
				 * tolerates a zero dot — darkest shade).
				 *
				 * For HD float math substitute the face normal at
				 * zero-source vertices so downstream dot/normalize
				 * operations stay finite. Gouraud-eligible faces then
				 * degrade to flat shading at the rare zero-source
				 * vertex instead of producing NaN. */
				const int16_t vn_x = norm_resolved[3 * vidx + 0];
				const int16_t vn_y = norm_resolved[3 * vidx + 1];
				const int16_t vn_z = norm_resolved[3 * vidx + 2];
				if (vn_x == 0 && vn_y == 0 && vn_z == 0) {
					out.vertex_normal[0] = -fn[0];
					out.vertex_normal[1] = -fn[1];
					out.vertex_normal[2] = -fn[2];
				} else {
					out.vertex_normal[0] = -(float)vn_x * (1.0f / 32768.0f);
					out.vertex_normal[1] = -(float)vn_y * (1.0f / 32768.0f);
					out.vertex_normal[2] = -(float)vn_z * (1.0f / 32768.0f);
				}
				/* Per-face Gouraud gates — see drawpol.c:538-597:
				 *   bit 0: face flag_byte & 0x40 set AND color & 0x80
				 *          clear (engine's combined "Gouraud-eligible"
				 *          predicate).
				 *   bit 1: flag_byte == 194 (0x80|0x40|0x02) — two-
				 *          sided unlit-edge sentinel that preserves
				 *          negative dot products (drawpol.c:559). Set
				 *          for documentation; vcount=2 path is the
				 *          one that actually emits it, but it never
				 *          reaches here. */
				uint8_t ff_bits = 0;
				if ((flag_byte & 0x40) && !(color & 0x80))
					ff_bits |= 0x01;
				if (flag_byte == 194)
					ff_bits |= 0x02;
				out.face_flags = (float)ff_bits;
				/* Strip bit 0x80 — see drawpol.c:604; 3D mesh faces
				 * always shade. The FS's `color & 0x80 → unlit` branch
				 * is reserved for laser-bolt line polys. */
				out.color = (float)(color & 0x7Fu);
				out.material_id = (float)material;
				out.mesh_index = (float)mesh_index;

				/* Face-local (u, v) for the FS decal overlay. */
				if (fb.ok) {
					TieFlightDecalVertex uv = TieShipModelConverter_ProjectUv(&fb, out.pos);
					out.face_u = uv.u;
					out.face_v = uv.v;
					out.decal_offset = (float)decal_off;
					out.decal_count = (float)decal_cnt;
				} else {
					out.face_u = 0.0f;
					out.face_v = 0.0f;
					out.decal_offset = 0.0f;
					out.decal_count = 0.0f;
				}

				if (vb->count >= 0xFFFFu)
					return false; /* uint16 index space exhausted */
				uint16_t out_idx = (uint16_t)vb->count;
				if (!TieShipModelConverter_VertbufPush(vb, &out))
					return false;
				if (!TieShipModelConverter_IdxbufPush(ib, out_idx))
					return false;
			}
		}
	}
	return true;
}

/* ---------------------------------------------------------------------------
 * Public API.
 * --------------------------------------------------------------------------- */

typedef struct TieShipModelConverterCanonicalPosition {
	float position[3];
	uint32_t vertex_index;
} TieShipModelConverterCanonicalPosition;

static int TieShipModelConverter_CompareCanonicalPosition(const void* left, const void* right) {
	const TieShipModelConverterCanonicalPosition* a = left;
	const TieShipModelConverterCanonicalPosition* b = right;
	for (int axis = 0; axis < 3; ++axis) {
		if (a->position[axis] < b->position[axis])
			return -1;
		if (a->position[axis] > b->position[axis])
			return 1;
	}
	return a->vertex_index < b->vertex_index ? -1 : a->vertex_index > b->vertex_index ? 1 : 0;
}

static bool TieShipModelConverter_RebuildCornerNormals(TieShipModelConverterVertexBuffer* vertices,
													   const TieShipModelConverterIndexBuffer* indices,
													   float smooth_angle_degrees) {
	if (!vertices || !indices || indices->count == 0 || indices->count % 3 != 0)
		return false;

	const uint32_t vertex_count = vertices->count;
	const uint32_t triangle_count = indices->count / 3;
	TieShipModelConverterCanonicalPosition* sorted = calloc(vertex_count, sizeof *sorted);
	uint32_t* canonical_by_vertex = malloc((size_t)vertex_count * sizeof *canonical_by_vertex);
	float* positions = malloc((size_t)vertex_count * 3 * sizeof *positions);
	uint32_t* triangle_positions = malloc((size_t)indices->count * sizeof *triangle_positions);
	float* corner_normals = malloc((size_t)indices->count * 3 * sizeof *corner_normals);
	if (!sorted || !canonical_by_vertex || !positions || !triangle_positions || !corner_normals) {
		free(sorted);
		free(canonical_by_vertex);
		free(positions);
		free(triangle_positions);
		free(corner_normals);
		return false;
	}

	for (uint32_t vertex = 0; vertex < vertex_count; ++vertex) {
		memcpy(sorted[vertex].position, vertices->data[vertex].pos, sizeof sorted[vertex].position);
		sorted[vertex].vertex_index = vertex;
	}
	qsort(sorted, vertex_count, sizeof *sorted, TieShipModelConverter_CompareCanonicalPosition);

	uint32_t canonical_count = 0;
	for (uint32_t item = 0; item < vertex_count; ++item) {
		if (item == 0 ||
			memcmp(sorted[item].position, sorted[item - 1].position, sizeof sorted[item].position) != 0) {
			memcpy(positions + (size_t)canonical_count * 3, sorted[item].position,
				   sizeof sorted[item].position);
			++canonical_count;
		}
		canonical_by_vertex[sorted[item].vertex_index] = canonical_count - 1;
	}

	for (uint32_t triangle = 0; triangle < triangle_count; ++triangle) {
		const uint16_t i0 = indices->data[triangle * 3 + 0];
		const uint16_t i1 = indices->data[triangle * 3 + 1];
		const uint16_t i2 = indices->data[triangle * 3 + 2];
		if (i0 >= vertex_count || i1 >= vertex_count || i2 >= vertex_count) {
			canonical_count = 0;
			break;
		}
		/* ShipModelData face normals are outward after negation, while its
		 * emitted winding is inward. Reverse the topology supplied to the
		 * conventional cross-product builder. */
		triangle_positions[triangle * 3 + 0] = canonical_by_vertex[i0];
		triangle_positions[triangle * 3 + 1] = canonical_by_vertex[i2];
		triangle_positions[triangle * 3 + 2] = canonical_by_vertex[i1];
	}

	AeronMeshNormalsError normal_error = { 0 };
	bool built = canonical_count > 0 && Aeron_MeshNormalsBuildCorners(
											&(AeronMeshNormalsInput) {
												.positions = positions,
												.position_stride = 3 * sizeof(float),
												.position_count = canonical_count,
												.triangle_position_indices = triangle_positions,
												.triangle_count = triangle_count,
												.smooth_angle_degrees = smooth_angle_degrees,
											},
											corner_normals, &normal_error);
	if (built) {
		for (uint32_t triangle = 0; triangle < triangle_count; ++triangle) {
			const uint16_t output_vertices[3] = {
				indices->data[triangle * 3 + 0],
				indices->data[triangle * 3 + 2],
				indices->data[triangle * 3 + 1],
			};
			for (uint32_t corner = 0; corner < 3; ++corner) {
				TieFlightVertex* vertex = &vertices->data[output_vertices[corner]];
				if ((uint32_t)vertex->face_flags & 1u)
					memcpy(vertex->vertex_normal, corner_normals + ((size_t)triangle * 3 + corner) * 3,
						   sizeof vertex->vertex_normal);
				else
					memcpy(vertex->vertex_normal, vertex->normal, sizeof vertex->vertex_normal);
			}
		}
	}

	free(sorted);
	free(canonical_by_vertex);
	free(positions);
	free(triangle_positions);
	free(corner_normals);
	return built;
}

void TieShipModelConverter_Free(TieFlightShipModel* m) {
	if (!m)
		return;
	free(m->vertices);
	free(m->indices);
	free(m->line_vertices);
	free(m->line_indices);
	free(m->line_lods);
	free(m->decals);
	free(m->decal_verts);
	free(m->mesh_rot);
	memset(m, 0, sizeof *m);
}

bool TieShipModelConverter_Build(const void* blob, size_t blob_size, float smooth_angle_degrees,
								 TieFlightShipModel* out) {
	if (!blob || !out)
		return false;
	memset(out, 0, sizeof *out);
	if (blob_size < 2 + 0x20)
		return false;

	/* Skip the 2-byte file-size prefix; sub-modules cast at +2. */
	const uint8_t* base = (const uint8_t*)blob + 2;
	size_t base_size = blob_size - 2;
	const TieShipModelConverterData* md = (const TieShipModelConverterData*)base;
	if (md->num_meshes == 0)
		return false;

	/* Mesh table sits at base + 0x20 + 6 * num_lods. */
	size_t lod_table_size = (size_t)6 * md->num_lods;
	size_t mesh_table_off = 0x20 + lod_table_size;
	if (mesh_table_off + (size_t)md->num_meshes * sizeof(TieShipModelConverterMesh) > base_size)
		return false;
	const TieShipModelConverterMesh* meshes = (const TieShipModelConverterMesh*)(base + mesh_table_off);

	TieShipModelConverterVertexBuffer vb = { 0 };
	TieShipModelConverterIndexBuffer ib = { 0 };
	TieShipModelConverterLineVertexBuffer lvb = { 0 }; /* antenna / strut line buffer */
	TieShipModelConverterIndexBuffer lib = { 0 };
	TieShipModelConverterDecalVertexBuffer dvb = { 0 }; /* decal polygon vertices in face (u, v) */
	TieShipModelConverterDecalBuffer drb = { 0 };       /* decal records (vert window + colour) */
	float bmin[3] = { 1e30f, 1e30f, 1e30f };
	float bmax[3] = { -1e30f, -1e30f, -1e30f };

	/* Per-mesh rotation table — one entry per source ShipModelMesh,
	 * filled in step with the polygon extraction so a missing
	 * rotation_offset stays as identity (has_rotation = 0). */
	AeronMeshRot* mesh_rot = (AeronMeshRot*)calloc(md->num_meshes, sizeof *mesh_rot);
	if (!mesh_rot)
		return false;

	for (int mi = 0; mi < md->num_meshes; ++mi) {
		const TieShipModelConverterMesh* mesh = &meshes[mi];

		/* Reference-only slot (hardpoints, dock anchors, etc.): no
		 * geometry attached. The engine never reads this mesh's
		 * render_offset because the BSP tree doesn't reference it.
		 * Skip explicitly instead of relying on TieShipModelConverter_ExtractMeshPolys'
		 * header-byte check to silently drop garbage. */
		if (mesh->render_offset == 0)
			continue;

		/* Pick the highest-detail per-mesh LOD record (the first
		 * entry — ShipMeshLOD records are sorted by ascending
		 * distance threshold, so [0] is the closest / highest
		 * detail). This matches the engine's HIGH-detail mode
		 * (shipdetailvalue == -1, draw.c:443-445), where
		 * z_threshold is halved before the LOD walk so each mesh
		 * lands on a closer-range record. At z_threshold = 0 that's
		 * always LOD[0]. */
		size_t mesh_off = (size_t)((const uint8_t*)mesh - base);
		size_t lod_off = mesh_off + mesh->render_offset;
		if (lod_off + sizeof(TieShipModelConverterMeshLod) > base_size)
			goto failed;
		const TieShipModelConverterMeshLod* lod = (const TieShipModelConverterMeshLod*)(base + lod_off);

		/* `lod->distance == 0x7FFFFFFF` is NOT a "skip" sentinel —
		 * it just means "this single LOD applies at every
		 * z-threshold". The engine's prune at draw_gettreeorder
		 * (draw.c:662-668) fires only for `shipdetailvalue == 1`
		 * (low-detail mode) as a cycle-saving heuristic; the
		 * dropped meshes are perfectly renderable. We're in the
		 * equivalent of high-detail mode (LOD[0] at z=0) — always
		 * emit and let TieShipModelConverter_ExtractMeshPolys' 0x80..0x83 header guard
		 * catch any truly-broken offset. */

		size_t poly_off = lod_off + lod->offset;
		if (poly_off + 5 > base_size)
			goto failed;
		const uint8_t* poly_data = base + poly_off;
		size_t poly_max = base_size - poly_off;

		/* Resolve mesh origin in craft frame. has_position selects
		 * pos_xyz over center_*. Same rule the engine uses in
		 * draw_getcompdetailptr (draw.c:399-413). */
		int16_t cs, cf, cu;
		if (mesh->has_position) {
			cs = mesh->pos_side;
			cf = mesh->pos_fwd;
			cu = mesh->pos_up;
		} else {
			cs = mesh->center_side;
			cf = mesh->center_fwd;
			cu = mesh->center_up;
		}

		if (!TieShipModelConverter_ExtractMeshPolys(poly_data, poly_max, cs, cf, cu, (uint32_t)mi, &vb, &ib,
													&lvb, &lib, &dvb, &drb)) {
			goto failed;
		}

		/* Record the mesh's engine MeshType so the host can apply
		 * per-mesh-type overrides (e.g. drawmarkingsflag's species-
		 * 17-wing rule at draw.c:894). Truncating the on-disk
		 * uint16 to uint8 is safe — shipped MeshType values are all
		 * < 32. */
		mesh_rot[mi].mesh_type = (uint8_t)mesh->mesh_type;

		/* Read ComponentRotData if the mesh has rotation_offset.
		 * Axis is Q15 (raw / 32768 = unit float); pivot is in the
		 * same craft-local int16 units as the vertex positions. */
		if (mesh->rotation_offset) {
			size_t rd_off = mesh_off + mesh->rotation_offset;
			if (rd_off + sizeof(TieShipModelConverterRotationData) > base_size)
				goto failed;
			const TieShipModelConverterRotationData* rd =
				(const TieShipModelConverterRotationData*)(base + rd_off);
			mesh_rot[mi].axis[0] = (float)rd->axis_y * (1.0f / 32768.0f);
			mesh_rot[mi].axis[1] = (float)rd->axis_x * (1.0f / 32768.0f);
			mesh_rot[mi].axis[2] = (float)rd->axis_z * (1.0f / 32768.0f);
			mesh_rot[mi].pivot[0] = (float)rd->pivot_value;
			mesh_rot[mi].pivot[1] = (float)rd->pivot_x;
			mesh_rot[mi].pivot[2] = (float)rd->pivot_z;
			mesh_rot[mi].has_rotation = 1;
		}
	}

	/* Triangles are required (no triangles → not a renderable ship).
	 * Line-only meshes are not expected in shipped data; an empty
	 * line buffer is fine. */
	if (vb.count == 0 || ib.count == 0 ||
		!TieShipModelConverter_RebuildCornerNormals(&vb, &ib, smooth_angle_degrees))
		goto failed;

	for (uint32_t i = 0; i < vb.count; ++i) {
		const float* p = vb.data[i].pos;
		if (p[0] < bmin[0])
			bmin[0] = p[0];
		if (p[1] < bmin[1])
			bmin[1] = p[1];
		if (p[2] < bmin[2])
			bmin[2] = p[2];
		if (p[0] > bmax[0])
			bmax[0] = p[0];
		if (p[1] > bmax[1])
			bmax[1] = p[1];
		if (p[2] > bmax[2])
			bmax[2] = p[2];
	}

	out->vertices = vb.data;
	out->indices = ib.data;
	out->vertex_count = vb.count;
	out->index_count = ib.count;
	out->line_vertices = lvb.data;
	out->line_indices = lib.data;
	out->line_vertex_count = lvb.count;
	out->line_index_count = lib.count;
	out->decals = drb.data;
	out->decal_verts = dvb.data;
	out->decal_count = drb.count;
	out->decal_vert_count = dvb.count;
	out->mesh_rot = mesh_rot;
	out->mesh_count = md->num_meshes;
	out->model_scale_shift = md->model_scale_shift;
	memcpy(out->bound_min, bmin, sizeof bmin);
	memcpy(out->bound_max, bmax, sizeof bmax);
	return true;

failed:
	free(vb.data);
	free(ib.data);
	free(lvb.data);
	free(lib.data);
	free(dvb.data);
	free(drb.data);
	free(mesh_rot);
	return false;
}

/* Laser blobs contain a sentinel-terminated LOD table followed by a line
 * polyobject: a four-byte header, packed int16 vertices, and five-byte edges
 * containing thickness, endpoint indices, and palette color. Convert those
 * edges through the same line-quad path used for ship surface details. */
/* Append one LOD's line geometry to the shared lvb/lib buffers,
 * updating bmin/bmax, and recording the IBO segment range. Returns
 * false on malformed input or allocation failure; the caller frees partial
 * state and rejects the required projectile model. */
static bool TieShipModelConverter_AppendLaserLod(const uint8_t* base, size_t blob_size, size_t poly_off,
												 TieShipModelConverterLineVertexBuffer* lvb,
												 TieShipModelConverterIndexBuffer* lib, float bmin[3],
												 float bmax[3], uint32_t* out_index_offset,
												 uint32_t* out_index_count) {
	*out_index_offset = lib->count;
	*out_index_count = 0;

	if (poly_off + 4 > blob_size)
		return false;
	const uint8_t* data = base + poly_off;
	uint8_t type = data[0];
	uint8_t numpoints = data[2];
	uint8_t numedges = data[3];
	if (type != 0x40 && type != 0x41)
		return false;
	if (numpoints == 0 || numedges == 0)
		return false;

	size_t verts_off = poly_off + 4;
	size_t edges_off = verts_off + (size_t)6 * numpoints;
	size_t edges_end = edges_off + (size_t)5 * numedges;
	if (edges_end > blob_size)
		return false;

	const TieShipModelConverterPolyVertexI16* pts_raw =
		(const TieShipModelConverterPolyVertexI16*)(base + verts_off);
	int16_t pos_resolved[3 * 256];
	TieShipModelConverter_DedupVertexStream(pts_raw, numpoints, pos_resolved);

	const uint32_t ib_start = lib->count;
	const uint8_t* edge = base + edges_off;
	for (int e = 0; e < numedges; ++e, edge += 5) {
		uint16_t thickness_base = (uint16_t)(edge[0] | (edge[1] << 8));
		uint8_t v1 = edge[2];
		uint8_t v2 = edge[3];
		uint8_t color = edge[4];
		if (v1 >= numpoints || v2 >= numpoints)
			return false;
		uint8_t material = (uint8_t)(color & 0x7Fu);

		float a[3] = {
			(float)pos_resolved[3 * v1 + 0],
			(float)pos_resolved[3 * v1 + 1],
			(float)pos_resolved[3 * v1 + 2],
		};
		float b[3] = {
			(float)pos_resolved[3 * v2 + 0],
			(float)pos_resolved[3 * v2 + 1],
			(float)pos_resolved[3 * v2 + 2],
		};
		for (int k = 0; k < 3; ++k) {
			if (a[k] < bmin[k])
				bmin[k] = a[k];
			if (b[k] < bmin[k])
				bmin[k] = b[k];
			if (a[k] > bmax[k])
				bmax[k] = a[k];
			if (b[k] > bmax[k])
				bmax[k] = b[k];
		}
		if (lvb->count + 4 > 0xFFFFu)
			return false;
		uint16_t base_idx = (uint16_t)lvb->count;
		const float endpoints[4] = { 0.0f, 0.0f, 1.0f, 1.0f };
		const float sides[4] = { -1.0f, +1.0f, -1.0f, +1.0f };
		for (int k = 0; k < 4; ++k) {
			TieFlightLineVertex lv;
			lv.pos_a[0] = a[0];
			lv.pos_a[1] = a[1];
			lv.pos_a[2] = a[2];
			lv.pos_b[0] = b[0];
			lv.pos_b[1] = b[1];
			lv.pos_b[2] = b[2];
			/* Line polys carry no face normal; supply +Z so shaders
			 * that consume normal won't NaN. */
			lv.normal[0] = 0.0f;
			lv.normal[1] = 0.0f;
			lv.normal[2] = 1.0f;
			lv.endpoint = endpoints[k];
			lv.side = sides[k];
			lv.color = (float)color;
			lv.material_id = (float)material;
			lv.thickness_base = (float)thickness_base;
			lv.mesh_index = 0.0f; /* single static mesh */
			if (!TieShipModelConverter_LinevertbufPush(lvb, &lv))
				return false;
		}
		/* CCW winding for FRONTFACE_COUNTER_CLOCKWISE pipeline —
		 * see the equivalent comment in the 3D-mesh line emitter
		 * above. (0,1,2)/(1,3,2) winds CW and falls in the shared
		 * FS's back-face branch, blanking the line via
		 * `lambert = saturate(-raw_dot)`. */
		static const uint16_t tri_off[6] = { 0, 2, 1, 1, 2, 3 };
		for (int k = 0; k < 6; ++k) {
			if (!TieShipModelConverter_IdxbufPush(lib, (uint16_t)(base_idx + tri_off[k])))
				return false;
		}
	}

	*out_index_count = lib->count - ib_start;
	return true;
}

bool TieShipModelConverter_BuildLaser(const void* blob, size_t blob_size, TieFlightShipModel* out) {
	if (!blob || !out)
		return false;
	memset(out, 0, sizeof *out);
	if (blob_size < sizeof(TieShipModelConverterMeshLod) + 4)
		return false;

	const uint8_t* base = (const uint8_t*)blob;

	/* Walk the ShipMeshLOD chain. The chain is terminated by an
	 * entry whose distance == INT_MAX (this is the engine convention
	 * — `draw_getdetailptr` halts there). Cap at 16 entries as a
	 * defensive bound against malformed blobs. */
	enum { MAX_LOD_ENTRIES = 16 };
	TieFlightShipModelLineLod segs[MAX_LOD_ENTRIES];
	uint32_t seg_count = 0;

	TieShipModelConverterLineVertexBuffer lvb = { 0 };
	TieShipModelConverterIndexBuffer lib = { 0 };
	bool terminated = false;
	float bmin[3] = { 1e30f, 1e30f, 1e30f };
	float bmax[3] = { -1e30f, -1e30f, -1e30f };

	for (uint32_t i = 0; i < MAX_LOD_ENTRIES; ++i) {
		size_t rec_off = (size_t)i * sizeof(TieShipModelConverterMeshLod);
		if (rec_off + sizeof(TieShipModelConverterMeshLod) > blob_size)
			goto failed_laser;
		const TieShipModelConverterMeshLod* lod = (const TieShipModelConverterMeshLod*)(base + rec_off);

		/* Engine: each ShipMeshLOD's `offset` is relative to the LOD
		 * record itself (draw.c:458 `(uint8_t*)p + p->offset`). */
		size_t poly_off = rec_off + (size_t)lod->offset;

		uint32_t idx_off = 0, idx_cnt = 0;
		if (!TieShipModelConverter_AppendLaserLod(base, blob_size, poly_off, &lvb, &lib, bmin, bmax, &idx_off,
												  &idx_cnt)) {
			goto failed_laser;
		}

		if (idx_cnt > 0 && seg_count < MAX_LOD_ENTRIES) {
			/* The threshold already uses native view units. INT_MAX → +INFINITY so the LOD picker's
			 * `<= threshold` always matches as a fallback. */
			float dist_f = (lod->distance == 0x7FFFFFFF) ? INFINITY : (float)lod->distance;
			segs[seg_count].index_offset = idx_off;
			segs[seg_count].index_count = idx_cnt;
			segs[seg_count].distance_view = dist_f;
			++seg_count;
		}

		/* Terminator — stop walking after consuming this slot. */
		if (lod->distance == 0x7FFFFFFF) {
			terminated = true;
			break;
		}
	}

	if (!terminated || lvb.count == 0 || seg_count == 0)
		goto failed_laser;

	TieFlightShipModelLineLod* lod_table = (TieFlightShipModelLineLod*)calloc(seg_count, sizeof *lod_table);
	if (!lod_table) {
		free(lvb.data);
		free(lib.data);
		return false;
	}
	memcpy(lod_table, segs, seg_count * sizeof *lod_table);

	/* Allocate a single static-mesh entry so the per-craft draw can
	 * push an identity affine + visibility=1 cbuffer. */
	AeronMeshRot* mesh_rot = (AeronMeshRot*)calloc(1, sizeof *mesh_rot);
	if (!mesh_rot) {
		free(lvb.data);
		free(lib.data);
		free(lod_table);
		return false;
	}

	out->vertices = NULL;
	out->indices = NULL;
	out->vertex_count = 0;
	out->index_count = 0;
	out->line_vertices = lvb.data;
	out->line_indices = lib.data;
	out->line_vertex_count = lvb.count;
	out->line_index_count = lib.count;
	out->line_lods = lod_table;
	out->line_lod_count = seg_count;
	out->mesh_rot = mesh_rot;
	out->mesh_count = 1;
	memcpy(out->bound_min, bmin, sizeof bmin);
	memcpy(out->bound_max, bmax, sizeof bmax);
	return true;

failed_laser:
	free(lvb.data);
	free(lib.data);
	return false;
}
