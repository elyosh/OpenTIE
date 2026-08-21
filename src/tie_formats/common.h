#ifndef TIE_FORMATS_COMMON_H
#define TIE_FORMATS_COMMON_H

#include <stddef.h>
#include <stdint.h>

typedef struct TieFormatError {
	int code;
	char message[256];
} TieFormatError;

typedef struct TieRgbaFrame {
	uint8_t* rgba;
	uint16_t width, height;
	int16_t anchor_x, anchor_y;
	uint16_t stable_id;
} TieRgbaFrame;

typedef struct TieRgbaFrames {
	TieRgbaFrame* frames;
	uint16_t count;
} TieRgbaFrames;

#define TIE_FOURCC(a, b, c, d)                                                                               \
	((uint32_t)(uint8_t)(a) << 24 | (uint32_t)(uint8_t)(b) << 16 | (uint32_t)(uint8_t)(c) << 8 |             \
	 (uint32_t)(uint8_t)(d))

void TieRgbaFrames_Free(TieRgbaFrames* frames);

#endif
