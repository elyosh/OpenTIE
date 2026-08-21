#ifndef TIE_FORMATS_INTERNAL_H
#define TIE_FORMATS_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "tie_formats/common.h"

#define TIE_FORMAT_MAX_ENTRY_SIZE (64u * 1024u * 1024u)

static inline uint32_t TieFormat_ReadU32Le(const uint8_t* bytes) {
	return (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8 | (uint32_t)bytes[2] << 16 | (uint32_t)bytes[3] << 24;
}

static inline uint16_t TieFormat_ReadU16Le(const uint8_t* bytes) {
	return (uint16_t)bytes[0] | (uint16_t)bytes[1] << 8;
}

static inline int16_t TieFormat_ReadI16Le(const uint8_t* bytes) {
	return (int16_t)TieFormat_ReadU16Le(bytes);
}

static inline uint32_t TieFormat_ReadFourcc(const uint8_t* bytes) {
	return TIE_FOURCC(bytes[0], bytes[1], bytes[2], bytes[3]);
}

bool TieFormat_SetError(TieFormatError* error, int code, const char* format, ...);

#endif
