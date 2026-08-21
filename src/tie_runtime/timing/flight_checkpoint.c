#include "tie_runtime/timing/flight_checkpoint.h"
#include "tie_runtime/timing/flight_timing_state.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "tie/dynamix.h"
#include "tie/move.h"
#include "tie/starship.h"
#include "tie/static.h"
#include "tie/user.h"
#include "tie_runtime/timing/ai_lead.h"
#include "tie_runtime/timing/chase_camera.h"
#include "tie_runtime/timing/flight_timing.h"

enum {
	TIE_FLIGHT_CHECKPOINT_HEADER_SIZE = 40,
	TIE_FLIGHT_CHECKPOINT_VERSION = 3,
};

static void TieCheckpoint_PutU16(uint8_t* out, uint16_t value) {
	out[0] = (uint8_t)value;
	out[1] = (uint8_t)(value >> 8);
}

static void TieCheckpoint_PutU32(uint8_t* out, uint32_t value) {
	out[0] = (uint8_t)value;
	out[1] = (uint8_t)(value >> 8);
	out[2] = (uint8_t)(value >> 16);
	out[3] = (uint8_t)(value >> 24);
}

static uint16_t TieCheckpoint_GetU16(const uint8_t* in) { return (uint16_t)(in[0] | (uint16_t)in[1] << 8); }

static uint32_t TieCheckpoint_GetU32(const uint8_t* in) {
	return (uint32_t)in[0] | (uint32_t)in[1] << 8 | (uint32_t)in[2] << 16 | (uint32_t)in[3] << 24;
}

static size_t TieCheckpoint_PayloadSize(void) {
	return sizeof(TieFlightTimingCheckpoint) + TieFlightTimingState_BlockSize(TIE_FLIGHT_TIMING_BLOCK_MOVE) +
		   TieFlightTimingState_BlockSize(TIE_FLIGHT_TIMING_BLOCK_DYNAMICS) +
		   TieFlightTimingState_BlockSize(TIE_FLIGHT_TIMING_BLOCK_TURRET) +
		   TieFlightTimingState_BlockSize(TIE_FLIGHT_TIMING_BLOCK_STATIC_WEAPON) +
		   TieFlightTimingState_BlockSize(TIE_FLIGHT_TIMING_BLOCK_USER) + TieAiLead_CheckpointSize() +
		   TieChaseCamera_CheckpointSize();
}

size_t TieFlightCheckpoint_Size(void) {
	return TIE_FLIGHT_CHECKPOINT_HEADER_SIZE + TieCheckpoint_PayloadSize();
}

static int TieCheckpoint_WriteBlock(TieFile* file, const void* data, size_t size) {
	return TieStorage_Write(data, 1, size, file) == size;
}

static int TieCheckpoint_ReadBlock(TieFile* file, void* data, size_t size) {
	return TieStorage_Read(data, 1, size, file) == size;
}

int TieFlightCheckpoint_Write(TieFile* file) {
	if (!file)
		return 0;
	const uint32_t move_size = (uint32_t)TieFlightTimingState_BlockSize(TIE_FLIGHT_TIMING_BLOCK_MOVE);
	const uint32_t dynamix_size = (uint32_t)TieFlightTimingState_BlockSize(TIE_FLIGHT_TIMING_BLOCK_DYNAMICS);
	const uint32_t starship_size = (uint32_t)TieFlightTimingState_BlockSize(TIE_FLIGHT_TIMING_BLOCK_TURRET);
	const uint32_t static_size =
		(uint32_t)TieFlightTimingState_BlockSize(TIE_FLIGHT_TIMING_BLOCK_STATIC_WEAPON);
	const uint32_t user_size = (uint32_t)TieFlightTimingState_BlockSize(TIE_FLIGHT_TIMING_BLOCK_USER);
	const uint32_t ai_lead_size = (uint32_t)TieAiLead_CheckpointSize();
	const uint32_t chase_camera_size = (uint32_t)TieChaseCamera_CheckpointSize();
	uint8_t header[TIE_FLIGHT_CHECKPOINT_HEADER_SIZE] = { 'T', 'F', 'S', 'T' };
	TieCheckpoint_PutU16(header + 4, TIE_FLIGHT_CHECKPOINT_VERSION);
	TieCheckpoint_PutU16(header + 6, (uint16_t)sizeof(TieFlightTimingCheckpoint));
	TieCheckpoint_PutU32(header + 8, (uint32_t)TieCheckpoint_PayloadSize());
	TieCheckpoint_PutU32(header + 12, move_size);
	TieCheckpoint_PutU32(header + 16, dynamix_size);
	TieCheckpoint_PutU32(header + 20, starship_size);
	TieCheckpoint_PutU32(header + 24, static_size);
	TieCheckpoint_PutU32(header + 28, user_size);
	TieCheckpoint_PutU32(header + 32, ai_lead_size);
	TieCheckpoint_PutU32(header + 36, chase_camera_size);

	TieFlightTimingCheckpoint timing;
	TieFlightTiming_Save(&timing);
	if (!TieCheckpoint_WriteBlock(file, header, sizeof header) ||
		!TieCheckpoint_WriteBlock(file, &timing, sizeof timing))
		return 0;

	size_t scratch_size = move_size;
	if (dynamix_size > scratch_size)
		scratch_size = dynamix_size;
	if (starship_size > scratch_size)
		scratch_size = starship_size;
	if (static_size > scratch_size)
		scratch_size = static_size;
	if (user_size > scratch_size)
		scratch_size = user_size;
	if (ai_lead_size > scratch_size)
		scratch_size = ai_lead_size;
	if (chase_camera_size > scratch_size)
		scratch_size = chase_camera_size;
	void* scratch = malloc(scratch_size);
	if (!scratch)
		return 0;

	TieFlightTimingState_SaveBlock(TIE_FLIGHT_TIMING_BLOCK_MOVE, scratch);
	int ok = TieCheckpoint_WriteBlock(file, scratch, move_size);
	TieFlightTimingState_SaveBlock(TIE_FLIGHT_TIMING_BLOCK_DYNAMICS, scratch);
	ok = ok && TieCheckpoint_WriteBlock(file, scratch, dynamix_size);
	TieFlightTimingState_SaveBlock(TIE_FLIGHT_TIMING_BLOCK_TURRET, scratch);
	ok = ok && TieCheckpoint_WriteBlock(file, scratch, starship_size);
	TieFlightTimingState_SaveBlock(TIE_FLIGHT_TIMING_BLOCK_STATIC_WEAPON, scratch);
	ok = ok && TieCheckpoint_WriteBlock(file, scratch, static_size);
	TieFlightTimingState_SaveBlock(TIE_FLIGHT_TIMING_BLOCK_USER, scratch);
	ok = ok && TieCheckpoint_WriteBlock(file, scratch, user_size);
	TieAiLead_SaveCheckpoint(scratch);
	ok = ok && TieCheckpoint_WriteBlock(file, scratch, ai_lead_size);
	TieChaseCamera_SaveCheckpoint(scratch);
	ok = ok && TieCheckpoint_WriteBlock(file, scratch, chase_camera_size);
	free(scratch);
	return ok;
}

int TieFlightCheckpoint_Read(TieFile* file) {
	uint8_t header[TIE_FLIGHT_CHECKPOINT_HEADER_SIZE];
	if (!file || !TieCheckpoint_ReadBlock(file, header, sizeof header) || memcmp(header, "TFST", 4) != 0 ||
		TieCheckpoint_GetU16(header + 4) != TIE_FLIGHT_CHECKPOINT_VERSION ||
		TieCheckpoint_GetU16(header + 6) != sizeof(TieFlightTimingCheckpoint) ||
		TieCheckpoint_GetU32(header + 8) != TieCheckpoint_PayloadSize() ||
		TieCheckpoint_GetU32(header + 12) != TieFlightTimingState_BlockSize(TIE_FLIGHT_TIMING_BLOCK_MOVE) ||
		TieCheckpoint_GetU32(header + 16) !=
			TieFlightTimingState_BlockSize(TIE_FLIGHT_TIMING_BLOCK_DYNAMICS) ||
		TieCheckpoint_GetU32(header + 20) != TieFlightTimingState_BlockSize(TIE_FLIGHT_TIMING_BLOCK_TURRET) ||
		TieCheckpoint_GetU32(header + 24) !=
			TieFlightTimingState_BlockSize(TIE_FLIGHT_TIMING_BLOCK_STATIC_WEAPON) ||
		TieCheckpoint_GetU32(header + 28) != TieFlightTimingState_BlockSize(TIE_FLIGHT_TIMING_BLOCK_USER) ||
		TieCheckpoint_GetU32(header + 32) != TieAiLead_CheckpointSize() ||
		TieCheckpoint_GetU32(header + 36) != TieChaseCamera_CheckpointSize())
		return 0;

	const size_t total = TieCheckpoint_PayloadSize();
	uint8_t* payload = (uint8_t*)malloc(total);
	if (!payload || !TieCheckpoint_ReadBlock(file, payload, total)) {
		free(payload);
		return 0;
	}

	size_t offset = 0;
	TieFlightTimingCheckpoint timing;
	memcpy(&timing, payload, sizeof timing);
	offset += sizeof timing;
	int ok = TieFlightTiming_Restore(&timing);
	ok =
		ok && TieFlightTimingState_RestoreBlock(TIE_FLIGHT_TIMING_BLOCK_MOVE, payload + offset,
												TieFlightTimingState_BlockSize(TIE_FLIGHT_TIMING_BLOCK_MOVE));
	offset += TieFlightTimingState_BlockSize(TIE_FLIGHT_TIMING_BLOCK_MOVE);
	ok = ok &&
		 TieFlightTimingState_RestoreBlock(TIE_FLIGHT_TIMING_BLOCK_DYNAMICS, payload + offset,
										   TieFlightTimingState_BlockSize(TIE_FLIGHT_TIMING_BLOCK_DYNAMICS));
	offset += TieFlightTimingState_BlockSize(TIE_FLIGHT_TIMING_BLOCK_DYNAMICS);
	ok = ok &&
		 TieFlightTimingState_RestoreBlock(TIE_FLIGHT_TIMING_BLOCK_TURRET, payload + offset,
										   TieFlightTimingState_BlockSize(TIE_FLIGHT_TIMING_BLOCK_TURRET));
	offset += TieFlightTimingState_BlockSize(TIE_FLIGHT_TIMING_BLOCK_TURRET);
	ok = ok && TieFlightTimingState_RestoreBlock(
				   TIE_FLIGHT_TIMING_BLOCK_STATIC_WEAPON, payload + offset,
				   TieFlightTimingState_BlockSize(TIE_FLIGHT_TIMING_BLOCK_STATIC_WEAPON));
	offset += TieFlightTimingState_BlockSize(TIE_FLIGHT_TIMING_BLOCK_STATIC_WEAPON);
	ok =
		ok && TieFlightTimingState_RestoreBlock(TIE_FLIGHT_TIMING_BLOCK_USER, payload + offset,
												TieFlightTimingState_BlockSize(TIE_FLIGHT_TIMING_BLOCK_USER));
	offset += TieFlightTimingState_BlockSize(TIE_FLIGHT_TIMING_BLOCK_USER);
	ok = ok && TieAiLead_RestoreCheckpoint(payload + offset, TieAiLead_CheckpointSize());
	offset += TieAiLead_CheckpointSize();
	ok = ok && TieChaseCamera_RestoreCheckpoint(payload + offset, TieChaseCamera_CheckpointSize());
	free(payload);
	return ok;
}
