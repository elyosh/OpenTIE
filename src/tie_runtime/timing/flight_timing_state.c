#include "tie_runtime/timing/flight_timing_state.h"

#include <string.h>

static TieMoveTimingState s_move[NUM_OBJECTS];
static TieDynamicsTimingState s_dynamics[NUM_CRAFTS];
static TieTurretTimingState s_turrets[NUM_CRAFTS][16];
static TieStaticWeaponTimingState s_static_weapons[NUM_STATIC_OBJECTS];
static TieUserTimingState s_user;

void TieFlightTimingState_Reset(void) {
	memset(s_move, 0, sizeof s_move);
	memset(s_dynamics, 0, sizeof s_dynamics);
	memset(s_turrets, 0, sizeof s_turrets);
	memset(s_static_weapons, 0, sizeof s_static_weapons);
	memset(&s_user, 0, sizeof s_user);
}

TieMoveTimingState* TieFlightTimingState_Move(uint16_t object_index, const FlightObject* object) {
	TieMoveTimingState* state = &s_move[object_index];
	if (state->idnumber != object->idnumber) {
		memset(state, 0, sizeof *state);
		state->idnumber = object->idnumber;
	}
	return state;
}

TieDynamicsTimingState* TieFlightTimingState_Dynamics(uint16_t object_index) {
	TieDynamicsTimingState* state = &s_dynamics[object_index];
	if (state->idnumber != objects[object_index].idnumber) {
		memset(state, 0, sizeof *state);
		state->idnumber = objects[object_index].idnumber;
	}
	return state;
}

uint32_t TieFlightTimingState_AccumulateVelocityDelta(uint16_t object_index, uint16_t rate, int direction,
													  uint16_t elapsed_ticks) {
	TieDynamicsTimingState* state = TieFlightTimingState_Dynamics(object_index);
	if (state->velocity_direction != direction) {
		state->velocity_direction = (int16_t)direction;
		state->velocity_remainder = 0;
	}
	const uint64_t numerator = (uint64_t)rate * elapsed_ticks * 65536u + (uint32_t)state->velocity_remainder;
	state->velocity_remainder = (int32_t)(numerator % 236u);
	return (uint32_t)(numerator / 236u);
}

TieStaticWeaponTimingState* TieFlightTimingState_StaticWeapon(uint16_t slot, uint16_t idnumber) {
	TieStaticWeaponTimingState* state = &s_static_weapons[slot];
	if (state->idnumber != idnumber) {
		memset(state, 0, sizeof *state);
		state->idnumber = idnumber;
	}
	return state;
}

TieTurretTimingState* TieFlightTimingState_Turret(uint16_t craft_index, uint16_t weapon_slot,
												  uint16_t idnumber, uint8_t divisor) {
	TieTurretTimingState* state = &s_turrets[craft_index][weapon_slot];
	if (state->idnumber != idnumber || state->divisor != divisor)
		*state = (TieTurretTimingState) { .idnumber = idnumber, .divisor = divisor };
	return state;
}

TieUserTimingState* TieFlightTimingState_User(void) {
	if (pstate.player && s_user.player_idnumber != pstate.player->idnumber) {
		memset(&s_user, 0, sizeof s_user);
		s_user.player_idnumber = pstate.player->idnumber;
	}
	return &s_user;
}

static void* TieFlightTimingState_BlockData(TieFlightTimingBlock block, size_t* size) {
	switch (block) {
		case TIE_FLIGHT_TIMING_BLOCK_MOVE:
			*size = sizeof s_move;
			return s_move;
		case TIE_FLIGHT_TIMING_BLOCK_DYNAMICS:
			*size = sizeof s_dynamics;
			return s_dynamics;
		case TIE_FLIGHT_TIMING_BLOCK_TURRET:
			*size = sizeof s_turrets;
			return s_turrets;
		case TIE_FLIGHT_TIMING_BLOCK_STATIC_WEAPON:
			*size = sizeof s_static_weapons;
			return s_static_weapons;
		case TIE_FLIGHT_TIMING_BLOCK_USER:
			*size = sizeof s_user;
			return &s_user;
	}
	*size = 0;
	return NULL;
}

size_t TieFlightTimingState_BlockSize(TieFlightTimingBlock block) {
	size_t size;
	TieFlightTimingState_BlockData(block, &size);
	return size;
}

void TieFlightTimingState_SaveBlock(TieFlightTimingBlock block, void* destination) {
	size_t size;
	void* data = TieFlightTimingState_BlockData(block, &size);
	if (data && destination)
		memcpy(destination, data, size);
}

bool TieFlightTimingState_RestoreBlock(TieFlightTimingBlock block, const void* source, size_t size) {
	size_t expected;
	void* data = TieFlightTimingState_BlockData(block, &expected);
	if (!data || !source || size != expected)
		return false;
	memcpy(data, source, size);
	return true;
}
