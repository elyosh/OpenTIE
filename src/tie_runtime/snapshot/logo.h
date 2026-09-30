#ifndef TIE_RUNTIME_SNAPSHOT_LOGO_H
#define TIE_RUNTIME_SNAPSHOT_LOGO_H

#include <landru/actor.h>
#include <landru/render.h>

void TieLogoSnapshot_Reset(void);
void TieLogoSnapshot_Stamp(Actor* actor);
int TieLogoSnapshot_ReadActors(LandruActorRenderState* actors, int capacity);

#endif
