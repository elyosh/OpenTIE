/*
 * Ship → glTF 2.0 export. See gltf_export.h for the contract.
 *
 * Buffer layout (one .bin per ship):
 *   [0 ..)            float3 POSITION × vertex_count
 *   [posN ..)         float3 NORMAL   × vertex_count   (vertex_normal)
 *   [posN+normN ..)   uint16 INDICES  × index_count
 *
 * Accessors:
 *   acc_pos      → POSITION view; shared across every primitive
 *   acc_nor      → NORMAL view;   shared across every primitive
 *   acc_idx[p]   → INDICES sub-view for primitive p
 *
 * Primitive grouping: walk every triangle in the index buffer, group
 * by (mesh_index, material_id) — that's the natural unit of "share
 * one glTF material under one glTF mesh node". Each group becomes
 * one cgltf_primitive; primitives sharing a mesh_index get parented
 * under the same cgltf_mesh.
 */

#include "gltf_export.h"

#define CGLTF_WRITE_IMPLEMENTATION
#include "cgltf_write.h"

#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------------------------------------------------------------------
 * Triangle group enumeration.
 * Each unique (mesh_index, material_id) pair forms one primitive.
 * ------------------------------------------------------------------- */

typedef struct {
	uint16_t mesh_index;
	uint16_t material_id;
	uint32_t first_tri; /* index into the per-group triangle list */
	uint32_t tri_count;
} TieLfd2GltfGroupKey;

typedef struct {
	TieLfd2GltfGroupKey* groups;
	uint32_t group_count;
	uint32_t* tri_perm; /* triangle indices, permuted so each group is contiguous */
	uint32_t total_tris;
} TieLfd2GltfGroupTable;

/* Build the per-(mesh_index, material_id) grouping. Returns false on
 * alloc failure; caller checks before use. */
static bool TieLfd2GltfExport_BuildGroups(const TieFlightShipModel* pm, TieLfd2GltfGroupTable* out) {
	out->groups = NULL;
	out->group_count = 0;
	out->tri_perm = NULL;
	out->total_tris = pm->index_count / 3;

	if (out->total_tris == 0)
		return true;

	/* First pass: identify unique (mesh_index, material_id) pairs.
	 * Worst case: every triangle has a distinct key — sized for that. */
	TieLfd2GltfGroupKey* temp = (TieLfd2GltfGroupKey*)calloc(out->total_tris, sizeof(TieLfd2GltfGroupKey));
	if (!temp)
		return false;

	/* Per-triangle key array, used to bucket sort later. */
	uint32_t* tri_key = (uint32_t*)malloc(out->total_tris * sizeof(uint32_t));
	if (!tri_key) {
		free(temp);
		return false;
	}

	for (uint32_t t = 0; t < out->total_tris; ++t) {
		/* All 3 corners of a triangle carry the same mesh_index +
		 * material_id (the converter sets them at face emit time). Read
		 * the first corner. */
		uint16_t i0 = pm->indices[3 * t];
		if (i0 >= pm->vertex_count) {
			i0 = 0;
		}
		uint16_t mi = (uint16_t)pm->vertices[i0].mesh_index;
		uint16_t mat = (uint16_t)pm->vertices[i0].material_id;
		uint32_t key = ((uint32_t)mi << 16) | mat;
		tri_key[t] = key;

		/* Linear search of seen groups — N small, no need for a hash. */
		bool found = false;
		for (uint32_t g = 0; g < out->group_count; ++g) {
			if ((((uint32_t)temp[g].mesh_index << 16) | temp[g].material_id) == key) {
				temp[g].tri_count++;
				found = true;
				break;
			}
		}
		if (!found) {
			temp[out->group_count].mesh_index = mi;
			temp[out->group_count].material_id = mat;
			temp[out->group_count].tri_count = 1;
			out->group_count++;
		}
	}

	/* Assign each group its slice in `tri_perm`. */
	uint32_t cursor = 0;
	for (uint32_t g = 0; g < out->group_count; ++g) {
		temp[g].first_tri = cursor;
		cursor += temp[g].tri_count;
		temp[g].tri_count = 0; /* reused as fill cursor below */
	}

	out->tri_perm = (uint32_t*)malloc(out->total_tris * sizeof(uint32_t));
	if (!out->tri_perm) {
		free(temp);
		free(tri_key);
		return false;
	}

	/* Second pass: bucket-place triangle indices into their group's slot. */
	for (uint32_t t = 0; t < out->total_tris; ++t) {
		uint32_t key = tri_key[t];
		for (uint32_t g = 0; g < out->group_count; ++g) {
			if ((((uint32_t)temp[g].mesh_index << 16) | temp[g].material_id) == key) {
				out->tri_perm[temp[g].first_tri + temp[g].tri_count] = t;
				temp[g].tri_count++;
				break;
			}
		}
	}
	/* Output array sized exactly. */
	out->groups = (TieLfd2GltfGroupKey*)malloc(out->group_count * sizeof(TieLfd2GltfGroupKey));
	if (!out->groups) {
		free(temp);
		free(tri_key);
		free(out->tri_perm);
		return false;
	}
	memcpy(out->groups, temp, out->group_count * sizeof(TieLfd2GltfGroupKey));
	free(temp);
	free(tri_key);
	return true;
}

static void TieLfd2GltfExport_FreeGroups(TieLfd2GltfGroupTable* g) {
	free(g->groups);
	free(g->tri_perm);
	memset(g, 0, sizeof *g);
}

/* ---------------------------------------------------------------------
 * Material colour: HSL hue rotation by golden ratio so consecutive
 * material_id values are visually well-separated.
 * ------------------------------------------------------------------- */

static void TieLfd2GltfExport_HslToRgb(float h, float s, float l, float out[3]) {
	/* Standard HSL → RGB. h in [0,1). */
	const float c = (1.0f - fabsf(2.0f * l - 1.0f)) * s;
	const float h6 = h * 6.0f;
	const float x = c * (1.0f - fabsf(fmodf(h6, 2.0f) - 1.0f));
	float r = 0, g = 0, b = 0;
	if (h6 < 1.0f) {
		r = c;
		g = x;
	} else if (h6 < 2.0f) {
		r = x;
		g = c;
	} else if (h6 < 3.0f) {
		g = c;
		b = x;
	} else if (h6 < 4.0f) {
		g = x;
		b = c;
	} else if (h6 < 5.0f) {
		r = x;
		b = c;
	} else {
		r = c;
		b = x;
	}
	const float m = l - 0.5f * c;
	out[0] = r + m;
	out[1] = g + m;
	out[2] = b + m;
}

static void TieLfd2GltfExport_MaterialColorForId(uint16_t mat_id, float out[3]) {
	/* Golden-ratio hue rotation. Constant saturation/lightness keeps
	 * tones consistent so users can identify "this is material 3" by
	 * its hue across multiple ships. */
	const float hue = fmodf((float)mat_id * 0.61803398874989484820f, 1.0f);
	TieLfd2GltfExport_HslToRgb(hue, 0.55f, 0.55f, out);
}

/* ---------------------------------------------------------------------
 * Best-effort mesh_type → name lookup. Unknown types fall through to
 * "type<N>"; collected from inline comments in src/tie/. */
static const char* TieLfd2GltfExport_MeshTypeName(uint8_t t) {
	switch (t) {
		case 1:
			return "MainHull";
		case 2:
			return "Wing";
		case 20:
			return "AltWing";
		default:
			return NULL;
	}
}

/* ---------------------------------------------------------------------
 * .bin writer. Layout matches the bufferView offsets we set up in the
 * cgltf_data below.
 * ------------------------------------------------------------------- */

static bool TieLfd2GltfExport_WriteBin(const char* path, const TieFlightShipModel* pm,
									   const TieLfd2GltfGroupTable* gt, size_t* out_bin_size) {
	FILE* fp = fopen(path, "wb");
	if (!fp) {
		fprintf(stderr, "  open %s for write: %s\n", path, strerror(errno));
		return false;
	}
	size_t total = 0;

	/* Rotate engine (side, forward, up) coordinates into glTF:
	 *     gltf_x =  engine_x      (side stays put)
	 *     gltf_y =  engine_z      (up stays up)
	 *     gltf_z = -engine_y      (engine -Y nose → glTF +Z front)
	 * This proper rotation preserves handedness and applies to normals too. */
	/* A fixed authoring scale preserves relative sizes while keeping every ship
	 * within common DCC viewport clip ranges. */
	const float POS_SCALE = 1.0f / 64.0f;
	for (uint32_t v = 0; v < pm->vertex_count; ++v) {
		/* (ex, ey, ez) → (ex, ez, -ey) — see comment above. */
		const float ex = pm->vertices[v].pos[0];
		const float ey = pm->vertices[v].pos[1];
		const float ez = pm->vertices[v].pos[2];
		const float pos[3] = {
			ex * POS_SCALE,
			ez * POS_SCALE,
			-ey * POS_SCALE,
		};
		if (fwrite(pos, sizeof pos, 1, fp) != 1)
			goto fail;
		total += sizeof pos;
	}
	/* NORMAL: float3 per vertex (rebuilt), under the same
	 * axis rotation as positions. Normals are unit-length so no
	 * scaling, just the (x, z, -y) permutation. */
	for (uint32_t v = 0; v < pm->vertex_count; ++v) {
		const float nx = pm->vertices[v].vertex_normal[0];
		const float ny = pm->vertices[v].vertex_normal[1];
		const float nz = pm->vertices[v].vertex_normal[2];
		const float n[3] = { nx, nz, -ny };
		if (fwrite(n, sizeof n, 1, fp) != 1)
			goto fail;
		total += sizeof n;
	}
	/* Indices are grouped into contiguous sub-views. Winding is corrected per
	 * triangle against its outward face normal because the source is not
	 * consistently wound. glTF output is CCW when viewed from outside. */
	for (uint32_t g = 0; g < gt->group_count; ++g) {
		const uint32_t first = gt->groups[g].first_tri;
		const uint32_t count = gt->groups[g].tri_count;
		for (uint32_t k = 0; k < count; ++k) {
			const uint32_t t = gt->tri_perm[first + k];
			const uint16_t i0 = pm->indices[3 * t + 0];
			const uint16_t i1 = pm->indices[3 * t + 1];
			const uint16_t i2 = pm->indices[3 * t + 2];

			const float* a = pm->vertices[i0].pos;
			const float* b = pm->vertices[i1].pos;
			const float* c = pm->vertices[i2].pos;
			const float ux = b[0] - a[0], uy = b[1] - a[1], uz = b[2] - a[2];
			const float vx = c[0] - a[0], vy = c[1] - a[1], vz = c[2] - a[2];
			const float gx = uy * vz - uz * vy;
			const float gy = uz * vx - ux * vz;
			const float gz = ux * vy - uy * vx;

			/* Outward face normal stored on every corner of the
			 * triangle (the converter writes the same fn to all 3). */
			const float* fn = pm->vertices[i0].normal;
			const float d = gx * fn[0] + gy * fn[1] + gz * fn[2];

			uint16_t idx[3];
			idx[0] = i0;
			if (d >= 0.0f) {
				/* cross already on the outward side → keep order. */
				idx[1] = i1;
				idx[2] = i2;
			} else {
				/* cross on the wrong side → flip. */
				idx[1] = i2;
				idx[2] = i1;
			}
			if (fwrite(idx, sizeof idx, 1, fp) != 1)
				goto fail;
			total += sizeof idx;
		}
	}
	fclose(fp);
	*out_bin_size = total;
	return true;
fail:
	fprintf(stderr, "  write %s: %s\n", path, strerror(errno));
	fclose(fp);
	return false;
}

/* ---------------------------------------------------------------------
 * Public export.
 * ------------------------------------------------------------------- */

bool TieLfd2GltfExport_Ship(const TieFlightShipModel* pm, const char* out_dir, const char* ship_name) {
	if (pm->vertex_count == 0 || pm->index_count < 3) {
		fprintf(stderr, "  %s: no triangulated geometry, skipping\n", ship_name);
		return false;
	}

	TieLfd2GltfGroupTable gt = { 0 };
	if (!TieLfd2GltfExport_BuildGroups(pm, &gt)) {
		fprintf(stderr, "  %s: group build OOM\n", ship_name);
		return false;
	}

	/* Output paths. */
	char gltf_path[1024], bin_path[1024], bin_uri[256];
	snprintf(gltf_path, sizeof gltf_path, "%s/%s.gltf", out_dir, ship_name);
	snprintf(bin_path, sizeof bin_path, "%s/%s.bin", out_dir, ship_name);
	snprintf(bin_uri, sizeof bin_uri, "%s.bin", ship_name);

	/* Write .bin first so we know the buffer size. */
	size_t bin_size = 0;
	if (!TieLfd2GltfExport_WriteBin(bin_path, pm, &gt, &bin_size)) {
		TieLfd2GltfExport_FreeGroups(&gt);
		return false;
	}

	/* ---- Build the cgltf_data structure ----
	 *
	 * Counts and ownership map:
	 *
	 *   buffers          1
	 *   buffer_views     2 + group_count       (POSITION, NORMAL, indices per group)
	 *   accessors        2 + group_count       (POSITION, NORMAL, indices per group)
	 *   materials        unique_material_count (one per unique material_id, dedup'd)
	 *   meshes           unique_mesh_count     (one per unique mesh_index)
	 *   nodes            1 + unique_mesh_count (root + per-component child)
	 *   scenes           1
	 *
	 * Per-mesh primitives are sliced from a single `primitives` block
	 * sized `group_count`; cgltf_mesh.primitives points into that
	 * block by index.
	 */

	/* Identify unique mesh_indexes and unique material_ids. */
	uint16_t uniq_mi[256] = { 0 };
	uint32_t n_uniq_mi = 0;
	uint16_t uniq_mat[256] = { 0 };
	uint32_t n_uniq_mat = 0;
	for (uint32_t g = 0; g < gt.group_count; ++g) {
		bool seen_mi = false;
		for (uint32_t i = 0; i < n_uniq_mi; ++i)
			if (uniq_mi[i] == gt.groups[g].mesh_index) {
				seen_mi = true;
				break;
			}
		if (!seen_mi && n_uniq_mi < 256)
			uniq_mi[n_uniq_mi++] = gt.groups[g].mesh_index;

		bool seen_mat = false;
		for (uint32_t i = 0; i < n_uniq_mat; ++i)
			if (uniq_mat[i] == gt.groups[g].material_id) {
				seen_mat = true;
				break;
			}
		if (!seen_mat && n_uniq_mat < 256)
			uniq_mat[n_uniq_mat++] = gt.groups[g].material_id;
	}

	cgltf_data data = { 0 };
	cgltf_buffer buffers[1] = { 0 };
	cgltf_buffer_view* views = (cgltf_buffer_view*)calloc(2 + gt.group_count, sizeof *views);
	cgltf_accessor* accs = (cgltf_accessor*)calloc(2 + gt.group_count, sizeof *accs);
	cgltf_material* mats = (cgltf_material*)calloc(n_uniq_mat, sizeof *mats);
	cgltf_mesh* meshes = (cgltf_mesh*)calloc(n_uniq_mi, sizeof *meshes);
	cgltf_node* nodes = (cgltf_node*)calloc(1 + n_uniq_mi, sizeof *nodes);
	cgltf_node** root_children = (cgltf_node**)calloc(n_uniq_mi, sizeof *root_children);
	cgltf_scene scenes[1] = { 0 };
	cgltf_node** scene_nodes = (cgltf_node**)calloc(1, sizeof *scene_nodes);
	cgltf_primitive* prims = (cgltf_primitive*)calloc(gt.group_count, sizeof *prims);
	/* Per-primitive POSITION + NORMAL attribute records — cgltf wants
	 * them as a contiguous array per primitive, so allocate 2 per. */
	cgltf_attribute* attrs = (cgltf_attribute*)calloc(gt.group_count * 2, sizeof *attrs);

	if (!views || !accs || !mats || !meshes || !nodes || !root_children || !scene_nodes || !prims || !attrs) {
		fprintf(stderr, "  %s: out of memory wiring glTF data\n", ship_name);
		goto cleanup;
	}

	/* ---- Buffer ---- */
	buffers[0].size = bin_size;
	buffers[0].uri = (char*)bin_uri;

	/* ---- Buffer views ---- */
	const size_t pos_bytes = (size_t)pm->vertex_count * 3 * sizeof(float);
	const size_t nor_bytes = (size_t)pm->vertex_count * 3 * sizeof(float);
	views[0] = (cgltf_buffer_view) {
		.buffer = &buffers[0],
		.offset = 0,
		.size = pos_bytes,
		.type = cgltf_buffer_view_type_vertices,
	};
	views[1] = (cgltf_buffer_view) {
		.buffer = &buffers[0],
		.offset = pos_bytes,
		.size = nor_bytes,
		.type = cgltf_buffer_view_type_vertices,
	};
	/* Per-group INDICES views. */
	size_t bv_cursor = pos_bytes + nor_bytes;
	for (uint32_t g = 0; g < gt.group_count; ++g) {
		const size_t idx_bytes = (size_t)gt.groups[g].tri_count * 3 * sizeof(uint16_t);
		views[2 + g] = (cgltf_buffer_view) {
			.buffer = &buffers[0],
			.offset = bv_cursor,
			.size = idx_bytes,
			.type = cgltf_buffer_view_type_indices,
		};
		bv_cursor += idx_bytes;
	}

	/* ---- Accessors ---- */
	/* Compute pos min-max for the POSITION accessor (glTF spec
	 * requires it). Use the SAME scale + axis rotation we applied
	 * when writing positions to the .bin so accessor metadata
	 * matches data. */
	const float pos_scale_for_minmax = 1.0f / 64.0f;
	float pmin[3] = { 1e30f, 1e30f, 1e30f };
	float pmax[3] = { -1e30f, -1e30f, -1e30f };
	for (uint32_t v = 0; v < pm->vertex_count; ++v) {
		const float ex = pm->vertices[v].pos[0];
		const float ey = pm->vertices[v].pos[1];
		const float ez = pm->vertices[v].pos[2];
		const float p[3] = {
			ex * pos_scale_for_minmax,
			ez * pos_scale_for_minmax,
			-ey * pos_scale_for_minmax,
		};
		for (int k = 0; k < 3; ++k) {
			if (p[k] < pmin[k])
				pmin[k] = p[k];
			if (p[k] > pmax[k])
				pmax[k] = p[k];
		}
	}
	accs[0] = (cgltf_accessor) {
		.component_type = cgltf_component_type_r_32f,
		.type = cgltf_type_vec3,
		.count = pm->vertex_count,
		.buffer_view = &views[0],
		.has_min = 1,
		.has_max = 1,
	};
	memcpy(accs[0].min, pmin, sizeof pmin);
	memcpy(accs[0].max, pmax, sizeof pmax);

	accs[1] = (cgltf_accessor) {
		.component_type = cgltf_component_type_r_32f,
		.type = cgltf_type_vec3,
		.count = pm->vertex_count,
		.buffer_view = &views[1],
	};
	for (uint32_t g = 0; g < gt.group_count; ++g) {
		accs[2 + g] = (cgltf_accessor) {
			.component_type = cgltf_component_type_r_16u,
			.type = cgltf_type_scalar,
			.count = (cgltf_size)gt.groups[g].tri_count * 3,
			.buffer_view = &views[2 + g],
		};
	}

	/* ---- Materials ---- */
	/* Buffer for material names — cgltf treats them as plain `char *`
	 * (not strdup'd). One static buffer per material is fine since
	 * the data has the same lifetime. */
	static char mat_name_storage[256][32];
	for (uint32_t i = 0; i < n_uniq_mat; ++i) {
		const uint16_t mid = uniq_mat[i];
		float rgb[3];
		TieLfd2GltfExport_MaterialColorForId(mid, rgb);
		snprintf(mat_name_storage[i], sizeof mat_name_storage[i], "mat_%u", (unsigned)mid);
		mats[i].name = mat_name_storage[i];
		mats[i].has_pbr_metallic_roughness = 1;
		mats[i].pbr_metallic_roughness.base_color_factor[0] = rgb[0];
		mats[i].pbr_metallic_roughness.base_color_factor[1] = rgb[1];
		mats[i].pbr_metallic_roughness.base_color_factor[2] = rgb[2];
		mats[i].pbr_metallic_roughness.base_color_factor[3] = 1.0f;
		mats[i].pbr_metallic_roughness.metallic_factor = 0.0f;
		mats[i].pbr_metallic_roughness.roughness_factor = 1.0f;
		mats[i].double_sided = 1; /* engine renders both sides */
	}

	/* ---- Primitives + attributes ---- */
	/* Per-primitive 2 attributes: POSITION + NORMAL. Each cgltf_primitive
	 * gets its own attributes pointer that aliases into the shared
	 * `attrs` array. */
	static char attr_pos_name[] = "POSITION";
	static char attr_nor_name[] = "NORMAL";
	for (uint32_t g = 0; g < gt.group_count; ++g) {
		attrs[2 * g + 0].name = attr_pos_name;
		attrs[2 * g + 0].type = cgltf_attribute_type_position;
		attrs[2 * g + 0].index = 0;
		attrs[2 * g + 0].data = &accs[0];
		attrs[2 * g + 1].name = attr_nor_name;
		attrs[2 * g + 1].type = cgltf_attribute_type_normal;
		attrs[2 * g + 1].index = 0;
		attrs[2 * g + 1].data = &accs[1];

		/* Locate the material index for this group's material_id. */
		cgltf_material* mat = NULL;
		for (uint32_t i = 0; i < n_uniq_mat; ++i) {
			if (uniq_mat[i] == gt.groups[g].material_id) {
				mat = &mats[i];
				break;
			}
		}

		prims[g] = (cgltf_primitive) {
			.type = cgltf_primitive_type_triangles,
			.indices = &accs[2 + g],
			.material = mat,
			.attributes = &attrs[2 * g],
			.attributes_count = 2,
		};
	}

	/* ---- Meshes (one per unique mesh_index) ---- */
	static char mesh_name_storage[256][32];
	for (uint32_t i = 0; i < n_uniq_mi; ++i) {
		const uint16_t mi = uniq_mi[i];
		const uint8_t mt = (mi < pm->mesh_count && pm->mesh_rot) ? pm->mesh_rot[mi].mesh_type : 0;
		const char* TieFilmExtract_TypeName = TieLfd2GltfExport_MeshTypeName(mt);
		if (TieFilmExtract_TypeName)
			snprintf(mesh_name_storage[i], sizeof mesh_name_storage[i], "mesh_%u_%s", (unsigned)mi,
					 TieFilmExtract_TypeName);
		else
			snprintf(mesh_name_storage[i], sizeof mesh_name_storage[i], "mesh_%u_type%u", (unsigned)mi,
					 (unsigned)mt);
		meshes[i].name = mesh_name_storage[i];

		/* Count primitives belonging to this mesh_index. */
		uint32_t prim_count = 0;
		for (uint32_t g = 0; g < gt.group_count; ++g)
			if (gt.groups[g].mesh_index == mi)
				++prim_count;
		meshes[i].primitives_count = prim_count;
		/* primitives must be a contiguous block — copy out of `prims`. */
		meshes[i].primitives = (cgltf_primitive*)calloc(prim_count, sizeof(cgltf_primitive));
		if (!meshes[i].primitives) {
			fprintf(stderr, "  %s: OOM allocating mesh primitives\n", ship_name);
			goto cleanup;
		}
		uint32_t fill = 0;
		for (uint32_t g = 0; g < gt.group_count; ++g) {
			if (gt.groups[g].mesh_index == mi)
				meshes[i].primitives[fill++] = prims[g];
		}
	}

	/* ---- Nodes ---- */
	static char ship_root_name[64];
	snprintf(ship_root_name, sizeof ship_root_name, "%s", ship_name);
	nodes[0].name = ship_root_name;
	nodes[0].children = root_children;
	nodes[0].children_count = n_uniq_mi;
	for (uint32_t i = 0; i < n_uniq_mi; ++i) {
		nodes[1 + i].name = meshes[i].name;
		nodes[1 + i].mesh = &meshes[i];
		root_children[i] = &nodes[1 + i];
	}

	/* ---- Scene ---- */
	scene_nodes[0] = &nodes[0];
	scenes[0].nodes = scene_nodes;
	scenes[0].nodes_count = 1;

	/* ---- Asset metadata ---- */
	static char asset_generator[] = "tie/lfd2gltf";
	static char asset_version[] = "2.0";
	data.asset.generator = asset_generator;
	data.asset.version = asset_version;

	/* ---- Wire counts into cgltf_data ---- */
	data.buffers = buffers;
	data.buffers_count = 1;
	data.buffer_views = views;
	data.buffer_views_count = 2 + gt.group_count;
	data.accessors = accs;
	data.accessors_count = 2 + gt.group_count;
	data.materials = mats;
	data.materials_count = n_uniq_mat;
	data.meshes = meshes;
	data.meshes_count = n_uniq_mi;
	data.nodes = nodes;
	data.nodes_count = 1 + n_uniq_mi;
	data.scenes = scenes;
	data.scenes_count = 1;
	data.scene = &scenes[0];

	/* ---- Write glTF JSON ---- */
	cgltf_options opts = { 0 };
	cgltf_result wr = cgltf_write_file(&opts, gltf_path, &data);
	bool ok = (wr == cgltf_result_success);
	if (!ok) {
		fprintf(stderr, "  %s: cgltf_write_file failed (%d)\n", ship_name, (int)wr);
	}

cleanup:
	/* Free the per-mesh primitives blocks we allocated above. */
	for (uint32_t i = 0; i < n_uniq_mi; ++i)
		free(meshes[i].primitives);

	free(views);
	free(accs);
	free(mats);
	free(meshes);
	free(nodes);
	free(root_children);
	free(scene_nodes);
	free(prims);
	free(attrs);
	TieLfd2GltfExport_FreeGroups(&gt);
	return true;
}
