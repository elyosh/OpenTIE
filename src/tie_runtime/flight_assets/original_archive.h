#ifndef TIE_ORIGINAL_ARCHIVE_H
#define TIE_ORIGINAL_ARCHIVE_H

#include <stdbool.h>
#include <stddef.h>

#include "aeron/vfs.h"
#include "tie_formats/lfd.h"
#include "tie_runtime/runtime/exports.h"

typedef struct TieOriginalArchive {
	AeronFile* file;
	TieLfdIndex index;
	char path[64];
} TieOriginalArchive;

typedef struct TieOriginalArchiveCache {
	AeronVfs* vfs;
	TieOriginalArchive archives[2][3];
} TieOriginalArchiveCache;

void TieOriginalArchiveCache_Init(TieOriginalArchiveCache* cache, AeronVfs* vfs);
void TieOriginalArchiveCache_Release(TieOriginalArchiveCache* cache);
bool TieOriginalArchiveCache_ReadEntry(TieOriginalArchiveCache* cache, const TieSpeciesLfdLocation* location,
									   uint8_t** out_bytes, size_t* out_size, TieFormatError* error);

#endif
