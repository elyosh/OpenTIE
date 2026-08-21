#include "tie_runtime/flight_assets/original_archive.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t TieOriginalArchive_ArchiveU32le(const uint8_t* bytes) {
	return (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8 | (uint32_t)bytes[2] << 16 | (uint32_t)bytes[3] << 24;
}

static uint32_t TieOriginalArchive_ArchiveFourcc(const uint8_t* bytes) {
	return TIE_FOURCC(bytes[0], bytes[1], bytes[2], bytes[3]);
}

static bool TieOriginalArchive_ArchiveError(TieFormatError* error, int code, const char* format, ...) {
	if (error) {
		va_list args;
		error->code = code;
		va_start(args, format);
		vsnprintf(error->message, sizeof error->message, format, args);
		va_end(args);
	}
	return false;
}

void TieOriginalArchiveCache_Init(TieOriginalArchiveCache* cache, AeronVfs* vfs) {
	if (!cache)
		return;
	memset(cache, 0, sizeof *cache);
	cache->vfs = vfs;
}

void TieOriginalArchiveCache_Release(TieOriginalArchiveCache* cache) {
	if (!cache)
		return;
	for (int resolution = 0; resolution < 2; ++resolution) {
		for (int file = 0; file < 3; ++file) {
			TieOriginalArchive* archive = &cache->archives[resolution][file];
			if (archive->file)
				AeronVfs_Close(archive->file);
			TieLfdIndex_Free(&archive->index);
		}
	}
	memset(cache, 0, sizeof *cache);
}

static bool TieOriginalArchive_ArchiveReadExact(AeronFile* file, void* destination, size_t size) {
	size_t read_size = 0;
	return AeronVfs_Read(file, destination, size, &read_size) && read_size == size;
}

static bool TieOriginalArchive_ArchiveOpen(TieOriginalArchiveCache* cache,
										   TieSpeciesLfdResourceSet resource_set, TieSpeciesLfdFile lfd_file,
										   TieFormatError* error) {
	static const char* const directories[] = { "RES320", "RES640" };
	static const char* const names[] = { "SPECIES.LFD", "SPECIES2.LFD", "SPECIES3.LFD" };
	TieOriginalArchive* archive = &cache->archives[resource_set][lfd_file];
	if (archive->file)
		return true;
	snprintf(archive->path, sizeof archive->path, "%s/%s", directories[resource_set], names[lfd_file]);
	if (!AeronVfs_Open(cache->vfs, AERON_VFS_ROOT_ASSET, archive->path, AERON_VFS_READ, &archive->file))
		return TieOriginalArchive_ArchiveError(error, 10, "cannot open %s", archive->path);
	const int64_t file_size = AeronVfs_GetSize(archive->file);
	uint8_t header[16];
	if (file_size < 16 || file_size > 256 * 1024 * 1024 || !AeronVfs_Seek(archive->file, 0, 0) ||
		!TieOriginalArchive_ArchiveReadExact(archive->file, header, sizeof header))
		goto read_failed;
	const uint32_t directory_size = TieOriginalArchive_ArchiveU32le(header + 12);
	if (directory_size > (uint32_t)file_size - 16)
		goto read_failed;
	uint8_t* directory = malloc((size_t)directory_size + 16);
	if (!directory) {
		TieOriginalArchive_ArchiveError(error, 11, "allocation failed for %s RMAP", archive->path);
		goto failed;
	}
	memcpy(directory, header, 16);
	if (!TieOriginalArchive_ArchiveReadExact(archive->file, directory + 16, directory_size) ||
		!TieLfdIndex_Parse(directory, (size_t)directory_size + 16, (uint64_t)file_size, &archive->index,
						   error)) {
		free(directory);
		goto failed;
	}
	free(directory);
	return true;

read_failed:
	TieOriginalArchive_ArchiveError(error, 12, "cannot read %s RMAP", archive->path);
failed:
	AeronVfs_Close(archive->file);
	archive->file = NULL;
	return false;
}

bool TieOriginalArchiveCache_ReadEntry(TieOriginalArchiveCache* cache, const TieSpeciesLfdLocation* location,
									   uint8_t** out_bytes, size_t* out_size, TieFormatError* error) {
	if (out_bytes)
		*out_bytes = NULL;
	if (out_size)
		*out_size = 0;
	if (error)
		memset(error, 0, sizeof *error);
	if (!cache || !cache->vfs || !location || !out_bytes || !out_size ||
		location->resource_set > TIE_SPECIES_LFD_RES640 || location->lfd_file > TIE_SPECIES_LFD_SPECIES3)
		return TieOriginalArchive_ArchiveError(error, 1, "invalid original archive request");
	if (!TieOriginalArchive_ArchiveOpen(cache, (TieSpeciesLfdResourceSet)location->resource_set,
										(TieSpeciesLfdFile)location->lfd_file, error))
		return false;
	TieOriginalArchive* archive = &cache->archives[location->resource_set][location->lfd_file];
	if (location->entry >= archive->index.count)
		return TieOriginalArchive_ArchiveError(error, 2, "%s has no entry %u", archive->path,
											   location->entry);
	const TieLfdEntry* entry = &archive->index.entries[location->entry];
	uint8_t chunk_header[16];
	if (entry->payload_offset < 16 ||
		!AeronVfs_Seek(archive->file, (int64_t)(entry->payload_offset - 16), 0) ||
		!TieOriginalArchive_ArchiveReadExact(archive->file, chunk_header, sizeof chunk_header) ||
		TieOriginalArchive_ArchiveFourcc(chunk_header) != entry->type ||
		memcmp(chunk_header + 4, entry->name, 8) != 0 ||
		TieOriginalArchive_ArchiveU32le(chunk_header + 12) != entry->size)
		return TieOriginalArchive_ArchiveError(error, 3, "%s entry %u no longer matches its RMAP",
											   archive->path, location->entry);
	uint8_t* bytes = malloc(entry->size ? entry->size : 1);
	if (!bytes)
		return TieOriginalArchive_ArchiveError(error, 4, "allocation failed for %s entry %u", archive->path,
											   location->entry);
	if (!TieOriginalArchive_ArchiveReadExact(archive->file, bytes, entry->size)) {
		free(bytes);
		return TieOriginalArchive_ArchiveError(error, 5, "short read in %s entry %u", archive->path,
											   location->entry);
	}
	*out_bytes = bytes;
	*out_size = entry->size;
	return true;
}
