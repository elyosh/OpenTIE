/* Tool-side owner for an RMAP-based LucasArts LFD file. */
#ifndef TIE_TOOLS_LFD_FILE_H
#define TIE_TOOLS_LFD_FILE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "tie_formats/lfd.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef TieLfdEntry TieLfdFileEntry;

typedef struct TieLfdFile {
	TieLfdFileEntry* entries;
	uint32_t count;
	uint8_t* data;
	size_t size;
} TieLfdFile;

bool TieLfdFile_Open(TieLfdFile* lfd, const char* path, char* error, size_t error_capacity);
void TieLfdFile_Close(TieLfdFile* lfd);

const TieLfdFileEntry* TieLfdFile_Find(const TieLfdFile* lfd, uint32_t type, const char* name);
const uint8_t* TieLfdFile_Data(const TieLfdFile* lfd, const TieLfdFileEntry* entry);

typedef struct TieLfdFileChain {
	const TieLfdFile** files;
	int count;
} TieLfdFileChain;

const TieLfdFileEntry* TieLfdFileChain_Find(const TieLfdFileChain* chain, uint32_t type, const char* name,
											const TieLfdFile** out_owner);

#ifdef __cplusplus
}
#endif

#endif
