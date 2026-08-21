#ifndef TIE_REMASTER_SCENE2D_TYPES_H
#define TIE_REMASTER_SCENE2D_TYPES_H

#include "aeron/scene/sprite_atlas.h"

typedef AeronSpriteRect TieScene2dRect;

typedef struct TieScene2dUvRect {
	float u0, v0, u1, v1;
} TieScene2dUvRect;

typedef struct TieScene2dRgba {
	float r, g, b, a;
} TieScene2dRgba;

typedef enum TieScene2dFlip {
	TIE_SCENE2D_FLIP_H = 0x1,
	TIE_SCENE2D_FLIP_V = 0x2,
} TieScene2dFlip;

#endif
