#include "tie_runtime/runtime/replay_format.h"

#include <string.h>

#include "tie_runtime/timing/flight_timing.h"

/* All replay-file I/O goes through TieStorage_Read/fwrite — the
 * library never touches stdio directly. */

int TieReplayFormat_WriteHeader(TieFile* fp) {
	TieFlightTimingCheckpoint timing;
	TieFlightTiming_Save(&timing);
	const uint16_t flags = (TieProfile_UsesTie98Logic() ? 1u : 0u) | (uint16_t)timing.requested_rate
																		 << REPLAY_FORMAT_UPDATE_RATE_SHIFT;
	uint8_t hdr[REPLAY_FORMAT_HEADER_SIZE] = {
		REPLAY_FORMAT_MAGIC0,
		REPLAY_FORMAT_MAGIC1,
		REPLAY_FORMAT_MAGIC2,
		REPLAY_FORMAT_MAGIC3,
		(uint8_t)(REPLAY_FORMAT_VERSION & 0xFF),
		(uint8_t)((REPLAY_FORMAT_VERSION >> 8) & 0xFF),
		(uint8_t)(flags & 0xFF),
		(uint8_t)(flags >> 8),
		(uint8_t)(REPLAY_FORMAT_FRAME_SIZE & 0xFF),
		(uint8_t)((REPLAY_FORMAT_FRAME_SIZE >> 8) & 0xFF),
		(uint8_t)(REPLAY_FORMAT_HEADER_SIZE & 0xFF),
		(uint8_t)((REPLAY_FORMAT_HEADER_SIZE >> 8) & 0xFF),
		timing.step_ticks,
		timing.compatibility_ticks,
		0u,
		0u /* reserved */
	};
	return TieStorage_Write(hdr, 1u, REPLAY_FORMAT_HEADER_SIZE, fp) == REPLAY_FORMAT_HEADER_SIZE;
}

int TieReplayFormat_ReadHeader(TieFile* fp, TieReplayFormatMetadata* metadata) {
	uint8_t hdr[REPLAY_FORMAT_HEADER_SIZE];
	if (TieStorage_Read(hdr, 1u, REPLAY_FORMAT_HEADER_SIZE, fp) != REPLAY_FORMAT_HEADER_SIZE)
		return 0;

	if (hdr[0] != REPLAY_FORMAT_MAGIC0 || hdr[1] != REPLAY_FORMAT_MAGIC1 || hdr[2] != REPLAY_FORMAT_MAGIC2 ||
		hdr[3] != REPLAY_FORMAT_MAGIC3)
		return 0;

	uint16_t version = (uint16_t)(hdr[4] | (uint16_t)hdr[5] << 8);
	uint16_t flags = (uint16_t)(hdr[6] | (uint16_t)hdr[7] << 8);
	uint16_t frame_size = (uint16_t)(hdr[8] | (uint16_t)hdr[9] << 8);
	const uint16_t update_rate = (flags & REPLAY_FORMAT_UPDATE_RATE_MASK) >> REPLAY_FORMAT_UPDATE_RATE_SHIFT;

	if (version < REPLAY_FORMAT_MIN_READ_VERSION || version > REPLAY_FORMAT_VERSION)
		return 0;
	if (frame_size != REPLAY_FORMAT_FRAME_SIZE)
		return 0;
	if ((flags & REPLAY_FORMAT_FLIGHT_VERSION_MASK) != (TieProfile_UsesTie98Logic() ? 1u : 0u))
		return 0;
	if (update_rate > TIE_FLIGHT_UPDATE_RATE_UNLOCKED || (hdr[12] != 1u && hdr[12] != 4u && hdr[12] != 7u) ||
		(hdr[13] != 4u && hdr[13] != 7u) || hdr[12] > hdr[13])
		return 0;
	const uint8_t expected_compatibility_ticks = update_rate == TIE_FLIGHT_UPDATE_RATE_TIE95         ? 4u
												 : (flags & REPLAY_FORMAT_FLIGHT_VERSION_MASK) == 1u ? 7u
																									 : 4u;
	const uint8_t expected_step_ticks =
		update_rate == TIE_FLIGHT_UPDATE_RATE_UNLOCKED ? 1u : expected_compatibility_ticks;
	if (hdr[12] != expected_step_ticks || hdr[13] != expected_compatibility_ticks)
		return 0;
	if (metadata) {
		metadata->version = version;
		metadata->flight_version = (flags & REPLAY_FORMAT_FLIGHT_VERSION_MASK) == 1u ? TIE_GAME_VERSION_TIE98
																					 : TIE_GAME_VERSION_TIE95;
		metadata->update_rate = (TieFlightUpdateRate)update_rate;
		metadata->step_ticks = hdr[12];
		metadata->compatibility_ticks = hdr[13];
	}
	return 1;
}
