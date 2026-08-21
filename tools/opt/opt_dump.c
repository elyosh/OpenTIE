/*
 * opt_dump - CLI for the C OPT parser. Default mode mirrors opt_info.py:
 * counts, overall bbox, rotating components, hardpoint list. With -v, dumps
 * every mesh's geometry (vertices, normals, UVs, descriptor, rotation,
 * LODs/face-groups with sample faces) and every texture's header info.
 */

#include "opt.h"

#include <stdio.h>
#include <string.h>

static void TieOptDump_PrintVec3(const opt_vec3_t* v) { printf("(%.3f, %.3f, %.3f)", v->x, v->y, v->z); }

static void TieOptDump_PrintVec2(const opt_vec2_t* v) { printf("(%.3f, %.3f)", v->u, v->v); }

static void TieOptDump_VerboseMesh(int index, const opt_mesh_t* m) {
	const char* type = m->has_descriptor ? opt_mesh_type_name(m->descriptor.mesh_type) : "<no-desc>";
	printf("  mesh[%d] %s\n", index, type);

	if (m->has_descriptor) {
		const opt_mesh_descriptor_t* d = &m->descriptor;
		printf("    descriptor: explosionType=%d\n", d->explosion_type);
		printf("      span=");
		TieOptDump_PrintVec3(&d->span);
		printf("\n");
		printf("      center=");
		TieOptDump_PrintVec3(&d->center);
		printf("\n");
		printf("      bbox=");
		TieOptDump_PrintVec3(&d->bbox_min);
		printf(" .. ");
		TieOptDump_PrintVec3(&d->bbox_max);
		printf("\n");
		if (d->target_id != 0 || d->target.x != 0.0f || d->target.y != 0.0f || d->target.z != 0.0f) {
			printf("      target_id=%d  target=", d->target_id);
			TieOptDump_PrintVec3(&d->target);
			printf("\n");
		}
	}
	if (m->has_rotation_scale) {
		const opt_rotation_scale_t* r = &m->rotation_scale;
		printf("    rotation_scale: %s\n", opt_rotation_scale_is_identity(r) ? "identity" : "ROTATING");
		printf("      pivot=");
		TieOptDump_PrintVec3(&r->pivot);
		printf("\n");
		printf("      rot_axis=");
		TieOptDump_PrintVec3(&r->rotation_axis);
		printf("\n");
		printf("      dir_axis=");
		TieOptDump_PrintVec3(&r->direction_axis);
		printf("\n");
		printf("      up_axis=");
		TieOptDump_PrintVec3(&r->up_axis);
		printf("\n");
	}

	printf("    vertices: %d", m->vertex_count);
	if (m->vertex_count > 0) {
		printf("  v0=");
		TieOptDump_PrintVec3(&m->vertices[0]);
	}
	printf("\n");
	printf("    normals: %d", m->normal_count);
	if (m->normal_count > 0) {
		printf("  n0=");
		TieOptDump_PrintVec3(&m->normals[0]);
	}
	printf("\n");
	printf("    uvs: %d", m->uv_count);
	if (m->uv_count > 0) {
		printf("  uv0=");
		TieOptDump_PrintVec2(&m->uvs[0]);
	}
	printf("\n");

	for (int j = 0; j < m->hardpoint_count; j++) {
		const opt_hardpoint_t* hp = &m->hardpoints[j];
		printf("    hardpoint[%d] %s pos=", j, opt_hardpoint_type_name(hp->type));
		TieOptDump_PrintVec3(&hp->pos);
		printf("\n");
	}

	for (int j = 0; j < m->lod_count; j++) {
		const opt_lod_t* lod = &m->lods[j];
		printf("    lod[%d] dist=%.6f  groups=%d\n", j, lod->distance_threshold, lod->group_count);
		for (int k = 0; k < lod->group_count; k++) {
			const opt_face_group_t* g = &lod->groups[k];
			printf("      group[%d] tex=%d  faces=%d  edges=%d", k, g->texture_index, g->face_count,
				   g->edges_count);
			if (g->state_count > 1 && g->state_textures) {
				printf("  states=[%d", g->state_textures[0]);
				for (int s = 1; s < g->state_count; s++)
					printf(",%d", g->state_textures[s]);
				printf("]");
			}
			if (g->face_count > 0) {
				const opt_face_t* f0 = &g->faces[0];
				printf("\n        face[0] verts={%d,%d,%d,%d}"
					   " uvs={%d,%d,%d,%d} normals={%d,%d,%d,%d}",
					   f0->verts[0], f0->verts[1], f0->verts[2], f0->verts[3], f0->uvs[0], f0->uvs[1],
					   f0->uvs[2], f0->uvs[3], f0->normals[0], f0->normals[1], f0->normals[2],
					   f0->normals[3]);
			}
			printf("\n");
		}
	}
}

static void TieOptDump_VerboseTextures(const opt_file_t* opt) {
	printf("  textures:\n");
	for (int i = 0; i < opt->texture_count; i++) {
		const opt_texture_t* t = &opt->textures[i];
		printf("    [%2d] %-12s  %dx%d  mips=%d  chain=%d bytes\n", i, t->name, t->width, t->height,
			   t->mip_count, t->mip_chain_bytes);
	}
}

static void TieOptDump_Summary(const char* path, int verbose) {
	opt_error_t err;
	opt_file_t* opt = opt_load_file(path, &err);
	if (!opt) {
		fprintf(stderr, "%s: load failed: %s\n", path, err.msg);
		return;
	}

	printf("\n=== %s ===\n", path);
	printf("  version=%d  meshes=%d  textures=%d\n", opt->version, opt->mesh_count, opt->texture_count);

	/* Counts of node-like things derived from the parsed model. */
	int faces_total = 0, vertices_total = 0, hardpoints_total = 0, lods_total = 0;
	int has_bbox = 0;
	opt_vec3_t bbox_min = { 0, 0, 0 }, bbox_max = { 0, 0, 0 };

	for (int i = 0; i < opt->mesh_count; i++) {
		const opt_mesh_t* m = &opt->meshes[i];
		vertices_total += m->vertex_count;
		hardpoints_total += m->hardpoint_count;
		lods_total += m->lod_count;
		for (int j = 0; j < m->lod_count; j++) {
			for (int k = 0; k < m->lods[j].group_count; k++) {
				faces_total += m->lods[j].groups[k].face_count;
			}
		}
		if (m->has_descriptor) {
			if (!has_bbox) {
				bbox_min = m->descriptor.bbox_min;
				bbox_max = m->descriptor.bbox_max;
				has_bbox = 1;
			} else {
#define MN(a, b) ((a) < (b) ? (a) : (b))
#define MX(a, b) ((a) > (b) ? (a) : (b))
				bbox_min.x = MN(bbox_min.x, m->descriptor.bbox_min.x);
				bbox_min.y = MN(bbox_min.y, m->descriptor.bbox_min.y);
				bbox_min.z = MN(bbox_min.z, m->descriptor.bbox_min.z);
				bbox_max.x = MX(bbox_max.x, m->descriptor.bbox_max.x);
				bbox_max.y = MX(bbox_max.y, m->descriptor.bbox_max.y);
				bbox_max.z = MX(bbox_max.z, m->descriptor.bbox_max.z);
#undef MN
#undef MX
			}
		}
	}

	printf("  totals: vertices=%d  faces=%d  hardpoints=%d  lods=%d\n", vertices_total, faces_total,
		   hardpoints_total, lods_total);
	if (has_bbox) {
		printf("  overall bbox: min=");
		TieOptDump_PrintVec3(&bbox_min);
		printf(" max=");
		TieOptDump_PrintVec3(&bbox_max);
		printf("\n");
	}

	/* Per-mesh inventory. */
	printf("  meshes:\n");
	for (int i = 0; i < opt->mesh_count; i++) {
		const opt_mesh_t* m = &opt->meshes[i];
		const char* type = m->has_descriptor ? opt_mesh_type_name(m->descriptor.mesh_type) : "<no-desc>";
		int faces = 0;
		for (int j = 0; j < m->lod_count; j++) {
			for (int k = 0; k < m->lods[j].group_count; k++) {
				faces += m->lods[j].groups[k].face_count;
			}
		}
		printf("    [%d] type=%-18s verts=%-4d normals=%-4d uvs=%-4d "
			   "hardpoints=%d lods=%d faces=%d\n",
			   i, type, m->vertex_count, m->normal_count, m->uv_count, m->hardpoint_count, m->lod_count,
			   faces);
	}

	/* Rotating components. */
	int rotating = 0;
	for (int i = 0; i < opt->mesh_count; i++) {
		const opt_mesh_t* m = &opt->meshes[i];
		int is_rotary = m->has_descriptor && opt_mesh_type_is_rotary(m->descriptor.mesh_type);
		int has_rot = m->has_rotation_scale && !opt_rotation_scale_is_identity(&m->rotation_scale);
		if (is_rotary || has_rot)
			rotating++;
	}
	if (rotating > 0) {
		printf("  rotating components: %d\n", rotating);
		for (int i = 0; i < opt->mesh_count; i++) {
			const opt_mesh_t* m = &opt->meshes[i];
			int is_rotary = m->has_descriptor && opt_mesh_type_is_rotary(m->descriptor.mesh_type);
			int has_rot = m->has_rotation_scale && !opt_rotation_scale_is_identity(&m->rotation_scale);
			if (!is_rotary && !has_rot)
				continue;
			const char* type = m->has_descriptor ? opt_mesh_type_name(m->descriptor.mesh_type) : "<no-desc>";
			printf("    mesh[%d] type=%s", i, type);
			if (m->has_rotation_scale) {
				printf("  pivot=");
				TieOptDump_PrintVec3(&m->rotation_scale.pivot);
				printf(" axis=");
				TieOptDump_PrintVec3(&m->rotation_scale.rotation_axis);
				printf(" dir=");
				TieOptDump_PrintVec3(&m->rotation_scale.direction_axis);
			}
			printf("\n");
		}
	}

	/* Hardpoints, listed for the whole ship. */
	if (hardpoints_total > 0) {
		printf("  hardpoints:\n");
		for (int i = 0; i < opt->mesh_count; i++) {
			const opt_mesh_t* m = &opt->meshes[i];
			for (int j = 0; j < m->hardpoint_count; j++) {
				const opt_hardpoint_t* hp = &m->hardpoints[j];
				printf("    mesh[%d] %-16s pos=", i, opt_hardpoint_type_name(hp->type));
				TieOptDump_PrintVec3(&hp->pos);
				printf("\n");
			}
		}
	}

	if (verbose) {
		TieOptDump_VerboseTextures(opt);
		for (int i = 0; i < opt->mesh_count; i++)
			TieOptDump_VerboseMesh(i, &opt->meshes[i]);
	}

	opt_free(opt);
}

int main(int argc, char** argv) {
	int verbose = 0;
	int first_path = 1;
	if (argc >= 2 && (strcmp(argv[1], "-v") == 0 || strcmp(argv[1], "--verbose") == 0)) {
		verbose = 1;
		first_path = 2;
	}
	if (argc <= first_path) {
		fprintf(stderr, "usage: %s [-v] <file.opt> [<file.opt> ...]\n", argv[0]);
		return 1;
	}
	for (int i = first_path; i < argc; i++)
		TieOptDump_Summary(argv[i], verbose);
	return 0;
}
