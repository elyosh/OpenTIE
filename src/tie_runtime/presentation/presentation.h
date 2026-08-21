#ifndef TIE_REMASTER_PRESENTATION_H
#define TIE_REMASTER_PRESENTATION_H

#include <stdint.h>

#include "aeron/aeron.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TIE_PRESENTATION_INITIAL_WIDTH 1440
#define TIE_PRESENTATION_LOGICAL_HEIGHT 1080

typedef struct TieAspectRatio {
	int width;
	int height;
} TieAspectRatio;

typedef struct TiePresentationLayout {
	AeronRectI frame;
	AeronRectI modern;
	AeronRectI classic;
	AeronRectI split_scissor;
	int render_width;
	int render_height;
	uint32_t render_generation;
} TiePresentationLayout;

typedef enum TiePresentationChange {
	TIE_PRESENTATION_CHANGE_NONE = 0,
	TIE_PRESENTATION_CHANGE_LAYOUT = 1 << 0,
	TIE_PRESENTATION_CHANGE_RENDER_SIZE = 1 << 1,
} TiePresentationChange;

int TiePresentation_Init(TieAspectRatio modern_aspect);
int TiePresentation_SetModernAspect(TieAspectRatio modern_aspect);
TiePresentationChange TiePresentation_BeginFrame(const AeronInputSnapshot* input, int32_t delta_us);
const TiePresentationLayout* TiePresentation_Layout(void);
TieAspectRatio TiePresentation_ModernAspect(void);
void TiePresentation_FromClassic(float classic_x, float classic_y, int classic_width, int classic_height,
								 float* logical_x, float* logical_y);
void TiePresentation_Shutdown(void);

#ifdef __cplusplus
}
#endif

#endif
