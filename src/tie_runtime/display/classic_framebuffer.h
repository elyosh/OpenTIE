#ifndef TIE_RUNTIME_DISPLAY_CLASSIC_FRAMEBUFFER_H
#define TIE_RUNTIME_DISPLAY_CLASSIC_FRAMEBUFFER_H

#include <stdbool.h>
#include <stdint.h>

typedef struct TieFramebuffer {
	const uint8_t* pixels;
	int width;
	int height;
	int pitch;
	uint32_t generation;
	const uint8_t* palette;
} TieFramebuffer;

const TieFramebuffer* TieClassicFramebuffer_Current(void);
const TieFramebuffer* TieClassicFramebuffer_PresentedVga(void);
bool TieClassicFramebuffer_TakeDirty(void);
void TieClassicFramebuffer_CapturePresentedVga(void);
void TieClassicFramebuffer_InvalidatePresentedVga(void);
void TieClassicFramebuffer_SetPalette(const uint8_t* rgb, int start, int count);

#endif
