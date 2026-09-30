#include "tie_runtime/snapshot/logo.h"
#include "tie/tielogo.h"

#include <string.h>

enum { TIELOGO_MAX_STAMPS = 96 };
static LandruActorRenderState s_stamps[TIELOGO_MAX_STAMPS];
static int s_stamp_count;

void TieLogoSnapshot_Reset(void) { s_stamp_count = 0; }

void TieLogoSnapshot_Stamp(Actor* actor) {
	if (s_stamp_count >= TIELOGO_MAX_STAMPS)
		return;
	LandruActorRenderState* state = &s_stamps[s_stamp_count++];
	xactor_fill_render_state(actor, state);
	state->flags |= LANDRU_ACTOR_RENDER_VISIBLE;
}

int TieLogoSnapshot_ReadActors(LandruActorRenderState* actors, int capacity) {
	if (!actors || capacity <= 0)
		return 0;
	int count = s_stamp_count < capacity ? s_stamp_count : capacity;
	memcpy(actors, s_stamps, (size_t)count * sizeof *actors);
	if (!fighter2_actor || count == capacity)
		return count;

	const int16_t saved_state = fighter2_actor->state;
	for (int index = 0; index < 32 && count < capacity; ++index) {
		if (tielogo_fight_state[index] < 0)
			continue;
		LandruActorRenderState* output = &actors[count++];
		memset(output, 0, sizeof *output);
		fighter2_actor->state = (int16_t)index;
		xactor_fill_render_state(fighter2_actor, output);
		output->flags |= LANDRU_ACTOR_RENDER_VISIBLE;
		output->x = (int16_t)(output->x + tielogo_fight_x[index]);
		output->y = (int16_t)(output->y + tielogo_fight_y[index]);
	}
	fighter2_actor->state = saved_state;
	return count;
}
