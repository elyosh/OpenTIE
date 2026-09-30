#ifndef TIE_FORMATS_STRING_TABLE_H
#define TIE_FORMATS_STRING_TABLE_H

#include "tie_formats/common.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Validated view borrowing an immutable STRINGS.DAT blob. */
typedef struct TieStringIndex {
	const uint8_t* data;
	size_t count;
} TieStringIndex;

bool TieStringIndex_Parse(const void* data, size_t size, TieStringIndex* out, TieFormatError* error);
const char* TieStringIndex_At(const TieStringIndex* index, size_t cell);

#ifdef __cplusplus
}
#endif

#endif
