#include "tie_app/setup/installation.h"

#include "tie_runtime/flight_assets/assets.h"

#include <SDL3/SDL_filesystem.h>

#include <stdio.h>
#include <string.h>

static bool TieInstallation_InstallationError(char* error, size_t capacity, const char* format,
											  const char* detail) {
	if (error && capacity)
		snprintf(error, capacity, format, detail ? detail : "");
	return false;
}

static int TieInstallation_InstallationDiscSource(const char* path, char* source, size_t capacity) {
	static const char* const candidates[] = { "game.ins", "game.gog", "game.fil" };
	SDL_PathInfo info;
	if (!SDL_GetPathInfo(path, &info))
		return 0;
	if (info.type == SDL_PATHTYPE_FILE) {
		if (strlen(path) >= capacity)
			return -1;
		snprintf(source, capacity, "%s", path);
		return 1;
	}
	if (info.type != SDL_PATHTYPE_DIRECTORY)
		return 0;
	for (size_t index = 0; index < sizeof candidates / sizeof candidates[0]; ++index) {
		const int length = snprintf(source, capacity, "%s/%s", path, candidates[index]);
		if (length < 0 || (size_t)length >= capacity)
			return -1;
		if (SDL_GetPathInfo(source, &info) && info.type == SDL_PATHTYPE_FILE)
			return 1;
	}
	return 0;
}

static AeronVfs* TieInstallation_CandidateVfs(const char* path, const char* resource_root, char* error,
											  size_t error_capacity) {
	AeronVfs* vfs = AeronVfs_Create(&(AeronVfsConfig) {
		.asset_root = path,
		.resource_root = resource_root,
	});
	if (!vfs) {
		TieInstallation_InstallationError(error, error_capacity,
										  "Could not create the installation filesystem: %s.", path);
		return NULL;
	}
	if (!AeronVfs_SetRootOptions(vfs, AERON_VFS_ROOT_ASSET, AERON_VFS_ROOT_OPTION_CASE_INSENSITIVE_LOOKUP)) {
		AeronVfs_Destroy(vfs);
		return NULL;
	}
	char source[TIE_GAME_DATA_PATH_MAX];
	const int disc = TieInstallation_InstallationDiscSource(path, source, sizeof source);
	if (disc < 0) {
		AeronVfs_Destroy(vfs);
		TieInstallation_InstallationError(error, error_capacity,
										  "The TIE Fighter CD image path is too long: %s.", path);
		return NULL;
	}
	if (disc > 0 && !AeronVfs_SetDiscRoot(vfs, AERON_VFS_ROOT_ASSET, source)) {
		char detail[256];
		snprintf(detail, sizeof detail, "%s", SDL_GetError());
		AeronVfs_Destroy(vfs);
		TieInstallation_InstallationError(error, error_capacity,
										  "Could not read the TIE Fighter CD image: %s.", detail);
		return NULL;
	}
	return vfs;
}

static bool TieInstallation_RequirePath(AeronVfs* vfs, const char* path, char* error, size_t capacity) {
	if (AeronVfs_Exists(vfs, AERON_VFS_ROOT_ASSET, path))
		return true;
	return TieInstallation_InstallationError(
		error, capacity, "This is not a complete TIE Fighter installation. Missing %s.", path);
}

static bool TieInstallation_ValidateCommon(AeronVfs* vfs, char* error, size_t capacity) {
	static const char* const required[] = {
		"STRINGS.DAT", "RESOURCE", "RESOURCE/TITLE.LFD", "RESOURCE/MAINMENU.LFD", "MISSION",
		"RES320",      "CP320",
	};
	if (!AeronVfs_Exists(vfs, AERON_VFS_ROOT_ASSET, "STRINGS.DAT") &&
		AeronVfs_Exists(vfs, AERON_VFS_ROOT_ASSET, "TITLE.LFD") &&
		AeronVfs_Exists(vfs, AERON_VFS_ROOT_ASSET, "MAINMENU.LFD"))
		return TieInstallation_InstallationError(
			error, capacity,
			"This appears to be the installation's RESOURCE directory. Select its parent: %s.",
			"STRINGS.DAT is in the parent directory");
	for (size_t index = 0; index < sizeof required / sizeof required[0]; ++index)
		if (!TieInstallation_RequirePath(vfs, required[index], error, capacity))
			return false;
	return true;
}

static bool TieInstallation_HasTie98Signature(AeronVfs* vfs, int* out_present) {
	static const char* const signature[] = {
		"RESOURCE/MM640.LFD",
		"RESOURCE/BRIEF640.LFD",
		"RESOURCE/REG640.LFD",
		"RESOURCE/TRAIN640.LFD",
		"IVFILES",
	};
	int present = 0;
	for (size_t index = 0; index < sizeof signature / sizeof signature[0]; ++index)
		present += AeronVfs_Exists(vfs, AERON_VFS_ROOT_ASSET, signature[index]) != 0;
	*out_present = present;
	return present == (int)(sizeof signature / sizeof signature[0]);
}

static bool TieInstallation_ValidateTie98Models(AeronVfs* vfs, char* error, size_t capacity) {
	TieFlightAssetBundle* catalog =
		TieFlightAssets_Open(vfs, TIE_FLIGHT_ASSET_CATALOG_TIE98, error, capacity);
	if (!catalog)
		return false;
	for (uint16_t species = 0; species < 161; ++species) {
		const TieFlightAssetEntry* entry = TieFlightAssets_Find(catalog, species);
		if (entry && !TieInstallation_RequirePath(vfs, entry->path, error, capacity)) {
			TieFlightAssets_Close(catalog);
			return false;
		}
	}
	TieFlightAssets_Close(catalog);
	return true;
}

static bool TieInstallation_ValidateVersion(AeronVfs* vfs, TieGameVersion version, char* error,
											size_t capacity) {
	int signature_count = 0;
	const bool complete_tie98 = TieInstallation_HasTie98Signature(vfs, &signature_count);
	if (version == TIE_GAME_VERSION_TIE98) {
		if (!complete_tie98)
			return TieInstallation_InstallationError(error, capacity,
													 "The TIE98 installation is incomplete. Missing %s.",
													 "one or more SVGA archives or IVFILES");
		return TieInstallation_ValidateTie98Models(vfs, error, capacity);
	}
	if (signature_count != 0)
		return TieInstallation_InstallationError(
			error, capacity,
			complete_tie98 ? "This is a TIE98 installation, not TIE95: %s."
						   : "This installation has an incomplete TIE98 signature: %s.",
			"SVGA/IVFILES content is present");
	return true;
}

bool TieInstallation_Classify(const char* path, TieGameVersion* out_version, char* error, size_t capacity) {
	if (!path || !path[0] || strlen(path) >= TIE_GAME_DATA_PATH_MAX || !out_version)
		return TieInstallation_InstallationError(
			error, capacity, "The selected installation path is invalid: %s.", path ? path : "");
	AeronVfs* vfs = TieInstallation_CandidateVfs(path, NULL, error, capacity);
	if (!vfs)
		return false;
	bool valid = TieInstallation_ValidateCommon(vfs, error, capacity);
	if (valid) {
		int signature_count = 0;
		const bool tie98 = TieInstallation_HasTie98Signature(vfs, &signature_count);
		if (signature_count != 0 && !tie98)
			valid = TieInstallation_InstallationError(
				error, capacity, "This installation has an incomplete TIE98 signature: %s.", path);
		else
			*out_version = tie98 ? TIE_GAME_VERSION_TIE98 : TIE_GAME_VERSION_TIE95;
	}
	AeronVfs_Destroy(vfs);
	return valid;
}

bool TieInstallation_Open(TieInstallation* installation, TieGameVersion version, const char* path,
						  const char* resource_root, char* error, size_t capacity) {
	if (!installation || !path || !path[0] || strlen(path) >= sizeof installation->root)
		return TieInstallation_InstallationError(
			error, capacity, "The selected installation path is invalid: %s.", path ? path : "");
	memset(installation, 0, sizeof *installation);
	AeronVfs* vfs = TieInstallation_CandidateVfs(path, resource_root, error, capacity);
	if (!vfs)
		return false;
	if (!TieInstallation_ValidateCommon(vfs, error, capacity) ||
		!TieInstallation_ValidateVersion(vfs, version, error, capacity)) {
		AeronVfs_Destroy(vfs);
		return false;
	}
	installation->version = version;
	installation->vfs = vfs;
	snprintf(installation->root, sizeof installation->root, "%s", path);
	return true;
}

void TieInstallation_Close(TieInstallation* installation) {
	if (!installation)
		return;
	AeronVfs_Destroy(installation->vfs);
	memset(installation, 0, sizeof *installation);
}

void TieInstallation_SetClose(TieInstallationSet* installations) {
	if (!installations)
		return;
	TieInstallation_Close(&installations->tie95);
	TieInstallation_Close(&installations->tie98);
	memset(installations, 0, sizeof *installations);
}

const TieInstallation* TieInstallation_Get(const TieInstallationSet* installations, TieGameVersion version) {
	if (!installations)
		return NULL;
	if (version == TIE_GAME_VERSION_TIE98)
		return installations->has_tie98 ? &installations->tie98 : NULL;
	return installations->has_tie95 ? &installations->tie95 : NULL;
}
