#include "render_internal.h"

#include <string.h>

static LandruRenderSink s_sink;
static bool s_sink_set;

bool landru_set_render_sink(const LandruRenderSink* sink) {
	if (!sink) {
		s_sink_set = false;
		memset(&s_sink, 0, sizeof s_sink);
		return true;
	}
	if (!sink->actor || !sink->draw || !sink->film || !sink->text || !sink->paint || !sink->cursor ||
		!sink->fade || !sink->scene_clock)
		return false;
	s_sink = *sink;
	s_sink_set = true;
	return true;
}

void landru_render_actor(const LandruActorRenderState* state) {
	if (s_sink_set)
		s_sink.actor(s_sink.userdata, state);
}

void landru_render_draw(const LandruDrawCommand* command) {
	if (s_sink_set)
		s_sink.draw(s_sink.userdata, command);
}

void landru_render_film(const LandruFilmRenderState* state) {
	if (s_sink_set)
		s_sink.film(s_sink.userdata, state);
}

void landru_render_text(const LandruTextCommand* command) {
	if (s_sink_set)
		s_sink.text(s_sink.userdata, command);
}

void landru_render_paint(const LandruPaintCommand* command) {
	if (s_sink_set)
		s_sink.paint(s_sink.userdata, command);
}

void landru_render_cursor(const LandruCursorRenderState* state) {
	if (s_sink_set)
		s_sink.cursor(s_sink.userdata, state);
}

void landru_render_fade(const LandruFadeRenderState* state) {
	if (s_sink_set)
		s_sink.fade(s_sink.userdata, state);
}

void landru_render_scene_clock(int32_t frame, float progress, uint32_t period_us) {
	if (s_sink_set)
		s_sink.scene_clock(s_sink.userdata, frame, progress, period_us);
}

bool landru_render_is_scene_transition(void) {
	return s_sink_set && s_sink.is_scene_transition ? s_sink.is_scene_transition(s_sink.userdata) : false;
}
