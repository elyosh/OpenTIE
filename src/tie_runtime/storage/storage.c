#include "tie_runtime/storage/storage.h"

#include "aeron/log.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

typedef struct TieStorageDirectory {
	TieDirEntry* entries;
	int count;
	int next;
	int capacity;
} TieStorageDirectory;

static TieStorageConfig s_storage;

void TieStorage_Init(const TieStorageConfig* config) {
	s_storage = config ? *config : (TieStorageConfig) { 0 };
}

void TieStorage_Shutdown(void) { memset(&s_storage, 0, sizeof s_storage); }

void TieStorage_SelectFlightVfs(AeronVfs* vfs) { s_storage.flight = vfs; }

AeronVfs* TieStorage_Tie95FlightVfs(void) { return s_storage.tie95_flight; }

AeronVfs* TieStorage_Tie98FlightVfs(void) { return s_storage.tie98_flight; }

static bool TieStorage_Location(TieFileRoot root, AeronVfs** out_vfs, AeronVfsRoot* out_root) {
	if (!out_vfs || !out_root)
		return false;
	switch (root) {
		case TIE_FILE_ROOT_FRONTEND_ASSET:
			*out_vfs = s_storage.frontend;
			*out_root = AERON_VFS_ROOT_ASSET;
			break;
		case TIE_FILE_ROOT_FLIGHT_ASSET:
			*out_vfs = s_storage.flight;
			*out_root = AERON_VFS_ROOT_ASSET;
			break;
		case TIE_FILE_ROOT_TIE98_MEDIA:
			*out_vfs = s_storage.tie98_media;
			*out_root = AERON_VFS_ROOT_ASSET;
			break;
		case TIE_FILE_ROOT_USER:
			*out_vfs = s_storage.application;
			*out_root = AERON_VFS_ROOT_USER;
			break;
		case TIE_FILE_ROOT_TEMP:
			*out_vfs = s_storage.application;
			*out_root = AERON_VFS_ROOT_TEMP;
			break;
		default:
			return false;
	}
	return *out_vfs != NULL;
}

static AeronVfsOpenMode TieStorage_OpenMode(const char* mode) {
	const int update = mode && strchr(mode, '+') != NULL;
	switch (mode ? mode[0] : 'r') {
		case 'w':
			return update ? AERON_VFS_WRITE_READ : AERON_VFS_WRITE;
		case 'a':
			return AERON_VFS_APPEND;
		default:
			return update ? AERON_VFS_READ_WRITE : AERON_VFS_READ;
	}
}

TieFile* TieStorage_Open(TieFileRoot root, const char* path, const char* mode) {
	AeronVfs* vfs;
	AeronVfsRoot vfs_root;
	AeronFile* file = NULL;
	if (!TieStorage_Location(root, &vfs, &vfs_root) || !path || !mode)
		return NULL;
	if ((root == TIE_FILE_ROOT_FRONTEND_ASSET || root == TIE_FILE_ROOT_FLIGHT_ASSET ||
		 root == TIE_FILE_ROOT_TIE98_MEDIA) &&
		(mode[0] != 'r' || strchr(mode, '+') != NULL)) {
		Aeron_LogError("tie.files", "refusing writable asset open: %s (%s)", path, mode);
		return NULL;
	}
	return AeronVfs_Open(vfs, vfs_root, path, TieStorage_OpenMode(mode), &file) ? (TieFile*)file : NULL;
}

size_t TieStorage_Read(void* buffer, size_t size, size_t count, TieFile* file) {
	size_t bytes = 0;
	if (!file || size == 0 || count == 0)
		return 0;
	AeronVfs_Read((AeronFile*)file, buffer, size * count, &bytes);
	return bytes / size;
}

size_t TieStorage_Write(const void* buffer, size_t size, size_t count, TieFile* file) {
	size_t bytes = 0;
	if (!file || size == 0 || count == 0)
		return 0;
	AeronVfs_Write((AeronFile*)file, buffer, size * count, &bytes);
	return bytes / size;
}

int TieStorage_WriteAllAtomic(TieFileRoot root, const char* path, const void* data, size_t size) {
	AeronVfs* vfs;
	AeronVfsRoot vfs_root;
	if ((root != TIE_FILE_ROOT_USER && root != TIE_FILE_ROOT_TEMP) || !path || (!data && size) ||
		!TieStorage_Location(root, &vfs, &vfs_root))
		return -1;
	return AeronVfs_WriteAllAtomic(vfs, vfs_root, path, data, size) ? 0 : -1;
}

int TieStorage_Seek(TieFile* file, long offset, int whence) {
	return file && AeronVfs_Seek((AeronFile*)file, offset, whence) ? 0 : -1;
}

long TieStorage_Tell(TieFile* file) { return file ? (long)AeronVfs_Tell((AeronFile*)file) : -1; }

int TieStorage_Close(TieFile* file) { return file && AeronVfs_Close((AeronFile*)file) ? 0 : -1; }

int TieStorage_Remove(TieFileRoot root, const char* path) {
	AeronVfs* vfs;
	AeronVfsRoot vfs_root;
	if (root == TIE_FILE_ROOT_FRONTEND_ASSET || root == TIE_FILE_ROOT_FLIGHT_ASSET ||
		root == TIE_FILE_ROOT_TIE98_MEDIA || !TieStorage_Location(root, &vfs, &vfs_root))
		return -1;
	return AeronVfs_Remove(vfs, vfs_root, path) ? 0 : -1;
}

static int TieStorage_CollectEntry(void* userdata, const AeronVfsEntry* entry) {
	TieStorageDirectory* directory = (TieStorageDirectory*)userdata;
	if (directory->count == directory->capacity) {
		const int capacity = directory->capacity ? directory->capacity * 2 : 16;
		TieDirEntry* entries = (TieDirEntry*)realloc(directory->entries, (size_t)capacity * sizeof *entries);
		if (!entries)
			return 0;
		directory->entries = entries;
		directory->capacity = capacity;
	}
	TieDirEntry* out = &directory->entries[directory->count++];
	strncpy(out->name, entry->name, TIE_DIR_NAME_MAX - 1);
	out->name[TIE_DIR_NAME_MAX - 1] = '\0';
	out->is_dir = entry->is_directory ? 1 : 0;
	out->size = entry->is_directory ? 0 : (uint64_t)(entry->size > 0 ? entry->size : 0);
	return 1;
}

TieDir* TieStorage_DirOpen(TieFileRoot root, const char* path) {
	AeronVfs* vfs;
	AeronVfsRoot vfs_root;
	AeronFileInfo info;
	if (!path || !TieStorage_Location(root, &vfs, &vfs_root) || !AeronVfs_Stat(vfs, vfs_root, path, &info) ||
		!info.is_directory)
		return NULL;
	TieStorageDirectory* directory = (TieStorageDirectory*)calloc(1, sizeof *directory);
	if (!directory)
		return NULL;
	if (!AeronVfs_Glob(vfs, vfs_root, path, "*", AERON_VFS_GLOB_FILES | AERON_VFS_GLOB_DIRECTORIES,
					   TieStorage_CollectEntry, directory)) {
		free(directory->entries);
		free(directory);
		return NULL;
	}
	return (TieDir*)directory;
}

int TieStorage_DirNext(TieDir* handle, TieDirEntry* entry) {
	TieStorageDirectory* directory = (TieStorageDirectory*)handle;
	if (!directory || !entry || directory->next >= directory->count)
		return 0;
	*entry = directory->entries[directory->next++];
	return 1;
}

void TieStorage_DirClose(TieDir* handle) {
	TieStorageDirectory* directory = (TieStorageDirectory*)handle;
	if (!directory)
		return;
	free(directory->entries);
	free(directory);
}

int TieStorage_IsDirectory(TieFileRoot root, const char* path) {
	AeronVfs* vfs;
	AeronVfsRoot vfs_root;
	AeronFileInfo info;
	return path && TieStorage_Location(root, &vfs, &vfs_root) && AeronVfs_Stat(vfs, vfs_root, path, &info) &&
		   info.is_directory;
}

int TieStorage_Getc(TieFile* file) {
	unsigned char value;
	return TieStorage_Read(&value, 1, 1, file) == 1 ? value : TIE_EOF;
}

int TieStorage_Putc(int value, TieFile* file) {
	const unsigned char byte = (unsigned char)value;
	return TieStorage_Write(&byte, 1, 1, file) == 1 ? byte : TIE_EOF;
}
