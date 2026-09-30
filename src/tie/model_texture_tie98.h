#ifndef TIE_MODEL_TEXTURE_TIE98_H
#define TIE_MODEL_TEXTURE_TIE98_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void ModelTexture_BuildPalettedShadeTable(uint8_t* dst, const uint8_t* rgb24, int width, int height);

#ifdef __cplusplus
}
#endif

#endif
