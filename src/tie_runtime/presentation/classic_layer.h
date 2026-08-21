#ifndef TIE_RUNTIME_PRESENTATION_CLASSIC_LAYER_H
#define TIE_RUNTIME_PRESENTATION_CLASSIC_LAYER_H

#include <stdbool.h>

struct TieFramebuffer;

void TieClassicLayer_Init(void);
void TieClassicLayer_SetSuppressed(bool suppressed);
void TieClassicLayer_SyncInputExtent(void);
void TieClassicLayer_Submit(const struct TieFramebuffer* framebuffer);
void TieClassicLayer_Shutdown(void);

#endif
