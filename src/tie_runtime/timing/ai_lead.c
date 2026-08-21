#include "tie_runtime/timing/ai_lead.h"

#include <limits.h>
#include <string.h>

#include "tie/tie.h"
#include "tie_runtime/timing/flight_timing.h"

/* Runtime history used to express target motion over one compatibility interval. */
typedef struct TieAiLeadHistoryEntry {
	uint16_t idnumber;
	int32_t world_x;
	int32_t world_y;
	int32_t world_z;
} TieAiLeadHistoryEntry;

typedef struct TieAiLeadState {
	TieAiLeadHistoryEntry objects[NUM_OBJECTS];
	uint32_t elapsed_since_ai_ticks;
} TieAiLeadState;

static TieAiLeadState s_ai_lead_timing;

void TieAiLead_Reset(void) {
	memset(&s_ai_lead_timing, 0, sizeof s_ai_lead_timing);
	for (uint16_t i = 0; i < NUM_OBJECTS; ++i) {
		if (!objects[i].ship_idx)
			continue;
		s_ai_lead_timing.objects[i] = (TieAiLeadHistoryEntry) {
			.idnumber = objects[i].idnumber,
			.world_x = objects[i].world_x,
			.world_y = objects[i].world_y,
			.world_z = objects[i].world_z,
		};
	}
}

void TieAiLead_Advance(uint16_t elapsed_ticks) {
	if (!TieFlightTiming_IsHighRate())
		return;
	if (UINT32_MAX - s_ai_lead_timing.elapsed_since_ai_ticks < elapsed_ticks)
		s_ai_lead_timing.elapsed_since_ai_ticks = UINT32_MAX;
	else
		s_ai_lead_timing.elapsed_since_ai_ticks += elapsed_ticks;
}

void TieAiLead_CommitBoundary(void) {
	if (!TieFlightTiming_IsHighRate())
		return;
	for (uint16_t i = 0; i < NUM_OBJECTS; ++i) {
		TieAiLeadHistoryEntry* history = &s_ai_lead_timing.objects[i];
		if (!objects[i].ship_idx) {
			memset(history, 0, sizeof *history);
			continue;
		}
		*history = (TieAiLeadHistoryEntry) {
			.idnumber = objects[i].idnumber,
			.world_x = objects[i].world_x,
			.world_y = objects[i].world_y,
			.world_z = objects[i].world_z,
		};
	}
	s_ai_lead_timing.elapsed_since_ai_ticks = 0;
}

bool TieAiLead_GetDisplacement(uint16_t target_obj_idx, int32_t* dx, int32_t* dy, int32_t* dz) {
	if (!TieFlightTiming_IsHighRate() || target_obj_idx >= NUM_OBJECTS || !dx || !dy || !dz)
		return false;

	const TieAiLeadHistoryEntry* history = &s_ai_lead_timing.objects[target_obj_idx];
	if (history->idnumber != objects[target_obj_idx].idnumber || !s_ai_lead_timing.elapsed_since_ai_ticks)
		return false;

	const int64_t interval = s_ai_lead_timing.elapsed_since_ai_ticks;
	const int64_t compatibility_ticks = TieFlightTiming_CompatibilityTicks();
	*dx = (int32_t)(((int64_t)objects[target_obj_idx].world_x - history->world_x) * compatibility_ticks /
					interval);
	*dy = (int32_t)(((int64_t)objects[target_obj_idx].world_y - history->world_y) * compatibility_ticks /
					interval);
	*dz = (int32_t)(((int64_t)objects[target_obj_idx].world_z - history->world_z) * compatibility_ticks /
					interval);
	return true;
}

size_t TieAiLead_CheckpointSize(void) { return sizeof s_ai_lead_timing; }

void TieAiLead_SaveCheckpoint(void* destination) {
	if (destination)
		memcpy(destination, &s_ai_lead_timing, sizeof s_ai_lead_timing);
}

bool TieAiLead_RestoreCheckpoint(const void* source, size_t size) {
	if (!source || size != sizeof s_ai_lead_timing)
		return false;
	memcpy(&s_ai_lead_timing, source, sizeof s_ai_lead_timing);
	return true;
}
