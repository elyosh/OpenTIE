/*
 * Compare the TIE95 ShipModelData used by the simulation with the TIE98 OPT
 * selected for the same species. The report focuses on the contracts needed
 * to drive an OPT from recovered TIE95 state: scale, bounds, component slots,
 * component types, rotation-frame metadata, and geometric vertex coverage.
 */

#include <errno.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "aeron/vfs.h"
#include "lfd_file.h"
#include "opt.h"
#include "tie/tie.h"
#include "tie_runtime/audio/config.h"
#include "tie_runtime/diagnostics/diagnostics.h"
#include "tie_runtime/display/classic_display.h"
#include "tie_runtime/display/classic_framebuffer.h"
#include "tie_runtime/flight_assets/assets.h"
#include "tie_runtime/flight_assets/model_types.h"
#include "tie_runtime/flight_assets/ship_model_converter.h"
#include "tie_runtime/input/input.h"
#include "tie_runtime/runtime/exports.h"
#include "tie_runtime/runtime/profile.h"
#include "tie_runtime/storage/storage.h"

#ifndef TIE_MODEL_COMPAT_DEFAULT_CATALOG_ROOT
#define TIE_MODEL_COMPAT_DEFAULT_CATALOG_ROOT "resources"
#endif

#define MODEL_SPECIES_LIMIT 100
#define PATH_CAPACITY 1024

typedef struct TieModelCompatOptions {
	const char* tie95_root;
	const char* tie98_root;
	const char* resource_set;
	const char* catalog_root;
	double tolerance;
	int species_filter;
	bool verbose;
} TieModelCompatOptions;

typedef struct TieModelCompatPoint {
	double axis[3];
} TieModelCompatPoint;

typedef struct TieModelCompatPointSet {
	TieModelCompatPoint* points;
	size_t count;
	double bound_min[3];
	double bound_max[3];
} TieModelCompatPointSet;

typedef struct TieModelCompatGeometryComparison {
	const char* bounds_class;
	double max_edge_delta;
	double max_size_error_percent;
	double center_error_percent;
	double classic_coverage_percent;
	double opt_coverage_percent;
	bool vertices_equal;
} TieModelCompatGeometryComparison;

typedef struct TieModelCompatLayoutComparison {
	unsigned invalid_mappings;
	unsigned type_mismatches;
	unsigned classic_rotation_blocks;
	unsigned opt_rotation_frames;
	unsigned opt_nondefault_frames;
	unsigned opt_rotary_meshes;
	unsigned missing_opt_rotation_frames;
	unsigned unmapped_opt_meshes;
	unsigned duplicated_opt_meshes;
	bool identity;
	bool valid;
} TieModelCompatLayoutComparison;

typedef struct TieModelCompatTotals {
	unsigned compared;
	unsigned direct;
	unsigned review;
	unsigned no_opt;
	unsigned errors;
} TieModelCompatTotals;

static void TieModelCompat_Usage(FILE* stream, const char* program) {
	fprintf(stream,
			"Usage: %s [options] <tie95-root> <tie98-root>\n"
			"\n"
			"options:\n"
			"  --resource-set NAME  LFD directory under tie95-root (default: RESOURCE)\n"
			"  --catalog-root PATH  directory containing flight/tie98-models.yaml\n"
			"  --tolerance UNITS    vertex and exact-bound tolerance (default: 1)\n"
			"  --species ID         compare one species only\n"
			"  --verbose            print bounds and component mismatches\n"
			"  --help               show this help\n",
			program);
}

static bool TieModelCompat_ParsePositiveDouble(const char* text, double* value) {
	char* end = NULL;
	errno = 0;
	double parsed = strtod(text, &end);
	if (errno || end == text || *end || !isfinite(parsed) || parsed <= 0.0)
		return false;
	*value = parsed;
	return true;
}

static bool TieModelCompat_ParseOptions(int argc, char** argv, TieModelCompatOptions* options) {
	*options = (TieModelCompatOptions) {
		.resource_set = "RESOURCE",
		.catalog_root = TIE_MODEL_COMPAT_DEFAULT_CATALOG_ROOT,
		.tolerance = 1.0,
		.species_filter = -1,
	};

	int positional = 0;
	for (int index = 1; index < argc; ++index) {
		const char* argument = argv[index];
		if (strcmp(argument, "--help") == 0) {
			TieModelCompat_Usage(stdout, argv[0]);
			exit(0);
		}
		if (strcmp(argument, "--verbose") == 0) {
			options->verbose = true;
			continue;
		}
		if (strcmp(argument, "--resource-set") == 0 || strcmp(argument, "--catalog-root") == 0 ||
			strcmp(argument, "--tolerance") == 0 || strcmp(argument, "--species") == 0) {
			if (++index >= argc) {
				fprintf(stderr, "%s requires a value\n", argument);
				return false;
			}
			if (strcmp(argument, "--resource-set") == 0)
				options->resource_set = argv[index];
			else if (strcmp(argument, "--catalog-root") == 0)
				options->catalog_root = argv[index];
			else if (strcmp(argument, "--species") == 0) {
				char* end = NULL;
				long species = strtol(argv[index], &end, 10);
				if (end == argv[index] || *end || species < 0 || species >= MODEL_SPECIES_LIMIT) {
					fprintf(stderr, "invalid species: %s\n", argv[index]);
					return false;
				}
				options->species_filter = (int)species;
			} else if (!TieModelCompat_ParsePositiveDouble(argv[index], &options->tolerance)) {
				fprintf(stderr, "invalid tolerance: %s\n", argv[index]);
				return false;
			}
			continue;
		}
		if (argument[0] == '-') {
			fprintf(stderr, "unknown option: %s\n", argument);
			return false;
		}
		if (positional == 0)
			options->tie95_root = argument;
		else if (positional == 1)
			options->tie98_root = argument;
		else {
			fprintf(stderr, "unexpected argument: %s\n", argument);
			return false;
		}
		++positional;
	}
	return positional == 2;
}

static bool TieModelCompat_JoinPath(char output[PATH_CAPACITY], const char* left, const char* middle,
									const char* right) {
	int length = middle ? snprintf(output, PATH_CAPACITY, "%s/%s/%s", left, middle, right)
						: snprintf(output, PATH_CAPACITY, "%s/%s", left, right);
	return length >= 0 && length < PATH_CAPACITY;
}

static uint32_t TieModelCompat_Fourcc(const char text[4]) {
	return ((uint32_t)(uint8_t)text[0] << 24) | ((uint32_t)(uint8_t)text[1] << 16) |
		   ((uint32_t)(uint8_t)text[2] << 8) | (uint32_t)(uint8_t)text[3];
}

static bool TieModelCompat_OpenSpeciesLfds(const TieModelCompatOptions* options, TieLfdFile lfds[3]) {
	static const char* const names[3] = { "SPECIES.LFD", "SPECIES2.LFD", "SPECIES3.LFD" };
	char path[PATH_CAPACITY];
	char error[512];
	for (int index = 0; index < 3; ++index) {
		if (!TieModelCompat_JoinPath(path, options->tie95_root, options->resource_set, names[index])) {
			fprintf(stderr, "TIE95 LFD path is too long\n");
			return false;
		}
		if (!TieLfdFile_Open(&lfds[index], path, error, sizeof error)) {
			fprintf(stderr, "%s\n", error);
			for (int preceding = 0; preceding < index; ++preceding)
				TieLfdFile_Close(&lfds[preceding]);
			return false;
		}
	}
	return true;
}

static TieFlightAssetBundle* TieModelCompat_OpenCatalog(const TieModelCompatOptions* options,
														AeronVfs** out_vfs) {
	AeronVfsConfig config = {
		.org_name = "OpenTIE",
		.app_name = "tie_model_compat",
		.asset_root = options->catalog_root,
		.resource_root = options->catalog_root,
		.user_root = "/tmp",
		.temp_root = "/tmp",
	};
	AeronVfs* vfs = AeronVfs_Create(&config);
	if (!vfs) {
		fprintf(stderr, "could not create VFS for catalog root %s\n", options->catalog_root);
		return NULL;
	}
	char error[512];
	TieFlightAssetBundle* catalog =
		TieFlightAssets_Open(vfs, TIE_FLIGHT_ASSET_CATALOG_TIE98, error, sizeof error);
	if (!catalog) {
		fprintf(stderr, "%s\n", error);
		AeronVfs_Destroy(vfs);
		return NULL;
	}
	*out_vfs = vfs;
	return catalog;
}

static int TieModelCompat_CompareModelPoints(const void* left, const void* right) {
	const TieModelCompatPoint* a = left;
	const TieModelCompatPoint* b = right;
	for (int axis = 0; axis < 3; ++axis) {
		if (a->axis[axis] < b->axis[axis])
			return -1;
		if (a->axis[axis] > b->axis[axis])
			return 1;
	}
	return 0;
}

static void TieModelCompat_FinishPointSet(TieModelCompatPointSet* set) {
	qsort(set->points, set->count, sizeof *set->points, TieModelCompat_CompareModelPoints);
	if (!set->count)
		return;
	size_t unique = 1;
	for (size_t index = 1; index < set->count; ++index) {
		if (TieModelCompat_CompareModelPoints(&set->points[index], &set->points[unique - 1]) != 0)
			set->points[unique++] = set->points[index];
	}
	set->count = unique;
}

static void TieModelCompat_AddPoint(TieModelCompatPointSet* set, size_t* next, double x, double y, double z) {
	const double values[3] = { x, y, z };
	for (int axis = 0; axis < 3; ++axis) {
		if (values[axis] < set->bound_min[axis])
			set->bound_min[axis] = values[axis];
		if (values[axis] > set->bound_max[axis])
			set->bound_max[axis] = values[axis];
		set->points[*next].axis[axis] = values[axis];
	}
	++*next;
}

static bool TieModelCompat_BuildClassicPoints(const TieFlightShipModel* model, TieModelCompatPointSet* set) {
	const size_t capacity = (size_t)model->vertex_count + (size_t)model->line_vertex_count * 2;
	if (!capacity)
		return false;
	*set = (TieModelCompatPointSet) {
		.points = malloc(capacity * sizeof *set->points),
		.bound_min = { INFINITY, INFINITY, INFINITY },
		.bound_max = { -INFINITY, -INFINITY, -INFINITY },
	};
	if (!set->points)
		return false;
	const double scale = model->model_scale_shift == 2 ? 4.0 : 1.0;
	size_t next = 0;
	for (uint32_t index = 0; index < model->vertex_count; ++index) {
		const float* position = model->vertices[index].pos;
		TieModelCompat_AddPoint(set, &next, position[0] * scale, position[1] * scale, position[2] * scale);
	}
	for (uint32_t index = 0; index < model->line_vertex_count; ++index) {
		const TieFlightLineVertex* line = &model->line_vertices[index];
		TieModelCompat_AddPoint(set, &next, line->pos_a[0] * scale, line->pos_a[1] * scale,
								line->pos_a[2] * scale);
		TieModelCompat_AddPoint(set, &next, line->pos_b[0] * scale, line->pos_b[1] * scale,
								line->pos_b[2] * scale);
	}
	set->count = next;
	TieModelCompat_FinishPointSet(set);
	return true;
}

static bool TieModelCompat_BuildOptPoints(const opt_file_t* opt, double scale, TieModelCompatPointSet* set) {
	size_t capacity = 0;
	for (int32_t mesh = 0; mesh < opt->mesh_count; ++mesh)
		capacity += (size_t)opt->meshes[mesh].vertex_count;
	if (!capacity)
		return false;
	*set = (TieModelCompatPointSet) {
		.points = malloc(capacity * sizeof *set->points),
		.bound_min = { INFINITY, INFINITY, INFINITY },
		.bound_max = { -INFINITY, -INFINITY, -INFINITY },
	};
	if (!set->points)
		return false;
	size_t next = 0;
	for (int32_t mesh = 0; mesh < opt->mesh_count; ++mesh) {
		const opt_mesh_t* source = &opt->meshes[mesh];
		for (int32_t vertex = 0; vertex < source->vertex_count; ++vertex) {
			const opt_vec3_t* position = &source->vertices[vertex];
			TieModelCompat_AddPoint(set, &next, position->x * scale, position->y * scale,
									position->z * scale);
		}
	}
	set->count = next;
	TieModelCompat_FinishPointSet(set);
	return true;
}

static size_t TieModelCompat_PointCoverageCount(const TieModelCompatPointSet* source,
												const TieModelCompatPointSet* target, double tolerance) {
	const double tolerance_squared = tolerance * tolerance;
	size_t covered = 0;
	for (size_t source_index = 0; source_index < source->count; ++source_index) {
		for (size_t target_index = 0; target_index < target->count; ++target_index) {
			double distance_squared = 0.0;
			for (int axis = 0; axis < 3; ++axis) {
				double delta =
					source->points[source_index].axis[axis] - target->points[target_index].axis[axis];
				distance_squared += delta * delta;
			}
			if (distance_squared <= tolerance_squared) {
				++covered;
				break;
			}
		}
	}
	return covered;
}

static TieModelCompatGeometryComparison TieModelCompat_CompareGeometry(const TieModelCompatPointSet* classic,
																	   const TieModelCompatPointSet* opt,
																	   double tolerance) {
	TieModelCompatGeometryComparison result = { 0 };
	double largest_classic_size = 1.0;
	double center_distance_squared = 0.0;
	for (int axis = 0; axis < 3; ++axis) {
		double classic_size = classic->bound_max[axis] - classic->bound_min[axis];
		double opt_size = opt->bound_max[axis] - opt->bound_min[axis];
		double denominator = fmax(fabs(classic_size), 1.0);
		double size_error = fabs(classic_size - opt_size) / denominator * 100.0;
		if (size_error > result.max_size_error_percent)
			result.max_size_error_percent = size_error;
		if (classic_size > largest_classic_size)
			largest_classic_size = classic_size;

		double classic_center = (classic->bound_min[axis] + classic->bound_max[axis]) * 0.5;
		double opt_center = (opt->bound_min[axis] + opt->bound_max[axis]) * 0.5;
		double center_delta = classic_center - opt_center;
		center_distance_squared += center_delta * center_delta;

		double minimum_delta = fabs(classic->bound_min[axis] - opt->bound_min[axis]);
		double maximum_delta = fabs(classic->bound_max[axis] - opt->bound_max[axis]);
		result.max_edge_delta = fmax(result.max_edge_delta, fmax(minimum_delta, maximum_delta));
	}
	result.center_error_percent = sqrt(center_distance_squared) / largest_classic_size * 100.0;
	if (result.max_edge_delta <= tolerance)
		result.bounds_class = "exact";
	else if (result.max_size_error_percent <= 1.0 && result.center_error_percent <= 1.0)
		result.bounds_class = "near";
	else
		result.bounds_class = "different";
	size_t classic_covered = TieModelCompat_PointCoverageCount(classic, opt, tolerance);
	size_t opt_covered = TieModelCompat_PointCoverageCount(opt, classic, tolerance);
	result.classic_coverage_percent =
		classic->count ? (double)classic_covered * 100.0 / (double)classic->count : 0.0;
	result.opt_coverage_percent = opt->count ? (double)opt_covered * 100.0 / (double)opt->count : 0.0;
	result.vertices_equal =
		classic->count == opt->count && classic_covered == classic->count && opt_covered == opt->count;
	return result;
}

static unsigned TieModelCompat_ClassicHardpointCount(const ShipModelData* model) {
	const ShipModelMesh* meshes = (const ShipModelMesh*)&model->lod_records[model->num_lods];
	unsigned count = 0;
	for (uint8_t index = 0; index < model->num_meshes; ++index)
		count += meshes[index].num_hardpoints;
	return count;
}

static unsigned TieModelCompat_OptHardpointCount(const opt_file_t* opt) {
	unsigned count = 0;
	for (int32_t index = 0; index < opt->mesh_count; ++index)
		count += (unsigned)opt->meshes[index].hardpoint_count;
	return count;
}

static bool TieModelCompat_OptMeshHasNondefaultFrame(const opt_mesh_t* mesh) {
	return mesh->has_rotation_scale && !opt_rotation_scale_is_identity(&mesh->rotation_scale);
}

static TieModelCompatLayoutComparison TieModelCompat_CompareLayout(const ShipModelData* classic,
																   const opt_file_t* opt) {
	TieModelCompatLayoutComparison result = { .identity = true };
	const ShipModelMesh* classic_meshes = (const ShipModelMesh*)&classic->lod_records[classic->num_lods];
	unsigned* uses = calloc((size_t)opt->mesh_count, sizeof *uses);
	if (!uses) {
		result.invalid_mappings = 1;
		return result;
	}
	for (uint8_t source = 0; source < classic->num_meshes; ++source) {
		uint8_t destination = source;
		bool classic_rotates = classic_meshes[source].rotation_offset != 0;
		if (classic_rotates)
			++result.classic_rotation_blocks;
		if (destination != source)
			result.identity = false;
		if (destination >= opt->mesh_count) {
			++result.invalid_mappings;
			if (classic_rotates)
				++result.missing_opt_rotation_frames;
			continue;
		}
		++uses[destination];
		const opt_mesh_t* opt_mesh = &opt->meshes[destination];
		if (!opt_mesh->has_descriptor || classic_meshes[source].mesh_type != opt_mesh->descriptor.mesh_type)
			++result.type_mismatches;
		if (classic_rotates && !opt_mesh->has_rotation_scale)
			++result.missing_opt_rotation_frames;
	}
	for (int32_t destination = 0; destination < opt->mesh_count; ++destination) {
		const opt_mesh_t* mesh = &opt->meshes[destination];
		if (!uses[destination])
			++result.unmapped_opt_meshes;
		if (uses[destination] > 1)
			++result.duplicated_opt_meshes;
		if (mesh->has_rotation_scale)
			++result.opt_rotation_frames;
		if (TieModelCompat_OptMeshHasNondefaultFrame(mesh))
			++result.opt_nondefault_frames;
		if (mesh->has_descriptor && opt_mesh_type_is_rotary(mesh->descriptor.mesh_type))
			++result.opt_rotary_meshes;
	}
	free(uses);
	if (classic->num_meshes != opt->mesh_count)
		result.identity = false;
	result.valid = result.invalid_mappings == 0 && result.type_mismatches == 0 &&
				   result.missing_opt_rotation_frames == 0 && result.unmapped_opt_meshes == 0;
	return result;
}

static void TieModelCompat_PrintVerboseDetails(const TieModelCompatPointSet* classic_points,
											   const TieModelCompatPointSet* opt_points,
											   const ShipModelData* classic, const opt_file_t* opt) {
	printf("      classic bounds: [%9.1f %9.1f %9.1f] .. "
		   "[%9.1f %9.1f %9.1f]\n",
		   classic_points->bound_min[0], classic_points->bound_min[1], classic_points->bound_min[2],
		   classic_points->bound_max[0], classic_points->bound_max[1], classic_points->bound_max[2]);
	printf("      TIE98   bounds: [%9.1f %9.1f %9.1f] .. "
		   "[%9.1f %9.1f %9.1f]\n",
		   opt_points->bound_min[0], opt_points->bound_min[1], opt_points->bound_min[2],
		   opt_points->bound_max[0], opt_points->bound_max[1], opt_points->bound_max[2]);

	const ShipModelMesh* classic_meshes = (const ShipModelMesh*)&classic->lod_records[classic->num_lods];
	for (uint8_t source = 0; source < classic->num_meshes; ++source) {
		uint8_t destination = source;
		if (destination >= opt->mesh_count) {
			printf("      mesh %u -> %u: destination is absent\n", source, destination);
			continue;
		}
		const opt_mesh_t* opt_mesh = &opt->meshes[destination];
		int opt_type = opt_mesh->has_descriptor ? opt_mesh->descriptor.mesh_type : -1;
		bool classic_rotates = classic_meshes[source].rotation_offset != 0;
		bool opt_nondefault = TieModelCompat_OptMeshHasNondefaultFrame(opt_mesh);
		bool opt_rotary = opt_mesh->has_descriptor && opt_mesh_type_is_rotary(opt_mesh->descriptor.mesh_type);
		if (classic_meshes[source].mesh_type != opt_type || classic_rotates || opt_nondefault || opt_rotary) {
			printf("      mesh %u -> %u: type %s/%s, "
				   "TIE95 rotation=%s, OPT frame=%s, OPT rotary type=%s\n",
				   source, destination, opt_mesh_type_name((opt_mesh_type_t)classic_meshes[source].mesh_type),
				   opt_mesh->has_descriptor ? opt_mesh_type_name(opt_mesh->descriptor.mesh_type)
											: "<missing>",
				   classic_rotates ? "yes" : "no",
				   !opt_mesh->has_rotation_scale ? "absent"
				   : opt_nondefault              ? "non-default"
												 : "identity",
				   opt_rotary ? "yes" : "no");
		}
	}
}

static const char* TieModelCompat_Classification(const TieModelCompatLayoutComparison* layout) {
	if (!layout->valid)
		return "REVIEW";
	return layout->identity ? "DIRECT" : "REVIEW";
}

static bool TieModelCompat_ModelBlob(const SpeciesEntry* species, const TieLfdFileEntry* entry,
									 const uint8_t* payload, const uint8_t** blob, size_t* blob_size,
									 const ShipModelData** model) {
	size_t strip = (species->load_flags & 1) ? 2u : 0u;
	if (entry->size < strip + 2u + sizeof(ShipModelData))
		return false;
	*blob = payload + strip;
	*blob_size = entry->size - strip;
	*model = (const ShipModelData*)(*blob + 2);
	size_t table_size = 0x20u + 6u * (*model)->num_lods + 64u * (*model)->num_meshes;
	return table_size <= *blob_size - 2u;
}

static void TieModelCompat_PrintMissing(unsigned species_index, const char* symbolic, const char* lfd_name,
										const char* opt_path, bool catalog_pair,
										TieModelCompatTotals* totals) {
	printf("%3u %-23s %-8s %-30s %-7s %-8s\n", species_index, symbolic, lfd_name, opt_path, "NO_OPT",
		   catalog_pair ? "catalog" : "name");
	++totals->no_opt;
}

static void TieModelCompat_CompareOne(unsigned species_index, const SpeciesEntry* species,
									  const TieLfdFile* lfd, const TieLfdFileEntry* lfd_entry,
									  const TieFlightAssetEntry* asset, const char* opt_relative,
									  const TieModelCompatOptions* options, TieModelCompatTotals* totals) {
	const char* symbolic = tie_species_symbolic_name((uint16_t)species_index);
	char fallback_symbol[24];
	if (!symbolic[0]) {
		snprintf(fallback_symbol, sizeof fallback_symbol, "species_%u", species_index);
		symbolic = fallback_symbol;
	}
	char opt_path[PATH_CAPACITY];
	if (!TieModelCompat_JoinPath(opt_path, options->tie98_root, NULL, opt_relative) ||
		access(opt_path, R_OK) != 0) {
		TieModelCompat_PrintMissing(species_index, symbolic, lfd_entry->name, opt_relative, asset != NULL,
									totals);
		return;
	}

	const uint8_t* blob = NULL;
	const ShipModelData* classic_header = NULL;
	size_t blob_size = 0;
	if (!TieModelCompat_ModelBlob(species, lfd_entry, TieLfdFile_Data(lfd, lfd_entry), &blob, &blob_size,
								  &classic_header)) {
		printf("%3u %-23s %-8s %-30s ERROR   invalid TIE95 model header\n", species_index, symbolic,
			   lfd_entry->name, opt_relative);
		++totals->errors;
		return;
	}

	TieFlightShipModel classic = { 0 };
	if (!TieShipModelConverter_Build(blob, blob_size, 90.0f, &classic)) {
		printf("%3u %-23s %-8s %-30s ERROR   TIE95 conversion failed\n", species_index, symbolic,
			   lfd_entry->name, opt_relative);
		++totals->errors;
		return;
	}
	opt_error_t opt_error = { 0 };
	opt_file_t* opt = opt_load_file(opt_path, &opt_error);
	if (!opt) {
		printf("%3u %-23s %-8s %-30s ERROR   %s\n", species_index, symbolic, lfd_entry->name, opt_relative,
			   opt_error.msg);
		TieShipModelConverter_Free(&classic);
		++totals->errors;
		return;
	}

	TieModelCompatPointSet classic_points = { 0 };
	TieModelCompatPointSet opt_points = { 0 };
	/* Classic's vertex transform contributes vertex/2 to world raw units;
	 * TIE98 OPT coordinates do not contain that extra denominator. Express
	 * OPT vertices in classic raw coordinate units before comparing them. */
	double opt_scale = 2.0;
	if (!TieModelCompat_BuildClassicPoints(&classic, &classic_points) ||
		!TieModelCompat_BuildOptPoints(opt, opt_scale, &opt_points)) {
		printf("%3u %-23s %-8s %-30s ERROR   geometry allocation failed\n", species_index, symbolic,
			   lfd_entry->name, opt_relative);
		free(classic_points.points);
		free(opt_points.points);
		opt_free(opt);
		TieShipModelConverter_Free(&classic);
		++totals->errors;
		return;
	}

	TieModelCompatGeometryComparison geometry =
		TieModelCompat_CompareGeometry(&classic_points, &opt_points, options->tolerance);
	TieModelCompatLayoutComparison layout = TieModelCompat_CompareLayout(classic_header, opt);
	const char* status = TieModelCompat_Classification(&layout);
	unsigned classic_hardpoints = TieModelCompat_ClassicHardpointCount(classic_header);
	unsigned opt_hardpoints = TieModelCompat_OptHardpointCount(opt);

	printf("%3u %-23s %-8s %-30s %-7s %-8s "
		   "mesh=%u/%d type=%u rot=c%u/r%u/f%u/n%u/m%u bounds=%-9s "
		   "vertices=%zu/%zu %.0f%%/%.0f%% hp=%u/%u\n",
		   species_index, symbolic, lfd_entry->name, opt_relative, status, asset ? "catalog" : "name",
		   classic_header->num_meshes, opt->mesh_count, layout.type_mismatches,
		   layout.classic_rotation_blocks, layout.opt_rotary_meshes, layout.opt_rotation_frames,
		   layout.opt_nondefault_frames, layout.missing_opt_rotation_frames, geometry.bounds_class,
		   classic_points.count, opt_points.count, geometry.classic_coverage_percent,
		   geometry.opt_coverage_percent, classic_hardpoints, opt_hardpoints);
	if (options->verbose) {
		printf("      edge delta %.2f, size error %.3f%%, center error %.3f%%, "
			   "unmapped OPT %u, duplicated OPT %u%s\n",
			   geometry.max_edge_delta, geometry.max_size_error_percent, geometry.center_error_percent,
			   layout.unmapped_opt_meshes, layout.duplicated_opt_meshes,
			   geometry.vertices_equal ? ", identical vertex sets" : "");
		TieModelCompat_PrintVerboseDetails(&classic_points, &opt_points, classic_header, opt);
	}

	++totals->compared;
	if (strcmp(status, "DIRECT") == 0)
		++totals->direct;
	else
		++totals->review;

	free(classic_points.points);
	free(opt_points.points);
	opt_free(opt);
	TieShipModelConverter_Free(&classic);
}

static int TieModelCompat_Run(const TieModelCompatOptions* options) {
	TieLfdFile lfds[3] = { 0 };
	if (!TieModelCompat_OpenSpeciesLfds(options, lfds))
		return 2;
	AeronVfs* vfs = NULL;
	TieFlightAssetBundle* catalog = TieModelCompat_OpenCatalog(options, &vfs);
	if (!catalog) {
		for (int index = 0; index < 3; ++index)
			TieLfdFile_Close(&lfds[index]);
		return 2;
	}

	printf("TIE95 root: %s/%s\n", options->tie95_root, options->resource_set);
	printf("TIE98 root: %s\n", options->tie98_root);
	printf("Catalog:    %s/flight/tie98-models.yaml\n", options->catalog_root);
	printf("Tolerance:  %.3f model units\n\n", options->tolerance);
	printf(" ID SYMBOL                  LFD      OPT                            STATUS  PAIR     DETAILS\n");

	TieModelCompatTotals totals = { 0 };
	const uint32_t ship_type = TieModelCompat_Fourcc("SHIP");
	for (unsigned index = 0; index < MODEL_SPECIES_LIMIT; ++index) {
		if (options->species_filter >= 0 && index != (unsigned)options->species_filter)
			continue;
		const SpeciesEntry* species = &species_table[index];
		if (!(species->flags & 2) || !(species->load_flags & 1) || species->lfd_file >= 3)
			continue;
		const TieLfdFile* lfd = &lfds[species->lfd_file];
		if (species->lfd_entry >= lfd->count) {
			fprintf(stderr, "species %u refers to absent LFD %u entry %u\n", index, species->lfd_file,
					species->lfd_entry);
			++totals.errors;
			continue;
		}
		const TieLfdFileEntry* entry = &lfd->entries[species->lfd_entry];
		if (entry->type != ship_type)
			continue;

		const TieFlightAssetEntry* asset = TieFlightAssets_Find(catalog, (uint16_t)index);
		char fallback[64];
		const char* opt_relative = NULL;
		if (asset) {
			opt_relative = asset->path;
		} else {
			snprintf(fallback, sizeof fallback, "IVFILES/%s.OPT", entry->name);
			opt_relative = fallback;
		}
		TieModelCompat_CompareOne(index, species, lfd, entry, asset, opt_relative, options, &totals);
	}

	printf("\nSummary: %u compared: %u direct, %u review; "
		   "%u without OPT; %u errors.\n",
		   totals.compared, totals.direct, totals.review, totals.no_opt, totals.errors);
	printf("Vertex percentages are TIE95-covered/TIE98-covered after scaling, "
		   "within tolerance.\n");
	printf("Rotation counts: c=TIE95 rotation blocks, r=OPT rotary mesh types, "
		   "f=OPT frame records, n=non-default OPT frames, "
		   "m=required OPT frames missing.\n");
	printf("OPT frames describe pivots and axes; neither f nor n implies "
		   "runtime animation.\n");
	printf("Status evaluates component-state routing; bounds and vertex "
		   "coverage are independent geometry results.\n");

	TieFlightAssets_Close(catalog);
	AeronVfs_Destroy(vfs);
	for (int index = 0; index < 3; ++index)
		TieLfdFile_Close(&lfds[index]);
	return totals.errors ? 2 : 0;
}

int main(int argc, char** argv) {
	TieModelCompatOptions options;
	if (!TieModelCompat_ParseOptions(argc, argv, &options)) {
		TieModelCompat_Usage(stderr, argv[0]);
		return 2;
	}
	return TieModelCompat_Run(&options);
}
