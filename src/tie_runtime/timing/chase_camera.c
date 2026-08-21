#include "tie_runtime/timing/chase_camera.h"

#include <stdint.h>
#include <string.h>

#include "tie/static.h"
#include "tie/tie.h"
#include "tie_runtime/timing/flight_timing.h"

/* Runtime timestamps make the recovered camera ring buffer time-based. */
typedef struct TieChaseCameraState {
	uint64_t sample_ticks[60];
	uint64_t current_ticks;
	uint16_t target_obj;
	uint8_t initialized;
} TieChaseCameraState;

static TieChaseCameraState s_chase_camera_timing;

static void TieChaseCamera_GetTargetAngles(int16_t* roll, int16_t* heading, int16_t* pitch) {
	if (camera.view_target_obj >= OBJ_REF_STATIC_BASE) {
		const StaticObject* object = &staticobjects[camera.view_target_obj - OBJ_REF_STATIC_BASE];
		*roll = (int16_t)((uint16_t)object->roll_byte << 8);
		*heading = (int16_t)((uint16_t)object->yaw_byte << 8);
		*pitch = (int16_t)((uint16_t)object->pitch_byte << 8);
	} else {
		const FlightObject* object = &objects[camera.view_target_obj];
		*roll = object->roll;
		*heading = object->heading;
		*pitch = object->pitch;
	}
}

void TieChaseCamera_Reset(void) { memset(&s_chase_camera_timing, 0, sizeof s_chase_camera_timing); }

void TieChaseCamera_Update(void) {
	int16_t target_roll;
	int16_t target_heading;
	int16_t target_pitch;
	TieChaseCamera_GetTargetAngles(&target_roll, &target_heading, &target_pitch);

	if (!TieFlightTiming_IsHighRate()) {
		int16_t history_index = (int16_t)(camera.cam_chase_slot - 5);
		if (history_index < 0)
			history_index += 60;
		camera.roll = camera.cam_chase_roll_hist[history_index];
		camera.cam_heading = (uint16_t)camera.cam_chase_heading_hist[history_index];
		camera.cam_pitch = (uint16_t)camera.cam_chase_pitch_hist[history_index];
		camera.cam_chase_roll_hist[camera.cam_chase_slot] = target_roll;
		camera.cam_chase_heading_hist[camera.cam_chase_slot] = target_heading;
		camera.cam_chase_pitch_hist[camera.cam_chase_slot] = target_pitch;
		if (++camera.cam_chase_slot == 60)
			camera.cam_chase_slot = 0;
		return;
	}

	if (!s_chase_camera_timing.initialized || s_chase_camera_timing.target_obj != camera.view_target_obj) {
		for (int i = 0; i < 60; ++i) {
			camera.cam_chase_roll_hist[i] = target_roll;
			camera.cam_chase_heading_hist[i] = target_heading;
			camera.cam_chase_pitch_hist[i] = target_pitch;
		}
		TieChaseCamera_Reset();
		s_chase_camera_timing.target_obj = camera.view_target_obj;
		s_chase_camera_timing.initialized = 1;
	}

	s_chase_camera_timing.current_ticks += frameticks;
	const uint64_t delay_ticks = 5u * TieFlightTiming_CompatibilityTicks();
	const uint64_t target_ticks = s_chase_camera_timing.current_ticks > delay_ticks
									  ? s_chase_camera_timing.current_ticks - delay_ticks
									  : 0;
	int16_t selected = camera.cam_chase_slot == 0 ? 59 : (int16_t)(camera.cam_chase_slot - 1);
	for (int i = 0; i < 60; ++i) {
		const int16_t candidate = (int16_t)((camera.cam_chase_slot + 59 - i) % 60);
		selected = candidate;
		if (s_chase_camera_timing.sample_ticks[candidate] <= target_ticks)
			break;
	}
	camera.roll = camera.cam_chase_roll_hist[selected];
	camera.cam_heading = (uint16_t)camera.cam_chase_heading_hist[selected];
	camera.cam_pitch = (uint16_t)camera.cam_chase_pitch_hist[selected];

	const int16_t write_slot = camera.cam_chase_slot;
	camera.cam_chase_roll_hist[write_slot] = target_roll;
	camera.cam_chase_heading_hist[write_slot] = target_heading;
	camera.cam_chase_pitch_hist[write_slot] = target_pitch;
	s_chase_camera_timing.sample_ticks[write_slot] = s_chase_camera_timing.current_ticks;
	if (++camera.cam_chase_slot == 60)
		camera.cam_chase_slot = 0;
}

size_t TieChaseCamera_CheckpointSize(void) { return sizeof s_chase_camera_timing; }

void TieChaseCamera_SaveCheckpoint(void* destination) {
	if (destination)
		memcpy(destination, &s_chase_camera_timing, sizeof s_chase_camera_timing);
}

bool TieChaseCamera_RestoreCheckpoint(const void* source, size_t size) {
	if (!source || size != sizeof s_chase_camera_timing)
		return false;
	memcpy(&s_chase_camera_timing, source, sizeof s_chase_camera_timing);
	return true;
}
