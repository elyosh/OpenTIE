#ifndef LANDRU_RENDER_INTERNAL_H
#define LANDRU_RENDER_INTERNAL_H

#include <landru/render.h>

void landru_render_actor(const LandruActorRenderState* state);
void landru_render_draw(const LandruDrawCommand* command);
void landru_render_film(const LandruFilmRenderState* state);
void landru_render_text(const LandruTextCommand* command);
void landru_render_paint(const LandruPaintCommand* command);
void landru_render_cursor(const LandruCursorRenderState* state);
void landru_render_fade(const LandruFadeRenderState* state);
void landru_render_scene_clock(int32_t frame, float progress, uint32_t period_us);
bool landru_render_is_scene_transition(void);

#endif
