#ifndef TIE_RUNTIME_TIMING_FLIGHT_TIMING_STATE_H
#define TIE_RUNTIME_TIMING_FLIGHT_TIMING_STATE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "tie/tie.h"

typedef struct TieMoveTimingState {
	uint16_t idnumber;
	int64_t position_remainder[3];
	int32_t homing_remainder[3];
	int32_t spin_remainder[2];
	int16_t spin_sign;
	int32_t push_remainder[3];
	int8_t push_sign[3];
	int8_t homing_speed_sign;
} TieMoveTimingState;

typedef struct TieDynamicsTimingState {
	uint16_t idnumber;
	int16_t velocity_direction;
	int32_t velocity_remainder;
	uint64_t autopilot_remainder[3];
} TieDynamicsTimingState;

typedef struct TieStaticWeaponTimingState {
	uint16_t idnumber;
	uint8_t remainder;
} TieStaticWeaponTimingState;

typedef struct TieTurretTimingState {
	uint16_t idnumber;
	uint8_t remainder;
	uint8_t divisor;
} TieTurretTimingState;

typedef struct TieUserTimingState {
	uint16_t player_idnumber;
	int32_t view_remainder[2];
	int32_t flight_axis_remainder[3];
	int32_t zoom_remainder;
	uint16_t zoom_rate_remainder;
	uint16_t slew_remainder[3];
	int8_t slew_sign[3];
	uint16_t throttle_remainder[2];
	int8_t throttle_sign[2];
} TieUserTimingState;

typedef enum TieFlightTimingBlock {
	TIE_FLIGHT_TIMING_BLOCK_MOVE,
	TIE_FLIGHT_TIMING_BLOCK_DYNAMICS,
	TIE_FLIGHT_TIMING_BLOCK_TURRET,
	TIE_FLIGHT_TIMING_BLOCK_STATIC_WEAPON,
	TIE_FLIGHT_TIMING_BLOCK_USER,
} TieFlightTimingBlock;

void TieFlightTimingState_Reset(void);
TieMoveTimingState* TieFlightTimingState_Move(uint16_t object_index, const FlightObject* object);
TieDynamicsTimingState* TieFlightTimingState_Dynamics(uint16_t object_index);
uint32_t TieFlightTimingState_AccumulateVelocityDelta(uint16_t object_index, uint16_t rate, int direction,
													  uint16_t elapsed_ticks);
TieStaticWeaponTimingState* TieFlightTimingState_StaticWeapon(uint16_t slot, uint16_t idnumber);
TieTurretTimingState* TieFlightTimingState_Turret(uint16_t craft_index, uint16_t weapon_slot,
												  uint16_t idnumber, uint8_t divisor);
TieUserTimingState* TieFlightTimingState_User(void);

size_t TieFlightTimingState_BlockSize(TieFlightTimingBlock block);
void TieFlightTimingState_SaveBlock(TieFlightTimingBlock block, void* destination);
bool TieFlightTimingState_RestoreBlock(TieFlightTimingBlock block, const void* source, size_t size);

#endif
