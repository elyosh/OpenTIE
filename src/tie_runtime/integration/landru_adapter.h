#ifndef TIE_LANDRU_BRIDGE_H
#define TIE_LANDRU_BRIDGE_H

#include <stdbool.h>

#include <landru/render.h>

bool TieLandruAdapter_Init(void);
void TieLandruAdapter_Shutdown(void);
void TieLandruAdapter_EmitRenderState(void);
bool TieLandruAdapter_EmitActorState(const LandruActorRenderState* state);

#endif
