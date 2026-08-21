#ifndef TIE_FORMATS_LFD_H
#define TIE_FORMATS_LFD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "tie_formats/common.h"

typedef struct TieLfdEntry {
	uint32_t type;
	char name[9];
	uint32_t size;
	uint64_t payload_offset;
} TieLfdEntry;

typedef struct TieLfdIndex {
	TieLfdEntry* entries;
	uint32_t count;
	uint64_t file_size;
} TieLfdIndex;

bool TieLfdIndex_Parse(const void* header_and_directory, size_t available_size, uint64_t declared_file_size,
					   TieLfdIndex* out, TieFormatError* error);
void TieLfdIndex_Free(TieLfdIndex* index);

#endif
