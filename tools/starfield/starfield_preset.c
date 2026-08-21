/* starfield_preset — see starfield_preset.h. */

#include "starfield_preset.h"

#include "aeron/config_file.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void TieStarfieldPreset_SetError(char* buf, size_t cap, const char* msg) {
	if (buf && cap) {
		strncpy(buf, msg, cap - 1);
		buf[cap - 1] = '\0';
	}
}
bool TieStarfieldPreset_Save(const char* path, const TieStarfieldParams* p, const TieStarfieldElement* els,
							 int n_els, char* errbuf, size_t errcap) {
	char tmp[2048];
	int n = snprintf(tmp, sizeof tmp, "%s.tmp", path);
	if (n < 0 || n >= (int)sizeof tmp) {
		TieStarfieldPreset_SetError(errbuf, errcap, "path too long");
		return false;
	}
	FILE* fp = fopen(tmp, "wb");
	if (!fp) {
		TieStarfieldPreset_SetError(errbuf, errcap, "open for write failed");
		return false;
	}

	fprintf(fp, "starfield_version: 1\n");
	fprintf(fp, "num_stars: %d\n", p->num_stars);
	fprintf(fp, "seed: %llu\n", (unsigned long long)p->seed);
	fprintf(fp, "intensity: %g\n", (double)p->intensity);
	fprintf(fp, "bright_floor: %g\n", (double)p->bright_floor);
	fprintf(fp, "bright_pow: %g\n", (double)p->bright_pow);
	for (int i = 0; i < STARFIELD_TIERS; ++i) {
		fprintf(fp, "tier%d_thresh: %g\n", i + 1, (double)p->tier_thresh[i]);
		fprintf(fp, "tier%d_sigma: %g\n", i + 1, (double)p->tier_sigma[i]);
	}
	fprintf(fp, "tint_sigma: %g\n", (double)p->tint_sigma);
	fprintf(fp, "tint_strength: %g\n", (double)p->tint_strength);
	fprintf(fp, "tint_bias: %g\n", (double)p->tint_bias);
	fprintf(fp, "bg_r: %g\n", (double)p->bg_color[0]);
	fprintf(fp, "bg_g: %g\n", (double)p->bg_color[1]);
	fprintf(fp, "bg_b: %g\n", (double)p->bg_color[2]);
	fprintf(fp, "face_size: %d\n", p->face_size);
	fprintf(fp, "zstd: %d\n", p->zstd ? 1 : 0);

	if (n_els > 0) {
		fprintf(fp, "elements:\n");
		for (int i = 0; i < n_els; ++i) {
			const TieStarfieldElement* e = &els[i];
			fprintf(fp, "  - path: \"%s\"\n", e->path);
			fprintf(fp, "    yaw: %g\n", (double)e->yaw);
			fprintf(fp, "    pitch: %g\n", (double)e->pitch);
			fprintf(fp, "    size_deg: %g\n", (double)e->size_deg);
			fprintf(fp, "    roll_deg: %g\n", (double)e->roll_deg);
			fprintf(fp, "    intensity: %g\n", (double)e->intensity);
			fprintf(fp, "    tint: [%g, %g, %g]\n", (double)e->tint[0], (double)e->tint[1],
					(double)e->tint[2]);
			fprintf(fp, "    enabled: %d\n", e->enabled ? 1 : 0);
		}
	}

	if (fclose(fp) != 0) {
		TieStarfieldPreset_SetError(errbuf, errcap, "write/close failed");
		remove(tmp);
		return false;
	}
	if (rename(tmp, path) != 0) {
		TieStarfieldPreset_SetError(errbuf, errcap, "rename failed");
		remove(tmp);
		return false;
	}
	return true;
}
static void TieStarfieldPreset_LoadString(const AeronConfigNode* map, const char* key, char* dst,
										  size_t cap) {
	const char* s = AeronConfigNode_String(AeronConfigNode_MapGet(map, key), NULL);
	if (s) {
		strncpy(dst, s, cap - 1);
		dst[cap - 1] = '\0';
	}
}

static void TieStarfieldPreset_LoadInt(const AeronConfigNode* map, const char* key, int* dst) {
	const AeronConfigNode* node = AeronConfigNode_MapGet(map, key);
	if (AeronConfigNode_Type(node) == AERON_CONFIG_INT)
		*dst = (int)AeronConfigNode_Int(node, *dst);
}

static void TieStarfieldPreset_LoadU64(const AeronConfigNode* map, const char* key, uint64_t* dst) {
	const AeronConfigNode* node = AeronConfigNode_MapGet(map, key);
	if (AeronConfigNode_Type(node) == AERON_CONFIG_INT)
		*dst = (uint64_t)AeronConfigNode_Int(node, (int64_t)*dst);
}

static void TieStarfieldPreset_LoadFloat(const AeronConfigNode* map, const char* key, float* dst) {
	const AeronConfigNode* node = AeronConfigNode_MapGet(map, key);
	if (node)
		*dst = (float)AeronConfigNode_Float(node, *dst);
}

static void TieStarfieldPreset_LoadBool(const AeronConfigNode* map, const char* key, bool* dst) {
	const AeronConfigNode* node = AeronConfigNode_MapGet(map, key);
	if (AeronConfigNode_Type(node) == AERON_CONFIG_BOOL)
		*dst = AeronConfigNode_Bool(node, *dst) != 0;
	else if (AeronConfigNode_Type(node) == AERON_CONFIG_INT)
		*dst = AeronConfigNode_Int(node, *dst) != 0;
}

static AeronVfs* TieStarfieldPreset_PresetVfs(const char* path, char* file_name, size_t capacity) {
	const char* slash;
	const char* backslash;
	if (!path || !path[0])
		return NULL;
	slash = strrchr(path, '/');
	backslash = strrchr(path, '\\');
	const char* separator = !slash ? backslash : !backslash ? slash : slash > backslash ? slash : backslash;
	char directory[2048];
	AeronVfsConfig config = { 0 };
	size_t length;

	if (!separator) {
		snprintf(directory, sizeof directory, "%s", ".");
		if (strlen(path) >= capacity)
			return NULL;
		memcpy(file_name, path, strlen(path) + 1);
	} else {
		length = (size_t)(separator - path);
		if (length == 0)
			length = 1;
		if (length >= sizeof directory)
			return NULL;
		memcpy(directory, path, length);
		directory[length] = '\0';
		if (strlen(separator + 1) >= capacity)
			return NULL;
		memcpy(file_name, separator + 1, strlen(separator + 1) + 1);
	}
	config.asset_root = directory;
	config.resource_root = directory;
	config.user_root = directory;
	config.temp_root = directory;
	return AeronVfs_Create(&config);
}

bool TieStarfieldPreset_Load(const char* path, TieStarfieldParams* p, TieStarfieldElement* els, int* n_els,
							 int max_els, char* errbuf, size_t errcap) {
	char file_name[1024];
	AeronVfs* vfs;
	AeronConfigFile* document = NULL;
	AeronConfigError error = { 0 };
	const AeronConfigNode* root;
	const AeronConfigNode* sequence;
	size_t index;

	if (n_els)
		*n_els = 0;
	vfs = TieStarfieldPreset_PresetVfs(path, file_name, sizeof file_name);
	if (!vfs || !AeronConfigFile_LoadYamlEx(vfs, AERON_VFS_ROOT_RESOURCE, file_name, &document, &error)) {
		TieStarfieldPreset_SetError(errbuf, errcap, vfs ? error.message : "invalid preset path");
		AeronVfs_Destroy(vfs);
		return false;
	}
	root = AeronConfigFile_Root(document);
	if (AeronConfigNode_Type(root) != AERON_CONFIG_MAP) {
		TieStarfieldPreset_SetError(errbuf, errcap, "root is not a mapping");
		AeronConfigFile_Destroy(document);
		AeronVfs_Destroy(vfs);
		return false;
	}

	TieStarfieldPreset_LoadInt(root, "num_stars", &p->num_stars);
	TieStarfieldPreset_LoadU64(root, "seed", &p->seed);
	TieStarfieldPreset_LoadFloat(root, "intensity", &p->intensity);
	TieStarfieldPreset_LoadFloat(root, "bright_floor", &p->bright_floor);
	TieStarfieldPreset_LoadFloat(root, "bright_pow", &p->bright_pow);
	for (int i = 0; i < STARFIELD_TIERS; ++i) {
		char key[32];
		snprintf(key, sizeof key, "tier%d_thresh", i + 1);
		TieStarfieldPreset_LoadFloat(root, key, &p->tier_thresh[i]);
		snprintf(key, sizeof key, "tier%d_sigma", i + 1);
		TieStarfieldPreset_LoadFloat(root, key, &p->tier_sigma[i]);
	}
	TieStarfieldPreset_LoadFloat(root, "tint_sigma", &p->tint_sigma);
	TieStarfieldPreset_LoadFloat(root, "tint_strength", &p->tint_strength);
	TieStarfieldPreset_LoadFloat(root, "tint_bias", &p->tint_bias);
	TieStarfieldPreset_LoadFloat(root, "bg_r", &p->bg_color[0]);
	TieStarfieldPreset_LoadFloat(root, "bg_g", &p->bg_color[1]);
	TieStarfieldPreset_LoadFloat(root, "bg_b", &p->bg_color[2]);
	TieStarfieldPreset_LoadInt(root, "face_size", &p->face_size);
	TieStarfieldPreset_LoadBool(root, "zstd", &p->zstd);

	sequence = AeronConfigNode_MapGet(root, "elements");
	if (els && n_els && AeronConfigNode_Type(sequence) == AERON_CONFIG_SEQUENCE) {
		for (index = 0; index < AeronConfigNode_SequenceCount(sequence) && *n_els < max_els; ++index) {
			const AeronConfigNode* item = AeronConfigNode_SequenceGet(sequence, index);
			const AeronConfigNode* tint;
			size_t component;
			if (AeronConfigNode_Type(item) != AERON_CONFIG_MAP)
				continue;
			TieStarfieldElement* e = &els[*n_els];
			TieStarfieldElements_StarfieldElementDefault(e);
			TieStarfieldPreset_LoadString(item, "path", e->path, sizeof e->path);
			TieStarfieldPreset_LoadFloat(item, "yaw", &e->yaw);
			TieStarfieldPreset_LoadFloat(item, "pitch", &e->pitch);
			TieStarfieldPreset_LoadFloat(item, "size_deg", &e->size_deg);
			TieStarfieldPreset_LoadFloat(item, "roll_deg", &e->roll_deg);
			TieStarfieldPreset_LoadFloat(item, "intensity", &e->intensity);
			tint = AeronConfigNode_MapGet(item, "tint");
			for (component = 0; component < AeronConfigNode_SequenceCount(tint) && component < 3; ++component)
				e->tint[component] = (float)AeronConfigNode_Float(
					AeronConfigNode_SequenceGet(tint, component), e->tint[component]);
			int en = e->enabled ? 1 : 0;
			TieStarfieldPreset_LoadInt(item, "enabled", &en);
			e->enabled = en != 0;
			(*n_els)++;
		}
	}

	AeronConfigFile_Destroy(document);
	AeronVfs_Destroy(vfs);
	return true;
}
