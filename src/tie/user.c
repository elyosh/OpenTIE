/*
 * USER.C -- cockpit input / view / replay camera dispatcher.
 */

#include "tie_runtime/audio/config.h"
#include "tie_runtime/audio/imuse_session.h"
#include "tie_runtime/diagnostics/diagnostics.h"
#include "tie_runtime/display/classic_display.h"
#include "tie_runtime/display/classic_framebuffer.h"
#include "tie_runtime/flight_assets/model_types.h"
#include "tie_runtime/hooks/orientation.h"
#include "tie_runtime/input/input.h"
#include "tie_runtime/runtime/exports.h"
#include "tie_runtime/runtime/inflight_state.h"
#include "tie_runtime/runtime/profile.h"
#include "tie_runtime/runtime/runtime.h"
#include "tie_runtime/snapshot/snapshot.h"
#include "tie_runtime/storage/storage.h"
#include "tie_runtime/timing/flight_timing_state.h"
#include "tie_runtime/timing/user_timing.h"
#include "tie_runtime/diagnostics/flight_trace.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "anim.h"
#include "tie/create.h"
#include "tie/damage.h"
#include "tie/draw.h"
#include "tie/fediskio.h"
#include "tie/feinput.h"
#include "tie/festring.h"
#include "tie/flight_surface_tie98.h"
#include "tie/frontend_display_tie98.h"
#include "tie/fscript.h"
#include "tie/fsfx.h"
#include "tie/fview.h"
#include "tie/gamesnd.h"
#include "tie/goals.h"
#include "tie/laser.h"
#include "tie/logbuf2.h" /* pixelswide, pixelsdeep */
#include "tie/math2.h"
#include "tie/modelbounds.h"
#include "tie/modelmesh.h"
#include "tie/msg.h"
#include "tie/msg_templates.h"
#include "tie/msgroom.h"
#include "tie/option.h"
#include "tie/pai.h"
#include "tie/panel.h"
#include "tie/rtsvga2.h"
#include "tie/spec.h"
#include "tie/tie.h"
#include "tie/tie_render_tie98.h"
#include "tie/transfm2.h"
#include "tie/trig2.h"
#include "tie/user.h"
#include "tie_runtime/audio/music_policy.h"
#include "tie_runtime/runtime/profile.h"
#include "tie_runtime/timing/chase_camera.h"
#include "tie_runtime/timing/replay_recording.h"
#include "tie_runtime/timing/replay_timing.h"
#include "util/binio.h"

#include <imuse/hilevel.h>
#include <imuse/lolevel.h>

#include "tie/damage.h"
#include "tie/goals.h"
#include "tie/help.h" /* help_helproom */
#include "tie/help.h"
#include "tie/maproom.h" /* maproom_maproom */
#include "tie/maproom.h"
#include "tie/msgroom.h"
#include "tie/option.h"
#include "tie/panelrts.h"
#include "tie/render_scene_tie98.h"
#include "tie/replay.h"
#include "tie/replayio.h"
#include "tie/wingman.h"
#include "tie_runtime/runtime/flight_screen.h"
#include "tie_runtime/timing/flight_timing.h"
#include "tie_runtime/timing/sim_clock.h"
#include <landru/task.h>

/* ================================================================== *
 *                        MODULE DATA TABLES                           *
 * ================================================================== */

/*
 * Sub-modal result handoff. Set by every leaf sub-modal task
 * (msgroom / goals / maproom / damage / wingman / help / option /
 * inflightinfo) on the tick it returns LANDRU_TASK_STEP_DONE; consumed
 * once by the parent task on the next step.
 */
int32_t user_submodal_result;

/*
 * Pending info-room request. Set to a screen index (0..6) by the
 * synchronous user_userinterface keybind handlers; consumed (and
 * reset to -1) by the flight task step which pushes the
 * user_Push_InflightInfo_Task task in response.
 */
static int32_t s_info_room_pending = -1;

int32_t user_consume_info_room_request(void) {
	int32_t r = s_info_room_pending;
	s_info_room_pending = -1;
	return r;
}

static TieFlightScreen flight_screen_from_index(int32_t screen) {
	if (screen < 0 || screen > 6)
		return TIE_FLIGHT_SCREEN_NORMAL;
	return (TieFlightScreen)(screen + 1);
}

/*
 * Pending replay-viewer request. Set by the 'v' key handler in
 * user_userinterface; consumed by the flight task step, which pushes
 * replayio_Push_ReplayScreen_Task in response. The pre-empt bookkeeping
 * (spool flush, blank screen, recording stop, info banner) runs
 * immediately at the keybind site, since those side effects need to
 * happen on the same tick the user pressed the key (the viewer push
 * itself happens one tick later, after user_userinterface returns).
 */
static int s_replay_viewer_pending;

int user_consume_replay_viewer_request(void) {
	int r = s_replay_viewer_pending;
	s_replay_viewer_pending = 0;
	return r;
}

/*
 * convertmessage -- 69-byte AI-order -> display-message-index table.
 * Binary values sampled from IDA (@ 0xDCF76); used by the 'radio order
 * report' key (R) to show the target's current order by name.
 */
uint8_t convertmessage[69] = { 0x7a, 0x7a, 0x7a, 0x92, 0x92, 0x92, 0x92, 0x7d, 0x7e, 0x7d, 0x7f, 0x80,
							   0x80, 0x81, 0x82, 0x7f, 0x80, 0x81, 0x80, 0x83, 0x84, 0x7f, 0x80, 0x81,
							   0x84, 0x7f, 0x80, 0x81, 0x85, 0x85, 0x85, 0x86, 0x85, 0x87, 0x85, 0x88,
							   0x89, 0x98, 0x98, 0x89, 0x8a, 0x89, 0x8b, 0x8c, 0x96, 0x8d, 0x8d, 0x8d,
							   0x8d, 0x8e, 0x8f, 0x90, 0x91, 0x90, 0x8d, 0x7a, 0x92, 0x92, 0x93, 0x94,
							   0x7c, 0x7c, 0x92, 0x92, 0x96, 0x95, 0x97, 0x7a, 0x85 };

const uint8_t* TieTextSnapshot_ConvertMessageTable(void) { return convertmessage; }

/*
 * viewtranslate[10] -- scan-key -> pilotview slot for the numpad view keys.
 * Indexed 0..9. Binary bytes: {0,3,4,5,2,16,6,1,0,7}.
 */
uint8_t viewtranslate[10] = { 0, 3, 4, 5, 2, 16, 6, 1, 0, 7 };

/*
 * looktranslate[20] -- (pilotview, side_angle_byte) pairs, 10 keys.
 * Used by the chase-camera snap keys; binary at 0xDCF62.
 */
uint8_t looktranslate[20] = {
	0, 0, 0, 0xa0, 0, 0x80, 0, 0x60, 0, 0xc0, 0, 0, 0, 0x40, 0, 0xe0, 0, 0, 0, 0x20
};

/*
 * LOD preset tables. Indexed 0..3 by user_setdetaillevel. Level 0 is
 * the shipped default (moderate detail); higher levels increase
 * fidelity at frame-rate cost.
 */
const uint16_t starshipexplodtl[4] = { 0x1000, 0x2000, 0x4000, 0x7FFF };
const uint16_t starshipdtl[4] = { 1, 2, 3, 4 };
const uint16_t stardtl[4] = { 2, 1, 1, 1 };
const uint16_t backdtl[4] = { 0, 0, 0, 1 };
const uint16_t debrisdtl[4] = { 0, 0, 1, 1 };
const int16_t polydtl[4] = { 1, 1, 0, -1 };
const uint16_t numpolydtl[4] = { 8, 12, 16, 16 };
const uint16_t markdtl[4] = { 0, 0, 1, 1 };
const uint16_t hyperdtl[4] = { 16, 32, 44, 50 };
const uint8_t gourauddtl[6] = { 0, 0, 0x40, 0x40, 0, 0 };

/* Per-frame scratch + volume-toggle latches (user.c per watdbg). */
uint16_t screendist;
uint8_t soundvolflag;
uint8_t musicvolflag;

/* ================================================================== *
 *                          SMALL HELPERS                              *
 * ================================================================== */

/*
 * user_framerateadjust -- scale a per-frame increment by elapsed PIT
 * ticks. 236 = one nominal-rate frame, so at target framerate this is
 * the identity. See binary 0x5DCB8.
 */
// FUNCTION: TIE 0x5FC50
int16_t user_framerateadjust(int16_t per_236) { return (int16_t)math2_ABoverC32(per_236, frameticks, 236); }

/*
 * user_increasepower / user_decreasepower -- saturating throttle adjust.
 * Binary 0x5DCD4 / 0x5DD04.
 */
// FUNCTION: TIE 0x5FC6C
void user_increasepower(uint16_t delta) {
	uint16_t cur = pstate.player_craft->throttle_speed;
	uint16_t next = (uint16_t)(cur + delta);
	pstate.player_craft->throttle_speed = next;
	if (cur > next)
		pstate.player_craft->throttle_speed = 0xFFFF;
}

// FUNCTION: TIE 0x5FC9C
void user_decreasepower(uint16_t delta) {
	uint16_t cur = pstate.player_craft->throttle_speed;
	pstate.player_craft->throttle_speed = (uint16_t)(cur - delta);
	if (cur < delta)
		pstate.player_craft->throttle_speed = 0;
}

/*
 * user_adjustshields -- pour shield energy from forward_shield[src_idx]
 * to forward_shield[dst_idx]. Indices 0=front, 1=rear.
 * Cap = 2x spec.shield_points (or 4x on easy difficulty).
 * Binary 0x5DD34. The binary indexes the two 16-bit shield fields as
 * (&forward_shield)[idx]; we express that via a local pointer.
 */
// FUNCTION: TIE 0x5FCCC
void user_adjustshields(uint16_t dst_idx, uint16_t src_idx) {
	int16_t* shields = &pstate.player_craft->forward_shield;
	int16_t src_cur = shields[src_idx];
	if (src_cur <= 0)
		return;

	int16_t cap = (int16_t)(2 * spec_data[pstate.player_spec_num].shield_points);
	if (!mission.difficulty)
		cap = (int16_t)(4 * spec_data[pstate.player_spec_num].shield_points);

	int16_t dst_cur = shields[dst_idx];
	int16_t headroom = (int16_t)(cap - dst_cur);
	if (headroom <= 0)
		return;

	if (headroom >= src_cur) {
		shields[dst_idx] = (int16_t)(src_cur + dst_cur);
		shields[src_idx] = 0;
	} else {
		shields[dst_idx] = (int16_t)(headroom + dst_cur);
		shields[src_idx] = (int16_t)(src_cur - headroom);
	}
}

/*
 * user_resetview -- recenter the view after threat-view zoom or after
 * a target change. When zoomed (camera.view_zoom_flag != 0) prime the 60-slot
 * cam-chase angle history ring to the current orientation; otherwise
 * zero the pitch offset and either restore the saved angles (if still
 * tracking the player) or default back to pilotview 18. Binary 0x5DDE4.
 */
// FUNCTION: TIE 0x5FD7C
void user_resetview(void) {
	if (camera.view_zoom_flag) {
		uint16_t view_idx = camera.view_heading_offset ? 20u : 18u;
		panelrts_setnewpilotview(view_idx);
		for (int i = 0; i < 60; ++i) {
			camera.cam_chase_roll_hist[i] = camera.roll;
			camera.cam_chase_heading_hist[i] = (int16_t)camera.cam_heading;
			camera.cam_chase_pitch_hist[i] = (int16_t)camera.cam_pitch;
		}
		TieChaseCamera_Reset();
	} else {
		camera.view_pitch_offset = 0;
		uint16_t view_idx_out;
		if (camera.view_target_obj == pstate.object_idx) {
			camera.side_angle = camera.view_saved_side_angle;
			camera.up_angle = camera.view_saved_up_angle;
			view_idx_out = camera.view_saved_idx;
		} else {
			view_idx_out = 18;
			camera.side_angle = 0;
			camera.up_angle = 0;
		}
		panelrts_setnewpilotview(view_idx_out);
		if (camera.view_target_obj != pstate.object_idx) {
			camera.view_zoom_flag = 1;
		}
	}
}

/*
 * user_setdetaillevel -- install the detail-preset tables into the
 * runtime flags. Binary 0x5F07C.
 */
// FUNCTION: TIE 0x60FD0
void user_setdetaillevel(uint16_t level) {
	starshipexplodetail = starshipexplodtl[level];
	starshipdetail = starshipdtl[level];
	stardetaillevel = stardtl[level];
	hyperspacedetail = (int16_t)hyperdtl[level];
	drawbackdropflag = (uint8_t)backdtl[level];
	gouraudflag = gourauddtl[level];
	drawdebrisflag = (uint8_t)debrisdtl[level];
	shipdetailvalue = polydtl[level];
	shipdetailpolycnt = numpolydtl[level];
	drawmarkingsflag = (uint8_t)markdtl[level];
	lightflag = 1;
}

/*
 * user_mapmissiletomessage -- warhead_type -> status-banner argtable id.
 * Unknown types return default_msg. Binary 0x5FEC0.
 */
// FUNCTION: TIE 0x61E40
int32_t user_mapmissiletomessage(uint8_t warhead_type, int32_t default_msg) {
	switch (warhead_type) {
		case 0x8F:
			return 8;
		case 0x90:
			return 9;
		case 0x94:
			return 184;
		case 0x95:
			return 185;
		case 0x96:
			return 186;
		case 0x97:
			return 187;
		case 0x98:
			return 188;
		default:
			return default_msg;
	}
}

/*
 * user_validcomponent -- filter hidden meshes when advancing radar_target1.
 * Binary 0x5FF1C.
 */
// FUNCTION: TIE 0x61E9C
int16_t user_validcomponent(uint16_t comp_idx) {
	const ShipModelMesh* m = &componentblockptr[comp_idx];
	int mtype = m->mesh_type;
	if (mtype == 18 || mtype == 19)
		return 0;

	int16_t has_pos = m->has_position;
	if (!has_pos)
		return 1;
	if (has_pos == 1 && mtype != 1 /* MainHull */)
		return 1;

	const ShipModelMesh* probe = componentblockptr;
	int n = objectblockptr->num_meshes;
	for (int i = 0; i < n; ++i, ++probe) {
		if (probe->has_position == has_pos && probe->mesh_type == mtype)
			return (i == comp_idx);
	}
	return 0;
}

// FUNCTION: TIE98 0x498D00 USER_validcomponent
int16_t user_validcomponent_tie98(uint16_t model_type, uint16_t mesh_index) {
	const int mesh_type = modelmesh_gettype(model_type, mesh_index);
	if (mesh_type == TIE_MESH_MISC_HULL || mesh_type == TIE_MESH_ANTENNA)
		return 0;
	const int target_id = modelmesh_gettargetid(model_type, mesh_index);
	if (target_id == 0)
		return 1;
	if (target_id == 1 && mesh_type != TIE_MESH_MAIN_HULL && mesh_type != TIE_MESH_FUSELAGE)
		return 1;
	const int count = modelmesh_getcount(model_type);
	for (int index = 0; index < count; ++index) {
		if (modelmesh_gettargetid(model_type, index) == target_id &&
			modelmesh_gettype(model_type, index) == mesh_type)
			return index == mesh_index;
	}
	return 0;
}

/* ================================================================== *
 *                           TARGETING                                 *
 * ================================================================== */

/*
 * user_picktarget -- auto-target scan. Binary 0x5DEA4.
 * Scores hostile craft in view by reticle proximity; fallback to nearest
 * by screendist when none land in the reticle.
 */
// FUNCTION: TIE 0x5FE4C
uint16_t user_picktarget(void) {
	uint32_t best_in_cross_rough = 0xFFFFFFFFu;
	uint16_t best_offscreen_dist = 0xFFFF;
	uint16_t best_in_cross_idx = 0xFFFF;
	uint16_t best_offscreen_idx = 0xFFFF;

	for (uint16_t i = 0; i < NUM_OBJECTS; ++i) {
		if (!objects[i].ship_idx || i == pstate.object_idx)
			continue;
		if ((species_table[objects[i].ship_idx].side & 1) == 0)
			continue;

		if (user_targetincross(i, 1)) {
			if (best_in_cross_rough > (uint32_t)roughdistance) {
				best_in_cross_idx = i;
				best_in_cross_rough = (uint32_t)roughdistance;
			}
		} else if (best_offscreen_dist > screendist) {
			best_offscreen_idx = i;
			best_offscreen_dist = screendist;
		}
	}

	uint16_t static_obj_idx = 14336;
	for (uint16_t j = 0; j < 0x40u; ++j, ++static_obj_idx) {
		uint8_t species = staticobjects[j].species;
		if (!species)
			continue;
		if ((species_table[species].side & 1) == 0)
			continue;

		if (user_targetincross(static_obj_idx, 1)) {
			if (best_in_cross_rough > (uint32_t)roughdistance) {
				best_in_cross_idx = static_obj_idx;
				best_in_cross_rough = (uint32_t)roughdistance;
			}
		} else if (best_offscreen_dist > screendist) {
			best_offscreen_idx = static_obj_idx;
			best_offscreen_dist = screendist;
		}
	}

	if (best_in_cross_idx == 0xFFFF && best_offscreen_dist < 50)
		return best_offscreen_idx;
	return best_in_cross_idx;
}

/*
 * user_picknexttarget(start, step) -- step target cursor by +/-1 through
 * the 0..0x73 craft + 0x3800..0x383F static index spaces, skipping dead
 * slots. Updates global craftptr. Binary 0x5DFD0.
 */
// FUNCTION: TIE 0x5FF78
uint16_t user_picknexttarget(uint16_t start, int32_t step) {
	/* Binary quirk preserved: the local craft_ptr persists across loop
	 * iterations. If a prior iteration captured a hyperspacing craft's
	 * pointer and then we exit on a static or category-set target, the
	 * global craftptr inherits that last-seen craft pointer. */
	CraftData* cp_local = craftptr;
	int16_t step_local = (int16_t)step;
	/* Total search budget = NUM_OBJECTS + NUM_STATIC_OBJECTS. Retail = 184
	 * (120 + 64); demo was 180 (116 + 64). */
	int iter = NUM_OBJECTS + NUM_STATIC_OBJECTS;
	int found = 0;

	while ((int16_t)--iter != -1) {
		start = (uint16_t)(start + step_local);
		if (start < 0x8000u) {
			switch (start) {
				/* Underflow past first static (0x3800) -> last active flight
				 * slot (NUM_OBJECTS - 1 = 119 retail / 115 demo). */
				case 0x37FFu:
					start = NUM_OBJECTS - 1;
					break;
				/* Overflow past last active flight slot -> first static. */
				case NUM_OBJECTS:
					start = 14336;
					break;
				/* Overflow past last static -> first active flight slot. */
				case 0x3840u:
					start = 0;
					break;
			}
		} else {
			start = 14399;
		}
		if (start == pstate.object_idx)
			continue;

		uint8_t obj_species =
			(start >= 0x3800u) ? staticobjects[start - 14336].species : objects[start].ship_idx;
		if (!obj_species)
			continue;
		if ((species_table[obj_species].side & 1) == 0)
			continue;

		if (start >= NUM_ACTIVE_CRAFT_SLOTS) {
			found = 1;
			break;
		}
		if (objects[start].genus != GENUS_EXPLOSION) {
			if (objects[start].category) {
				found = 1;
				break;
			}
			cp_local = objects[start].craft_ptr;
			int flight = cp_local->flight_flag;
			if (flight != 3 && flight != 4) {
				found = 1;
				break;
			}
			/* flight_flag 3 or 4 (hyperspace) -> continue loop, but
			 * cp_local now carries this craft's ptr for the next exit. */
		}
		/* EXPLOSION genus or hyperspacing craft -> continue loop. */
	}
	if (!found)
		start = 0xFFFF;
	craftptr = cp_local;
	return start;
}

/*
 * user_targetincross -- does obj_idx project inside the gunsight?
 * Writes screendist. strict=1 -> pixel-accurate reticle; strict=0 ->
 * triples tolerance (auto-target scan). Binary 0x5E0D8.
 *
 * Demo (0x5E371) had a bug here: the Y-axis off-screen reject compared
 * |screen_x| against pixelsdeep/2 instead of |screen_y|. The 1995
 * Collector's CD-ROM retail build (USER_targetincross @ 0x60080) fixes
 * this and additionally adds an aspect-ratio correction on the Y
 * component (multiplier 59578/65536 ≈ 0.909). We apply both retail
 * fixes unconditionally because the demo behaviour was provably wrong
 * (false negatives at the horizontal edges, false positives above/below
 * the screen).
 *
 * Watcom unaligned-dword-load idioms on player->{orient_dirty, fwd_*,
 * side_*, up_*} are rewritten as explicit field accesses.
 */
// FUNCTION: TIE 0x60080
int16_t user_targetincross(uint16_t obj_idx, int32_t strict) {
	FlightObject* pl = pstate.player;
	screendist = 0xFFFF;
	pai_roughdistancebetween(obj_idx, pstate.object_idx);

	int32_t delta_x, delta_y, delta_z;
	int8_t dist_shift;
	if (roughdistance >= 0xA0000) {
		create_getworldposition(obj_idx, 0);
		delta_x = (worldlocx - pl->world_x) >> 8;
		delta_y = (worldlocy - pl->world_y) >> 8;
		delta_z = (worldlocz - pl->world_z) >> 8;
		dist_shift = 8;
	} else {
		create_getworldposition(obj_idx, 0);
		delta_x = (worldlocx - pl->world_x) >> 4;
		delta_y = (worldlocy - pl->world_y) >> 4;
		delta_z = (worldlocz - pl->world_z) >> 4;
		dist_shift = 4;
	}

	if (pl->orient_dirty) {
		fview_calcrotatemove(pl->heading, pl->pitch, pl);
		fview_calcrotateorient(pl->roll, 0, pl);
	}

	int32_t eye_z = ((pl->fwd_z * (int16_t)delta_z) >> 15) + ((pl->fwd_y * (int16_t)delta_y) >> 15) +
					((pl->fwd_x * (int16_t)delta_x) >> 15);
	if (eye_z <= 0 || eye_z > 0x20000)
		return 0;
	if (eye_z < 0x2000)
		++dist_shift;

	int32_t half_wide = pixelswide / 2;
	int32_t eye_side_dot = ((pl->side_z * (int16_t)delta_z) >> 15) + ((pl->side_y * (int16_t)delta_y) >> 15) +
						   ((pl->side_x * (int16_t)delta_x) >> 15);
	int32_t screen_x_rel = transfm2_getscreencoordx(eye_side_dot, eye_z) - half_wide;

	int32_t screen_dx_abs = (int32_t)(int16_t)screen_x_rel;
	if (screen_dx_abs & 0x8000)
		screen_dx_abs = -(int32_t)(int16_t)screen_x_rel;
	if ((int16_t)screen_dx_abs > (int32_t)pixelswide / 2)
		return 0;

	int32_t eye_up_dot = ((pl->up_z * (int16_t)delta_z) >> 15) + ((pl->up_y * (int16_t)delta_y) >> 15) +
						 ((pl->up_x * (int16_t)delta_x) >> 15);
	int32_t screen_y_rel =
		transfm2_getscreencoordy(eye_up_dot, eye_z) - (pixelsdeep / 2) - transfm2_screenyoffset;

	int32_t screen_dy_abs = (int32_t)(int16_t)screen_y_rel;
	if (screen_dy_abs & 0x8000)
		screen_dy_abs = -(int32_t)(int16_t)screen_y_rel;
	/* Retail fix: compare |screen_y| against the vertical half-extent, and
	 * apply aspect-ratio correction (59578/65536 ≈ 0.909) to make the
	 * reticle circular on 320x200 VGA (non-square pixels). */
	screen_dy_abs = (screen_dy_abs * 59578) >> 16;
	if ((int16_t)screen_dy_abs > (int32_t)pixelsdeep / 2)
		return 0;

	int32_t bound_hwidth;
	if (obj_idx >= NUM_ACTIVE_CRAFT_SLOTS) {
		int species =
			(obj_idx >= 0x3800u) ? staticobjects[obj_idx - 14336].species : objects[obj_idx].ship_idx;
		bound_hwidth = species_table[species].bound_hwidth;
	} else {
		int sp = objects[obj_idx].craft_ptr->species_idx;
		bound_hwidth =
			(((int16_t)(spec_data[sp].bound_height + spec_data[sp].bound_depth + spec_data[sp].bound_width) /
			  3)
			 << spec_data[sp].model_scale_shift);
	}

	int32_t reticle = ((bound_hwidth >> dist_shift) << 8) / eye_z;
	if ((int16_t)reticle <= 0)
		reticle = 1;
	if (!strict) {
		reticle = 3 * (int16_t)reticle;
		if ((int16_t)reticle < 9)
			reticle = 9;
	}
	screendist = (uint16_t)((int16_t)screen_dy_abs + (int16_t)screen_dx_abs);
	return ((int16_t)screen_dx_abs < (int16_t)reticle && (int16_t)screen_dy_abs < (int16_t)reticle);
}

/*
 * user_targetonscreen -- paint target bracket around obj_or_kind.
 * Retail behavior (Z_TIE__ 0x603EC): a hollow box drawn into the xtrans
 * buffer via panel_drawboxinxtrans, sized to screen resolution. The demo
 * binary used a rotscale bracket sprite; we follow retail.
 */
// FUNCTION: TIE 0x603EC
int16_t user_targetonscreen(uint16_t obj_or_kind) {
	FlightObject* pl = pstate.player;
	if (obj_or_kind == 0xFFFF || replayviewmode)
		return 0;
	if (!pstate.radar_enable)
		return 0;
	if (camera.pilotview && camera.pilotview != 19)
		return 0;

	uint16_t obj_idx_loc = obj_or_kind;
	pai_distancebetween(obj_idx_loc, pstate.object_idx);

	/* HD snapshot publish for target_box_engine_ok + target_bound_hwidth
	 * is now driven by TieHudSnapshot_Capture (runs unconditionally per
	 * host frame, including when paused — see panel_publish_target_box_state).
	 * user_targetonscreen still runs the classic 4:3 projection + xtrans
	 * emit below, but skipping it during pause no longer makes the HD
	 * target box vanish. bound_hwidth_pre is still computed locally
	 * for the apparent-size threshold check at line ~620. */
	int32_t bound_hwidth_pre;
	if (obj_idx_loc >= NUM_ACTIVE_CRAFT_SLOTS) {
		int species = (obj_idx_loc >= 0x3800u) ? staticobjects[obj_idx_loc - 14336].species
											   : objects[obj_idx_loc].ship_idx;
		bound_hwidth_pre = TieProfile_UsesTie98Logic() ? modelbounds_getmaxextent(species)
													   : species_table[species].bound_hwidth;
	} else {
		int sp = objects[obj_idx_loc].craft_ptr->species_idx;
		bound_hwidth_pre =
			(((int16_t)(spec_data[sp].bound_height + spec_data[sp].bound_depth + spec_data[sp].bound_width) /
			  3)
			 << spec_data[sp].model_scale_shift);
	}

	int32_t delta_x, delta_y, delta_z;
	int8_t dist_shift;
	if (trig2_polardistance >= 0x80000) {
		create_getworldposition(obj_idx_loc, 0);
		delta_x = (worldlocx - pl->world_x) >> 8;
		delta_y = (worldlocy - pl->world_y) >> 8;
		delta_z = (worldlocz - pl->world_z) >> 8;
		dist_shift = 8;
	} else {
		create_getworldposition(obj_idx_loc, 0);
		delta_x = (worldlocx - pl->world_x) >> 4;
		delta_y = (worldlocy - pl->world_y) >> 4;
		delta_z = (worldlocz - pl->world_z) >> 4;
		dist_shift = 4;
	}

	if (pl->orient_dirty) {
		fview_calcrotatemove(pl->heading, pl->pitch, pl);
		fview_calcrotateorient(pl->roll, 0, pl);
	}

	int32_t eye_z = ((pl->fwd_x * (int16_t)delta_x) >> 15) + ((pl->fwd_y * (int16_t)delta_y) >> 15) +
					((pl->fwd_z * (int16_t)delta_z) >> 15);
	if (eye_z <= 0)
		return 0;

	int32_t eye_side = ((pl->side_z * (int16_t)delta_z) >> 15) + ((pl->side_y * (int16_t)delta_y) >> 15) +
					   ((pl->side_x * (int16_t)delta_x) >> 15);
	int16_t screen_x = (int16_t)transfm2_getscreencoordx(eye_side, eye_z);
	if ((int32_t)screen_x < 0 || screen_x > (int32_t)pixelswide)
		return 0;

	int32_t eye_up = -(((pl->up_y * (int16_t)delta_y) >> 15) + ((pl->up_x * (int16_t)delta_x) >> 15) +
					   ((pl->up_z * (int16_t)delta_z) >> 15));
	/* transfm2_getscreeny already adds halfpixelsdeep + screenyoffset, so
	 * the result is an absolute screen-space Y. Retail uses it directly;
	 * the demo subtracted from pixelsdeep because its getscreencoordy
	 * returned a relative offset instead. Don't flip here. */
	int16_t screen_y = (int16_t)transfm2_getscreeny(eye_up, eye_z);
	if (screen_y < 0 || screen_y > (int16_t)pixelsdeep)
		return 0;

	int32_t bound_hwidth = bound_hwidth_pre;
	int32_t reticle = (perspFactor * (bound_hwidth >> dist_shift)) / eye_z;
	int32_t threshold = (flightResolution == TIE_FLIGHT_RES_VGA) ? 5 : 10;
	if ((int16_t)reticle > threshold)
		return 0;

	int32_t box_w = screenXRes / 0x30;
	int32_t box_h = screenYRes / 0x30;
	panel_drawboxinxtrans((int16_t)(screen_x - box_w / 2), (int16_t)(screen_y - box_h / 2), (uint16_t)box_w,
						  (uint16_t)box_h, 0xCE);
	return 0;
}

// FUNCTION: TIE98 0x4974A0
static int32_t user_gettargetdisplayextent_tie98(uint16_t object_reference) {
	if (object_reference >= OBJ_REF_STATIC_BASE) {
		const uint8_t model_type = staticobjects[object_reference - OBJ_REF_STATIC_BASE].species;
		return species_table[model_type].bound_hwidth;
	}
	FlightObject* object = &objects[object_reference];
	const uint16_t spec_index = object->craft_ptr->species_idx;
	if (object_reference < NUM_CRAFTS && object->genus != 0 &&
		(object->genus != 3 || spec_data[spec_index].max_speed != 0) &&
		(object->genus != 1 || object->ship_idx == 19 || object->ship_idx == 20)) {
		return ((spec_data[spec_index].bound_width + spec_data[spec_index].bound_height +
				 spec_data[spec_index].bound_depth) /
				3)
			   << spec_data[spec_index].model_scale_shift;
	}
	return species_table[object->ship_idx].bound_hwidth;
}

// FUNCTION: TIE98 0x497590
static int user_projectobjectmeshcenter_tie98(uint16_t object_reference, int16_t mesh_index,
											  int32_t* screen_x, int32_t* screen_y, int32_t* depth) {
	create_getworldposition(object_reference, 0);
	int32_t world_x = worldlocx;
	int32_t world_y = worldlocy;
	int32_t world_z = worldlocz;
	if (mesh_index != -1 && object_reference < OBJ_REF_STATIC_BASE) {
		FlightObject* object = &objects[object_reference];
		const uint16_t spec_index = object->craft_ptr->species_idx;
		if (object->genus != 0 && (object->genus != 3 || spec_data[spec_index].max_speed != 0) &&
			(object->genus != 1 || object->ship_idx == 19 || object->ship_idx == 20)) {
			pai_RotateLocalVectorToWorldScratch(object, modelmesh_getcenterx(object->ship_idx, mesh_index),
												modelmesh_getcenterz(object->ship_idx, mesh_index),
												-modelmesh_getcentery(object->ship_idx, mesh_index));
			world_x += rotatedx;
			world_y += rotatedy;
			world_z += rotatedz;
			worldlocx = world_x;
			worldlocy = world_y;
			worldlocz = world_z;
		}
	}
	const int32_t relative_x = world_x - camera.x;
	const int32_t relative_y = world_y - camera.y;
	const int32_t relative_z = world_z - camera.z;
	*depth = transfm2_geteyez(relative_x, relative_y, relative_z);
	if (*depth > 0) {
		const int32_t eye_x = transfm2_geteyex(relative_x, relative_y, relative_z);
		const int32_t eye_y = transfm2_geteyey(relative_x, relative_y, relative_z);
		*screen_x = transfm2_getscreenx(eye_x, *depth);
		*screen_y = transfm2_getscreeny(eye_y, *depth);
	}
	return *depth;
}

// FUNCTION: TIE98 0x4971A0
void user_targetonscreen_tie98(uint16_t object_reference, int16_t mesh_index, uint8_t color_index) {
	if (object_reference == 0xffff || replayviewmode || !pstate.radar_enable)
		return;
	int32_t screen_x;
	int32_t screen_y;
	int32_t depth;
	user_projectobjectmeshcenter_tie98(object_reference, mesh_index, &screen_x, &screen_y, &depth);
	if (depth > 0) {
		int32_t extent;
		if (object_reference < OBJ_REF_STATIC_BASE && mesh_index != -1) {
			FlightObject* object = &objects[object_reference];
			const uint16_t spec_index = object->craft_ptr->species_idx;
			if (object->genus != 0 && (object->genus != 3 || spec_data[spec_index].max_speed != 0) &&
				(object->genus != 1 || object->ship_idx == 19 || object->ship_idx == 20))
				extent = modelmesh_getcomponentmaxextent(object->ship_idx, mesh_index);
			else
				extent = user_gettargetdisplayextent_tie98(object_reference);
		} else {
			extent = user_gettargetdisplayextent_tie98(object_reference);
		}
		const int minimum = flightResolution == TIE_FLIGHT_RES_VGA ? 4 : 8;
		int size = (int)((uint32_t)perspFactor * (uint32_t)extent / (uint32_t)depth);
		if (size < minimum)
			size = minimum;
		const int maximum = screenXRes / 2 + screenXRes / 4;
		if (size > maximum)
			size = maximum;
		size += 4;
		if (mapflag) {
			FlightMap_DrawObjectBoxCorners(screen_x - size / 2, screen_y - size / 2, size, size, color_index);
		} else {
			Hud_DrawBoxInXTrans(screen_x - size / 2, screen_y - size / 2, size, size, color_index, depth);
		}
	}
	if (bluetarget == 0xffff)
		return;
	user_projectobjectmeshcenter_tie98(bluetarget, -1, &screen_x, &screen_y, &depth);
	if (depth <= 0)
		return;
	const int32_t extent = user_gettargetdisplayextent_tie98(bluetarget);
	const int minimum = flightResolution == TIE_FLIGHT_RES_VGA ? 4 : 8;
	int size = (int)((uint32_t)perspFactor * (uint32_t)extent / (uint32_t)depth) -
			   (extent - minimum) / (blinkticks + 1);
	if (size < minimum)
		size = minimum;
	const int maximum = screenXRes / 2 + screenXRes / 4;
	if (size > maximum)
		size = maximum;
	const int outer_size = size + 2;
	if (mapflag) {
		FlightMap_DrawObjectBoxCorners(screen_x - outer_size / 2, screen_y - outer_size / 2, outer_size,
									   outer_size, 50);
	} else {
		Hud_DrawBoxInXTrans(screen_x - outer_size / 2 + 2, screen_y - outer_size / 2 + 2, size - 2, size - 2,
							50, depth);
		Hud_DrawBoxInXTrans(screen_x - outer_size / 2 + 1, screen_y - outer_size / 2 + 1, size, size, 50,
							depth);
		Hud_DrawBoxInXTrans(screen_x - outer_size / 2, screen_y - outer_size / 2, outer_size, outer_size, 51,
							depth);
	}
}

/*
 * user_setnewtarget -- lock player_craft on new_obj. Requires sensors
 * online (status_flags & 4); plays target-ack SFX; primes radar_target1
 * to first MainHull/Engines mesh; emits 'report from' radio line when
 * the target viewer is open. Binary 0x5E83C.
 */
// FUNCTION: TIE 0x60790
void user_setnewtarget(uint16_t new_obj) {
	if ((pstate.player_craft->status_flags & 4) == 0) {
		argtable[0] = 33;
		argtable[1] = 25;
		msg_messageprintf(MSG_SYSTEM_STATUS);
		return;
	}
	if (new_obj == 0xFFFF || new_obj == pstate.target_obj_idx)
		return;

	fsfx_triggersfx(0x22u, 0xFFFF);
	pstate.target_obj_idx = new_obj;
	pstate.radar_target1 = 0;

	if (new_obj < NUM_ACTIVE_CRAFT_SLOTS) {
		const uint8_t model_type = objects[new_obj].ship_idx;
		if (!TieProfile_UsesTie98Logic())
			draw_lockshipfileptrs(model_type);
		int nm = TieProfile_UsesTie98Logic() ? modelmesh_getcount(model_type) : objectblockptr->num_meshes;
		for (int i = 0; i < nm; ++i) {
			int mt = TieProfile_UsesTie98Logic() ? modelmesh_gettype(model_type, i)
												 : componentblockptr[i].mesh_type;
			if (mt == 1 || mt == 3) {
				pstate.radar_target1 = (int16_t)i;
				break;
			}
		}
	}
	if (!replayviewmode && camera.view_heading_offset)
		camera.view_target_obj = pstate.target_obj_idx;
	pstate.radar_subtarget_state = 0;
	uint16_t working_subsystems = pstate.player_craft->working_subsystems;
	pstate.player_craft->missile_count_total = 0;

	if ((working_subsystems & 1) == 0)
		return;
	if (camera.pilotview != 19)
		return;

	if (new_obj < NUM_ACTIVE_CRAFT_SLOTS) {
		CraftData* cp_t = objects[new_obj].craft_ptr;
		msg_addmessageptr(0, (char*)spec_name_ptrs[cp_t->species_idx]);
		int fg_idx = objects[new_obj].fg_idx;
		EFGStruct* fgp = &fg_array[fg_idx];
		/* Watcom unaligned load: `*(int*)&fg.special_craft >> 24` = fg.count. */
		if (fgp->count <= 1) {
			msg_addmessageptr(1, fgp->name);
			argtable[2] = 103;
			msg_messageprintf(MSG_REPORT_FROM);
			return;
		}
		argtable[1] = (uint16_t)(cp_t->craft_idx_in_fg + 1);
		msg_addmessageptr(2, fgp->name);
		argtable[3] = 103;
		msg_messageprintf(MSG_REPORT_FROM_FG);
		return;
	}
	if (new_obj >= 0x3800u) {
		uint16_t si = (uint16_t)(new_obj - 14336);
		uint8_t species = staticobjects[si].species;
		if (species >= 0x46u && species <= 0x54u)
			msg_addmessageptr(0, ((char**)buoystr)[species - 70]);
		msg_addmessageptr(1, fg_array[staticobjects[si].fg_idx].name);
		argtable[2] = 103;
		msg_messageprintf(MSG_REPORT_FROM);
	}
}

/* Quaternion pitch decomposition remains controllable at the world ±Z poles.
 *
 * TIE basis (columns S, U, f in world frame) at (β=heading polar from -Z,
 * α=pitch azimuth, γ=roll around forward):
 *     S_0 = ( cos α, -sin α, 0)
 *     U_0 = (-cos β sin α, -cos β cos α, sin β)
 *     f   = (-sin β sin α, -sin β cos α, -cos β)
 *   then S = R_f(γ) · S_0,  U = R_f(γ) · U_0.
 */

/*
 * user_calcdeltapitch -- rotate craft's forward vector by (dheading,
 * dpitch) and decompose back to Euler, writing pitch/heading/roll into
 * objects[obj_idx]. Binary 0x5EAF8.
 *
 * With TIE_USER_GIMBAL_LOCK_FIX: float matrix → quaternion →
 * Euler-with-gimbal-branch round-trip. Without it, Q15 decomposition
 * gimbal-locks at the world ±Z poles.
 *
 * Either way the Q15 basis cache on the FlightObject is refreshed by the
 * next-frame fview_calcrotatemove/fview_calcrotateorient path via the
 * caller's `orient_dirty = 1`.  Player-only path; PAIMAN AI uses separate
 * code.
 */
// FUNCTION: TIE 0x60A4C
void user_calcdeltapitch(int16_t dheading, int16_t dpitch, uint16_t obj_idx, CraftData* cp) {
	FlightObject* o = &objects[obj_idx];

	if (TieOrientationHook_Enabled()) {
		int16_t new_heading, new_pitch, new_roll;
		TieOrientationHook_Apply(o->heading, o->pitch, o->roll, dheading, dpitch, (inputbuttons & 0xE) != 2,
								 &new_heading, &new_pitch, &new_roll);
		cp->orient_heading = (uint16_t)new_heading;
		o->heading = new_heading;
		o->pitch = new_pitch;
		o->roll = new_roll;
		return;
	}

	/* Faithful Q15 reverse-engineered binary 0x5EAF8.  Gimbal-locks at the
	 * world ±Z poles: arctan(calcf1, -calcf2) below collapses to noise when
	 * both inputs are near zero.  All `*(int*)&obj->field >> 16` Watcom
	 * unaligned loads are rewritten here. */
	if (o->orient_dirty) {
		fview_calcrotatemove(o->heading, o->pitch, o);
		fview_calcrotateorient(o->roll, 0, o);
	}
	calcf1 = -o->fwd_x;
	calcf2 = -o->fwd_y;
	calcf3 = -o->fwd_z;
	calcU1 = o->up_x;
	calcU2 = o->up_y;
	calcU3 = o->up_z;
	calcS1 = o->side_x;
	calcS2 = o->side_y;
	calcS3 = o->side_z;

	fview_transformaxes(calcS1, calcS2, calcS3, dheading);
	if ((inputbuttons & 0xE) != 2)
		fview_transformaxes(calcU1, calcU2, calcU3, dpitch);

	uint16_t new_heading = (uint16_t)trig2_arccos(-(int16_t)calcf3);
	cp->orient_heading = new_heading;
	int16_t new_pitch = (int16_t)-trig2_arctan((int16_t)calcf1, -(int16_t)calcf2);

	int16_t cos_p = trig2_getsignedcos(new_pitch);
	int16_t sin_p = trig2_getsignedsin(new_pitch);
	int16_t cos_h = trig2_getsignedcos((int16_t)new_heading);
	int16_t sin_h = trig2_getsignedsin((int16_t)new_heading);

	int32_t cH_sP = (cos_h * sin_p) >> 15;
	int32_t sH_sP = (sin_h * sin_p) >> 15;
	int32_t cH_cP = (cos_h * cos_p) >> 15;
	int32_t sH_cP = (sin_h * cos_p) >> 15;
	int32_t neg_sin_p = -(int32_t)sin_p;
	int32_t neg_sin_h = -(int32_t)sin_h;

	/* Rotate each of the three basis vectors (S, U, f) by the new euler. */
#define CLAMP_Q30(v)                                                                                         \
	do {                                                                                                     \
		if ((v) >= 0x40000000)                                                                               \
			(v) = 0x3FFF0000;                                                                                \
		if ((v) <= -0x40000000)                                                                              \
			(v) = -0x3FFF0000;                                                                               \
	} while (0)

	int32_t S1 = neg_sin_p * calcS2 + (int32_t)cos_p * calcS1;
	CLAMP_Q30(S1);
	int32_t S2 = neg_sin_h * calcS3 + (int16_t)cH_cP * calcS2 + (int16_t)cH_sP * calcS1;
	CLAMP_Q30(S2);
	int32_t S3 = (int32_t)cos_h * calcS3 + (int16_t)sH_cP * calcS2 + (int16_t)sH_sP * calcS1;
	CLAMP_Q30(S3);
	calcS1 = (int16_t)(S1 >> 15);
	calcS2 = (int16_t)(S2 >> 15);
	calcS3 = (int16_t)(S3 >> 15);

	int32_t U1 = neg_sin_p * calcU2 + (int32_t)cos_p * calcU1;
	CLAMP_Q30(U1);
	int32_t U2 = neg_sin_h * calcU3 + (int16_t)cH_cP * calcU2 + (int16_t)cH_sP * calcU1;
	CLAMP_Q30(U2);
	int32_t U3 = (int32_t)cos_h * calcU3 + (int16_t)sH_cP * calcU2 + (int16_t)sH_sP * calcU1;
	CLAMP_Q30(U3);
	calcU1 = (int16_t)(U1 >> 15);
	calcU2 = (int16_t)(U2 >> 15);
	calcU3 = (int16_t)(U3 >> 15);

	int32_t F1 = neg_sin_p * calcf2 + (int32_t)cos_p * calcf1;
	CLAMP_Q30(F1);
	int32_t F2 = neg_sin_h * calcf3 + (int16_t)cH_cP * calcf2 + (int16_t)cH_sP * calcf1;
	CLAMP_Q30(F2);
	int32_t F3 = (int32_t)cos_h * calcf3 + (int16_t)sH_cP * calcf2 + (int16_t)sH_sP * calcf1;
	CLAMP_Q30(F3);
	calcf1 = (int16_t)(F1 >> 15);
	calcf2 = (int16_t)(F2 >> 15);
	calcf3 = (int16_t)(F3 >> 15);
#undef CLAMP_Q30

	int16_t new_roll = trig2_arctan((int16_t)calcS2, (int16_t)calcS1);
	o->roll = (int16_t)-new_roll;
	o->pitch = new_pitch;
}

/*
 * user_checkradio -- validate radio target. Binary 0x5F11C.
 * Side effect: writes craftptr.
 */
// FUNCTION: TIE 0x61070
int16_t user_checkradio(void) {
	if (pstate.target_obj_idx == 0xFFFF)
		return 0;
	if (pstate.target_obj_idx >= NUM_ACTIVE_CRAFT_SLOTS)
		return 0;
	uint8_t fg_idx = objects[pstate.target_obj_idx].fg_idx;
	if (fg_idx != objects[pstate.object_idx].fg_idx && !fg_array[fg_idx].camo_flag)
		return 0;
	uint16_t status = objects[pstate.target_obj_idx].craft_ptr->status_flags;
	craftptr = objects[pstate.target_obj_idx].craft_ptr;
	return status != 0;
}

/*
 * user_assigntarget -- wingman 'attack my target' etc. Binary 0x5F1D8.
 */
// FUNCTION: TIE 0x6112C
void user_assigntarget(uint16_t new_target_obj, uint16_t msg_template_id) {
	FlightObject* pl = pstate.player;
	/* No-op when the target is an ally. */
	if (new_target_obj < NUM_ACTIVE_CRAFT_SLOTS && objects[new_target_obj].side == pl->side)
		return;

	int16_t wingman_count = 0;
	uint16_t last_speaker_obj = 0xFFFF;

	for (uint16_t i = 0; i < NUM_ACTIVE_CRAFT_SLOTS; ++i) {
		if (i == pstate.object_idx)
			continue;
		if (!objects[i].ship_idx)
			continue;
		if (objects[i].side != pl->side)
			continue;

		CraftData* cp = objects[i].craft_ptr;
		int cur = cp->current_order;
		if (cur == 47 || cur == 51 || cur == 49)
			continue;
		/* Skip orders 0..6 (idle / early-spawn states). */
		if (cur < 7)
			continue;
		if (objects[i].fg_idx != pl->fg_idx)
			continue;

		if (cur == 44) {
			cp->current_order = cp->saved_current_order;
			pai_setupcraftaivars(i);
			pai_initplan();
		}
		cp->pending_radio_command = new_target_obj;
		last_speaker_obj = i;
		++wingman_count;
	}

	if (last_speaker_obj == 0xFFFF)
		return;
	uint16_t cmdr_mode = (wingman_count == 1) ? 0u : 1u;
	msg_radiomessage(last_speaker_obj, objects[last_speaker_obj].craft_ptr, msg_template_id, cmdr_mode);
}

/*
 * user_findclosestattacker -- nearest enemy targeting obj_idx. Binary 0x5F308.
 */
// FUNCTION: TIE 0x61268
uint16_t user_findclosestattacker(uint16_t obj_idx) {
	if (obj_idx == 0xFFFF)
		return 0xFFFF;
	uint32_t best_dist = 0xFFFFFFFFu;
	uint16_t best_idx = 0xFFFF;
	for (uint16_t i = 0; i < NUM_ACTIVE_CRAFT_SLOTS; ++i) {
		if (!objects[i].ship_idx || i == obj_idx)
			continue;
		CraftData* cp = objects[i].craft_ptr;
		if (cp->ai_target_ref != obj_idx)
			continue;
		if (!cp->status_flags)
			continue;
		int mode = cp->mode_byte;
		if (mode != 12 && mode != 23)
			continue;
		uint8_t ff = cp->flight_flag;
		if (ff && ff != 6)
			continue;

		pai_distancebetween(obj_idx, i);
		if (best_dist > (uint32_t)trig2_polardistance) {
			best_idx = i;
			best_dist = (uint32_t)trig2_polardistance;
		}
	}
	return best_idx;
}

/*
 * user_isrescued -- eject-pod rescue test. Binary 0x5F3B0.
 * rescue_override_flag bit 0 forces rescue (mission file flag). Otherwise
 * returns true iff nearest hostile is more than twice as far as nearest
 * friendly.
 */
// FUNCTION: TIE 0x61310
int16_t user_isrescued(uint16_t player_obj_idx) {
	if (rescue_override_flag & 1)
		return 1;
	uint32_t nearest_friend = 0x1000000u;
	uint32_t nearest_hostile = 0x1000000u;

	for (uint16_t i = 0; i < NUM_ACTIVE_CRAFT_SLOTS; ++i) {
		if (i == player_obj_idx)
			continue;
		if (!objects[i].ship_idx)
			continue;
		if (objects[i].category)
			continue;
		if (!objects[i].genus)
			continue;

		uint8_t side = objects[i].side;
		if (!side || side == 4) {
			pai_distancebetween(player_obj_idx, i);
			if (nearest_friend > (uint32_t)trig2_polardistance)
				nearest_friend = (uint32_t)trig2_polardistance;
		} else if (side == 1) {
			pai_distancebetween(player_obj_idx, i);
			if (nearest_hostile > (uint32_t)trig2_polardistance)
				nearest_hostile = (uint32_t)trig2_polardistance;
		}
	}
	return (int16_t)((nearest_hostile / 2) < nearest_friend);
}

/*
 * user_checkreplaycamera -- stop-and-flush helper. Binary 0x5F478.
 */
// FUNCTION: TIE 0x613D8
void user_checkreplaycamera(void) {
	if (!recordingreplay)
		return;
	if (!replayio_spoolreplayinput())
		replaytotalcnt -= replaybuffercnt;
	replaybuffercnt = 0;
	recordingreplay = 0;
	msg_messageprintf(MSG_REPLAY_CAMERA_OFF);
	calcframerate = 0;
}

/*
 * user_ejectcamera -- swap to eject pod / fly-by camera. Binary 0x5F4CC.
 */
// FUNCTION: TIE 0x6142C
void user_ejectcamera(void) {
	fscript_MsSetSequence(16);
	hyperspaceflag = 0;
	player_ejected = 1;
	if (recordingreplay) {
		if (!replayio_spoolreplayinput())
			replaytotalcnt -= replaybuffercnt;
		replaybuffercnt = 0;
		recordingreplay = 0;
		msg_messageprintf(MSG_REPLAY_CAMERA_OFF);
		calcframerate = 0;
	}
	camera.view_target_obj = 0xFFFF;
	camera.view_zoom_flag = 1;
	camera.x = pstate.player->world_x_prev;
	camera.y = pstate.player->world_y_prev;
	camera.z = pstate.player->world_z_prev;
	camera.view_pitch_offset = 1;
	camera.up_angle = 0;
	camera.side_angle = 0;
	msg_clearmessagequeue();
	if (pstate.player_craft->status_flags & 2) {
		panelrts_setnewpilotview(0x12u);
		msg_messageprintf(MSG_EJECTED);
	} else {
		panelrts_setnewpilotview(0);
		msg_messageprintf(MSG_DIED);
	}
	pstate.hyperin_state = 1;
}

/* ================================================================== *
 *                      REPLAY TAPE RECORD/PLAY                        *
 * ================================================================== */

/*
 * user_nextreplaycount -- per playback tick. Binary 0x5AC64.
 */
// FUNCTION: TIE 0x5CC44
void user_nextreplaycount(void) {
	++replaytotalcntdown;
	uint16_t new_bufcnt = (uint16_t)(replaybuffercnt + 1);
	replaybuffercnt = new_bufcnt;
	if (replaytotalcntdown < (uint32_t)replaytotalcnt) {
		if (new_bufcnt >= REPLAY_INPUT_CHUNK_FRAMES) {
			if (!replay_loadreplayinput()) {
				replaytotalcntdown = (uint32_t)replaytotalcnt;
				replay_stopreplay();
				return;
			}
			replaybuffercnt = 0;
			replayptr = replaybufferstart;
		}
	} else {
		replay_stopreplay();
	}
}

/*
 * user_nextreplaystore -- per record tick. Binary 0x5ACBC.
 */
// FUNCTION: TIE 0x5CC9C
void user_nextreplaystore(void) { TieReplayRecording_StoreRecord(true); }

/* ================================================================== *
 *                         TOP-LEVEL FRAME                             *
 * ================================================================== */

typedef enum {
	PAUSE_NONE = 0,
	PAUSE_ACTIVE = 1,
} PauseState;

static PauseState s_pause_state;
static int16_t s_pause_saved_vol;

static void pause_enter(void) {
	s_pause_saved_vol = imuse_get_master_vol(im);
	imuse_set_master_vol(im, 0);
	imuse_pause(im);
	msg_messageprintf(MSG_PAUSED);
	if (TieClassicDisplay_UsesDx5()) {
		FrontendDisplay_BlitOffscreenToRenderSurface();
		FrontendDisplay_PresentFrame();
	}
	s_pause_state = PAUSE_ACTIVE;
}

static void pause_exit(void) {
	if (TieClassicDisplay_UsesDx5())
		FrontendDisplay_PresentFrame();
	msg_messageprintf(MSG_RESUMED);
	calcframerate = 0;
	keypress = 0;
	imuse_set_master_vol(im, s_pause_saved_vol);
	imuse_resume(im);
	s_pause_state = PAUSE_NONE;
}

int user_is_paused(void) { return s_pause_state == PAUSE_ACTIVE; }

/*
 * user_userinterface -- top-of-frame dispatcher. Binary 0x5A6B0.
 * Phases:
 *   (0) if paused, poll for resume key and return; world update +
 *       render are skipped by tie_doframe via user_is_paused().
 *   (1) auto-drop destroyed camera.view_target_obj back to the player.
 *   (2) if in replay playback, read next (key,dx,dy,buttons) tuple.
 *   (3) else fetch raw hardware input and handle meta keys (pause/menu).
 *   (4) if recording, append to tape.
 *   (5) dispatch flight controls via user_inputforplane.
 */
// FUNCTION: TIE 0x5C440, TIE98 0x493840
void user_userinterface(void) {
	/* Phase 0: paused? Poll for any input; any key resumes. The
	 * outer flight loop keeps iterating at the existing 62.5 Hz
	 * floor in tie_doframe Step 1, so this samples input ~16 ms. */
	if (s_pause_state == PAUSE_ACTIVE) {
		if (feinput_getrawinput() != 0)
			pause_exit();
		return;
	}

	/* Phase 1: target object disappeared → snap view back to the player. */
	if (!replayviewmode && camera.view_target_obj != 0xFFFF) {
		int gone = 0;
		if (camera.view_target_obj < 0x3800u) {
			if (!objects[camera.view_target_obj].ship_idx)
				gone = 1;
		} else {
			if (!staticobjects[camera.view_target_obj - 14336].species)
				gone = 1;
		}
		if (gone) {
			camera.view_target_obj = pstate.object_idx;
			user_resetview();
		}
	}

	if (replayviewmode) {
		/* Phase 2: replay-playback tuple pull. */
		uint8_t* rp = (uint8_t*)replayptr;
		uint16_t new_rep_bufcnt = (uint16_t)(replaybuffercnt + 1);
		uint32_t new_rep_cntdown = replaytotalcntdown + 1;

		ReplayInputFrame frame;
		if (!TieReplayTiming_DecodeCurrentInputFrame(&frame)) {
			replay_stopreplay();
			return;
		}
		inputkey = (int16_t)frame.key;
		inputdeltax = frame.deltax;
		inputdeltay = frame.deltay;
		inputdeltaroll = frame.deltaroll;
		inputbuttons = (int16_t)frame.buttons;
		/* frame.frameticks is pacing metadata; the original DOS binary
		 * does not restore it into any engine global on playback. */

		replayptr = rp + REPLAYINPUTFRAME_DISK_SIZE;
		replaybuffercnt = new_rep_bufcnt;
		replaytotalcntdown = new_rep_cntdown;

		if (new_rep_cntdown < (uint32_t)replaytotalcnt) {
			if (new_rep_bufcnt >= REPLAY_INPUT_CHUNK_FRAMES) {
				if (!replay_loadreplayinput()) {
					replaytotalcntdown = (uint32_t)replaytotalcnt;
					replay_stopreplay();
					return;
				}
				replaybuffercnt = 0;
				replayptr = replaybufferstart;
			}
		} else {
			replay_stopreplay();
		}
	} else {
		/* Phase 3: raw input + meta-keys. */
		feinput_getrawinput();
		feinput_checkinput();
		uint16_t k = (uint16_t)inputkey;

		if (k >= KEY_ALT_P) {
			if (k == KEY_ALT_P) {
				/* Alt+P: pause. State machine — see pause_enter. */
				pause_enter();
				return;
			} else if (k < KEY_ALT_C) {
				/* nothing */
			} else if (k == KEY_ALT_C) {
				/* Alt+C: pause + options menu. */
				int16_t saved_vol = imuse_get_master_vol(im);
				imuse_set_master_vol(im, 0);
				imuse_pause(im);
				blank();
				uint16_t next_view;
				if (camera.view_zoom_flag) {
					lastpilotpaneldraw = -1;
					camera.pilotview = 0xFF;
					next_view = 18;
				} else {
					next_view = (camera.view_target_obj == pstate.object_idx) ? camera.pilotview : 18u;
					lastpilotpaneldraw = -1;
					camera.pilotview = 0xFF;
				}
				panelrts_setnewpilotview(next_view);
				msg_messageinit();
				imuse_set_master_vol(im, saved_vol);
				imuse_resume(im);
			} else if (k == KEY_ALT_V) {
				msg_messageprintf(MSG_TIE_VERSION);
			} else if (k == KEY_ALT_B) {
				/* Alt+B: brightness cycle. Steps by 64 within
				 * [256..704]; wraps 768 -> 256. */
				brightness_setting += 64;
				if (brightness_setting == 768)
					brightness_setting = 256;
				unblank();
				argtable[0] = (uint16_t)(((brightness_setting - 256) >> 6) + 1);
				msg_messageprintf(MSG_BRIGHTNESS_SET);
			}
		} else if (k == KEY_c && !hyperspaceflag && maingameflag && !pstate.hyperin_state) {
			/* 'c': flight recorder toggle. */
			fsfx_triggersfx(0x21u, 0xFFFF);
			if (recordingreplay) {
				if (!replayio_spoolreplayinput())
					replaytotalcnt -= replaybuffercnt;
				replaybuffercnt = 0;
				recordingreplay = 0;
				msg_messageprintf(MSG_REPLAY_CAMERA_OFF);
			} else if (replayio_copytosave("start.rpy")) {
				replaybuffercnt = 0;
				replaytotalcnt = 0;
				TieReplayRecording_ResetDuration();
				if (replayspoolflag)
					replayio_openreplayinputfile();
				replayptr = replaybufferstart;
				recordingreplay = 1;
				msg_messageprintf(MSG_REPLAY_CAMERA_ON);
				replayavailable = 1;
				replayrandomseed = (uint16_t)math2_randomseed;
			} else {
				msg_messageprintf(MSG_CAMERA_FAIL);
				replayavailable = 0;
			}
			calcframerate = 0;
			fsfx_triggersfx(0x21u, 0xFFFF);
		} else if (k == KEY_p) {
			/* 'p': pause (alternate binding). State machine — see pause_enter. */
			pause_enter();
			return;
		} else if (k == KEY_v && !hyperspaceflag && maingameflag && !pstate.hyperin_state) {
			/* 'v': replay screen. The spool flush, recording stop,
			 * blank, and CAMERA-OFF banner all run on this tick; the
			 * actual viewer push is deferred to the next flight-task
			 * step via s_replay_viewer_pending. The RESUMED banner
			 * is posted by the flight task after the viewer pops
			 * (see flight_mission_step). */
			if (replayavailable == 1) {
				if (recordingreplay) {
					if (!replayio_spoolreplayinput())
						replaytotalcnt -= replaybuffercnt;
					replaybuffercnt = 0;
					recordingreplay = 0;
					msg_messageprintf(MSG_REPLAY_CAMERA_OFF);
				}
				calcframerate = 0;
				blank();
				s_replay_viewer_pending = 1;
			} else {
				msg_messageprintf(MSG_FILM_NONE);
			}
		}

		/* Phase 4: append to record tape. */
		if (recordingreplay == 1) {
			const bool records_info_payload = !pstate.hyperin_state && !hyperspaceflag &&
											  TieReplayRecording_KeyStartsInfoPayload((uint16_t)inputkey);
			const uint16_t required_slots = records_info_payload ? 5 : 1;
			if (TieReplayRecording_PrepareSlots(required_slots, records_info_payload)) {
				uint8_t* rp = (uint8_t*)replayptr;
				ReplayInputFrame frame;
				/* One record represents the complete admitted PIT interval, even
				 * when it was accumulated from several shorter host calls. */
				frame.delta_us = (uint32_t)frameticks * 4000u;
				frame.key = (uint16_t)inputkey;
				frame.frameticks = (uint8_t)frameticks;
				if (camera.view_pitch_offset) {
					frame.deltax = 0;
					frame.deltay = 0;
					frame.deltaroll = 0;
					frame.buttons = 0;
				} else {
					frame.deltax = inputdeltax;
					frame.deltay = inputdeltay;
					frame.deltaroll = inputdeltaroll;
					frame.buttons = (uint8_t)(inputbuttons & 0xFF);
				}
				ReplayInputFrame_encode(rp, &frame);
				replayptr = rp + REPLAYINPUTFRAME_DISK_SIZE;
				user_nextreplaystore();
			}
		}
	}

	/* Flight controls are suppressed during hyperspace, but inputforplane still
	 * advances the hyperspace state machine. hyperin_state skips both. */
	if (acceleratedtimesetting <= 1u || !acceleratedtimectr) {
		if (pstate.hyperin_state) {
			if (pstate.player->ship_idx) {
				if (inputkey == KEY_h)
					mission.end_flag = 1;
			} else {
				mission.end_flag = 1;
			}
			/* binary skips USER_inputforplane in this branch */
		} else {
			if (!hyperspaceflag) {
				int16_t buttons_lo4 = (int16_t)(inputbuttons & 0xF);
				int16_t prev_buttons_lo4 = (int16_t)(pstate.prev_inputbuttons & 0xF);

				if ((inputbuttons & 0xD) == 1 && !camera.view_pitch_offset)
					laser_fireplayerweapon();

				/* Chord-release double-tap synthesis. */
				if (buttons_lo4 != 4 && prev_buttons_lo4 == 4)
					inputkey = 114;
				if (buttons_lo4 != 8 && prev_buttons_lo4 == 8)
					inputkey = 46;
				if (buttons_lo4 != 15 && prev_buttons_lo4 == 15)
					inputkey = 119;
				if (buttons_lo4 != 11 && prev_buttons_lo4 == 11)
					inputkey = 221;
				if (buttons_lo4 != 7 && prev_buttons_lo4 == 7)
					inputkey = 115;

				if ((inputbuttons & 0xE) == 2) {
					if ((pstate.prev_inputbuttons & 0xE) == 2)
						pstate.double_tap_timer = (uint16_t)(pstate.double_tap_timer + frameticks);
					else
						pstate.double_tap_timer = frameticks;
					pstate.prev_inputbuttons = inputbuttons;
					if (pstate.double_tap_timer < 0x3Bu)
						inputbuttons = (int16_t)(inputbuttons & 0xFD);
				} else {
					if ((pstate.prev_inputbuttons & 0xE) == 2 && pstate.double_tap_timer < 0x3Bu &&
						!mission.train_craft_type) {
						uint16_t auto_target = user_picktarget();
						if (auto_target != 0xFFFF)
							user_setnewtarget(auto_target);
					}
					pstate.prev_inputbuttons = inputbuttons;
					pstate.double_tap_timer = 0;
				}
			}
			user_inputforplane();
		}
	}
}

/* ================================================================== *
 *                  user_inflightinfo (info/options room)              *
 * ================================================================== */

/* Task form of user_inflightinfo. screen_id ∈ {0 goals, 1 map,
 * 2 messages, 3 damage, 4 wingmen, 5 help, 6 options}. Binary
 * 0x5F5E4. Phases:
 *   BEGIN       — simulator-mission gate (returns 0xFFFF cancel for
 *                 screens 0..4 in simulator missions); replay viewmode
 *                 deserializes the 4 side-payload records and exits;
 *                 otherwise pauses iMUSE and falls through to DISPATCH.
 *   DISPATCH    — load + paint the panel for `screen`, push the
 *                 appropriate sub-room task; transition AFTER_SUB.
 *   AFTER_SUB   — read user_submodal_result, fold mission.end_flag /
 *                 sub == 0 / sub == 0xFFFF / sub != 1 / sub == 1 into
 *                 the screen carousel state machine; loop to DISPATCH
 *                 or fall through to FINISH.
 *   FINISH      — blank + (if recording) write 4 side-payload records
 *                 + restore pilotview / iMUSE; pop with the final
 *                 screen index latched in user_submodal_result. */

typedef enum {
	INFLIGHT_PHASE_BEGIN = 0,
	INFLIGHT_PHASE_DISPATCH,
	INFLIGHT_PHASE_AFTER_SUB,
	INFLIGHT_PHASE_FINISH,
} InflightPhase;

typedef struct InflightInfoTask {
	int32_t screen_id;
	uint16_t saved_master_vol;
	int16_t retreat_flag;
	int16_t exit_flag;
	int32_t screen;
	TieFlightScreen previous_screen;
	InflightPhase phase;
} InflightInfoTask;

static int32_t inflight_replay_deserialize(void) {
	/* --- Replay-playback deserialization path. ---
	 * Four REPLAYINPUTFRAME_DISK_SIZE side-payload records, each
	 * consumed in full
	 * by user_nextreplaycount (which advances the replay cursor
	 * + handles the per-chunk wrap). The trailing
	 * pad bytes inside each slot are produced by the writer
	 * below; we just step over them. */
	uint8_t* rp;
	int32_t rep_return;

	/* Slot 1 — radar / target / key / screen. */
	if (!TieReplayTiming_CurrentRecordAvailable())
		goto corrupt_payload;
	rp = (uint8_t*)replayptr;
	pstate.radar_target0 = br_i16le(rp + 0);
	pstate.target_obj_idx = br_u16le(rp + 2);
	inputkey = br_i16le(rp + 4);
	rep_return = br_i16le(rp + 6);
	replayptr = rp + REPLAYINPUTFRAME_DISK_SIZE;
	user_nextreplaycount();

	/* Slot 2 — 10 bytes of pilot rank / kill state, 5 u16
	 * big-endian-of-pair values. */
	if (!TieReplayTiming_CurrentRecordAvailable())
		goto corrupt_payload;
	rp = (uint8_t*)replayptr;
	uint8_t* rec_dst = (uint8_t*)&pstate.player_total_kills;
	for (int i = 0; i < 10; i += 2) {
		int16_t w = br_i16le(rp + i);
		rec_dst[i + 2] = (uint8_t)(w >> 8);
		rec_dst[i + 3] = (uint8_t)(w & 0xFF);
	}
	replayptr = rp + REPLAYINPUTFRAME_DISK_SIZE;
	user_nextreplaycount();

	/* Slot 3 — detail / effect-flag bit-packing. */
	if (!TieReplayTiming_CurrentRecordAvailable())
		goto corrupt_payload;
	rp = (uint8_t*)replayptr;
	starshipexplodetail = br_u16le(rp);
	uint16_t pk1 = br_u16le(rp + 2);
	drawdebrisflag = (uint8_t)(pk1 & 0xF);
	pk1 >>= 4;
	drawbackdropflag = (uint8_t)(pk1 & 0xF);
	pk1 >>= 4;
	stardetaillevel = (uint16_t)(pk1 & 0xF);
	starshipdetail = (uint16_t)(pk1 >> 4);
	uint16_t pk2 = br_u16le(rp + 4);
	gouraudflag = (uint8_t)(pk2 & 0xFF);
	drawmarkingsflag = (uint8_t)((pk2 >> 8) & 0xF);
	shipdetailvalue = (int16_t)((pk2 >> 12) & 0xF);
	uint16_t pk3 = br_u16le(rp + 6);
	hyperspacedetail = (int16_t)(pk3 & 0xFF);
	shipdetailpolycnt = (uint16_t)((pk3 >> 8) & 0xFF);
	replayptr = rp + REPLAYINPUTFRAME_DISK_SIZE;
	user_nextreplaycount();

	/* Slot 4 — cheat flags + volume latches. */
	if (!TieReplayTiming_CurrentRecordAvailable())
		goto corrupt_payload;
	rp = (uint8_t*)replayptr;
	uint16_t cheats = br_u16le(rp);
	cheatingflag = (uint8_t)(cheats & 0xF);
	inflight_unlimited = (int8_t)((cheats >> 4) & 0xF);
	inflight_invulnerable = (int8_t)((cheats >> 8) & 0xF);
	inflight_collision = (int8_t)((cheats >> 12) & 0xF);
	uint16_t snd_pk = br_u16le(rp + 2);
	soundvolflag = (uint8_t)(snd_pk & 0xFF);
	inflight_sound_vol = (int8_t)(snd_pk >> 8);
	uint16_t mus_pk = br_u16le(rp + 4);
	musicvolflag = (uint8_t)(mus_pk & 0xFF);
	inflight_music_vol = (int8_t)(mus_pk >> 8);
	inflight_speech_vol = (int8_t)rp[6];
	replayptr = rp + REPLAYINPUTFRAME_DISK_SIZE;
	user_nextreplaycount();

	rtsvga2_invalidatepagecache();
	TieReplayTiming_Reset();
	return rep_return;

corrupt_payload:
	replay_stopreplay();
	return 0xFFFF;
}

static void inflight_finish(InflightInfoTask* t) {
	blank();

	if (recordingreplay) {
		/* Four REPLAYINPUTFRAME_DISK_SIZE side-payload records; data
		 * fills the leading 7-10 bytes (slot-specific), the trailing
		 * bytes are zeroed so the on-disk record is fully defined and
		 * the round-trip on the read path lands on the same bytes. */
		uint8_t* rp;

		/* Slot 1 — radar / target / key / screen. */
		rp = (uint8_t*)replayptr;
		bw_i16le(rp + 0, pstate.radar_target0);
		bw_i16le(rp + 2, (int16_t)pstate.target_obj_idx);
		bw_i16le(rp + 4, inputkey);
		bw_i16le(rp + 6, (int16_t)t->screen);
		memset(rp + 8, 0, REPLAYINPUTFRAME_DISK_SIZE - 8u);
		replayptr = rp + REPLAYINPUTFRAME_DISK_SIZE;
		TieReplayRecording_StoreRecord(false);

		/* Slot 2 — 10 bytes of pilot rank / kill state, 5 u16
		 * big-endian-of-pair values. */
		rp = (uint8_t*)replayptr;
		uint8_t* rec_src = (uint8_t*)&pstate.player_total_kills;
		for (int i = 0; i < 10; i += 2) {
			bw_u16le(rp + i, (uint16_t)(rec_src[i + 3] + (rec_src[i + 2] << 8)));
		}
		memset(rp + 10, 0, REPLAYINPUTFRAME_DISK_SIZE - 10u);
		replayptr = rp + REPLAYINPUTFRAME_DISK_SIZE;
		TieReplayRecording_StoreRecord(false);

		/* Slot 3 — detail / effect-flag bit-packing. */
		rp = (uint8_t*)replayptr;
		bw_u16le(rp, starshipexplodetail);
		uint16_t pack =
			(uint16_t)((drawdebrisflag & 0xF) +
					   (((drawbackdropflag & 0xF) + ((stardetaillevel & 0xF) + (starshipdetail << 4)) * 16) *
						16));
		bw_u16le(rp + 2, pack);
		bw_u16le(rp + 4, (uint16_t)(gouraudflag + ((drawmarkingsflag + (shipdetailvalue << 4)) << 8)));
		bw_u16le(rp + 6, (uint16_t)((uint8_t)hyperspacedetail + (shipdetailpolycnt << 8)));
		memset(rp + 8, 0, REPLAYINPUTFRAME_DISK_SIZE - 8u);
		replayptr = rp + REPLAYINPUTFRAME_DISK_SIZE;
		TieReplayRecording_StoreRecord(false);

		/* Slot 4 — cheat flags + volume latches. */
		rp = (uint8_t*)replayptr;
		bw_u16le(rp, (uint16_t)(cheatingflag + 16 * (inflight_unlimited + 16 * (inflight_invulnerable +
																				16 * inflight_collision))));
		bw_u16le(rp + 2, (uint16_t)(soundvolflag + (inflight_sound_vol << 8)));
		bw_u16le(rp + 4, (uint16_t)(musicvolflag + (inflight_music_vol << 8)));
		rp[6] = (uint8_t)inflight_speech_vol;
		memset(rp + 7, 0, REPLAYINPUTFRAME_DISK_SIZE - 7u);
		replayptr = rp + REPLAYINPUTFRAME_DISK_SIZE;
		TieReplayRecording_StoreRecord(false);
	}

	festring_setfontsize(2);
	uint16_t pilotview_restore;
	if (camera.view_zoom_flag) {
		camera.pilotview = 0xFF;
		lastpilotpaneldraw = -1;
		pilotview_restore = camera.view_heading_offset ? 20u : 18u;
	} else {
		pilotview_restore = (camera.view_target_obj == pstate.object_idx) ? camera.pilotview : 18u;
		lastpilotpaneldraw = -1;
		camera.pilotview = 0xFF;
	}
	panelrts_setnewpilotview(pilotview_restore);
	msg_messageinit();
	msg_messagerestore();
	if (TieProfile_UsesTie98Logic())
		g_flightInitialTextureCacheFlushPending = 1;
	fullupdateflag = 1;
	imuse_set_master_vol(im, (int16_t)t->saved_master_vol);
	imuse_resume(im);
	/* Retail USER_inflightinfo @ 0x61a53: force the next
	 * rtsvga2_setcurrentpage to re-program the VESA bank so the
	 * cockpit panel and HUD regain their pages after the info room. */
	rtsvga2_invalidatepagecache();
}

// FUNCTION: TIE 0x61544, TIE98 0x498430 (task-split recovery)
static LandruTaskStepResult user_inflightinfo_task_step(void* self) {
	InflightInfoTask* t = (InflightInfoTask*)self;

	switch (t->phase) {
		case INFLIGHT_PHASE_BEGIN: {
			if (mission.train_craft_type && (uint16_t)t->screen_id <= 4u) {
				/* Retail USER_inflightinfo @ 0x6155e/0x61563: drop VESA
				 * page cache then return the cancel sentinel 0xFFFF. */
				rtsvga2_invalidatepagecache();
				user_submodal_result = 0xFFFF;
				return LANDRU_TASK_STEP_DONE;
			}
			if (replayviewmode) {
				user_submodal_result = inflight_replay_deserialize();
				return LANDRU_TASK_STEP_DONE;
			}

			t->saved_master_vol = (uint16_t)imuse_get_master_vol(im);
			t->retreat_flag = 0;
			t->exit_flag = 0;
			t->screen = t->screen_id;
			if (TieProfile_UsesTie98Logic()) {
				mapflag = 1;
				FSFX_UpdatePlayerEngineSound();
				mapflag = 0;
			}
			imuse_set_master_vol(im, 0);
			imuse_pause(im);
			t->phase = INFLIGHT_PHASE_DISPATCH;
			return LANDRU_TASK_STEP_CONTINUE;
		}

		case INFLIGHT_PHASE_DISPATCH: {
			if (t->screen == 6) {
				TieRuntime_RequestSettingsMenu();
				t->phase = INFLIGHT_PHASE_FINISH;
				return LANDRU_TASK_STEP_CONTINUE;
			}
			TieFlightScreen_SetActive(flight_screen_from_index(t->screen));
			const bool tie98_display = TieClassicDisplay_UsesDx5();
			if (tie98_display)
				FlightSurface_Lock();
			int panel_idx = (int)((uint16_t)(t->screen + 21));
			if (!panelviewptrs[panel_idx].handle) {
				temppanelptr = newbuf;
				panel_loadcontrolpanel(panelviewdefs[panel_idx].name, &panelviewptrs[panel_idx].image, 3u);
			}
			buildpalette((const uint8_t*)panelviewptrs[panel_idx].palette, 0, 64);
			drawshape(panelviewptrs[panel_idx].image, 0, 0, 253, 0);
			festring_showscreen();
			if (tie98_display)
				FlightSurface_Unlock();

			/* Pre-seed the result for sub-modals we skip in simulator
			 * missions — staying in DISPATCH would loop forever. */
			user_submodal_result = 0;
			int pushed = 0;
			switch ((int16_t)t->screen) {
				case 0:
					if (!mission.train_craft_type) {
						goals_Push_MissionGoalsRoom_Task();
						pushed = 1;
					}
					break;
				case 1:
					if (!mission.train_craft_type) {
						/* maproom mutates pstate.target_obj_idx; old/new is
						 * compared on resume to call user_setnewtarget. */
						maproom_Push_MapRoom_Task();
						pushed = 1;
					}
					break;
				case 2:
					if (!mission.train_craft_type) {
						msgroom_Push_MessageRoom_Task();
						pushed = 1;
					}
					break;
				case 3:
					if (!mission.train_craft_type) {
						damage_Push_DamageRoom_Task();
						pushed = 1;
					}
					break;
				case 4:
					if (!mission.train_craft_type) {
						wingman_Push_WingmanRoom_Task();
						pushed = 1;
					}
					break;
				case 5:
					help_Push_HelpRoom_Task(t->retreat_flag);
					pushed = 1;
					break;
				default:
					break;
			}
			(void)pushed;
			t->phase = INFLIGHT_PHASE_AFTER_SUB;
			return LANDRU_TASK_STEP_CONTINUE;
		}

		case INFLIGHT_PHASE_AFTER_SUB: {
			int32_t sub = user_submodal_result;
			if (mission.end_flag) {
				festring_setfontsize(2);
				imuse_set_master_vol(im, (int16_t)t->saved_master_vol);
				imuse_resume(im);
				/* Retail USER_inflightinfo @ 0x616ec. */
				rtsvga2_invalidatepagecache();
				user_submodal_result = t->screen;
				return LANDRU_TASK_STEP_DONE;
			}

			if (sub == 0) {
				t->exit_flag = 1;
			} else if (sub == 0xFFFF) {
				t->screen--;
				if (t->screen & 0x8000)
					t->screen = 6;
				if (mission.train_craft_type && t->screen == 4)
					t->screen = 6;
				t->retreat_flag = 1;
			} else if (sub != 1) {
				t->screen = -1;
				t->exit_flag = 1;
			} else {
				if (++t->screen > 6)
					t->screen = mission.train_craft_type ? 5 : 0;
				t->retreat_flag = 0;
			}

			t->phase = t->exit_flag ? INFLIGHT_PHASE_FINISH : INFLIGHT_PHASE_DISPATCH;
			return LANDRU_TASK_STEP_CONTINUE;
		}

		case INFLIGHT_PHASE_FINISH:
			inflight_finish(t);
			user_submodal_result = t->screen;
			return LANDRU_TASK_STEP_DONE;
	}
	return LANDRU_TASK_STEP_DONE;
}

static void user_inflightinfo_task_end(void* self) {
	InflightInfoTask* t = (InflightInfoTask*)self;
	TieFlightScreen_SetActive(t->previous_screen);
}

static const LandruTaskVtable user_inflightinfo_task_vt = {
	.step = user_inflightinfo_task_step,
	.end = user_inflightinfo_task_end,
};

void user_Push_InflightInfo_Task(int32_t screen_id) {
	InflightInfoTask* t = (InflightInfoTask*)landru_task_push(&user_inflightinfo_task_vt);
	if (!t)
		return;
	t->previous_screen = TieFlightScreen_Active();
	t->screen_id = screen_id;
	t->saved_master_vol = 0;
	t->retreat_flag = 0;
	t->exit_flag = 0;
	t->screen = 0;
	t->phase = INFLIGHT_PHASE_BEGIN;
}

/* ================================================================== *
 *              user_inputforplane  key-cluster helpers                *
 * ================================================================== */

/* Trigger the 0x21 SFX "acknowledge" ping and nothing else. */
static void ui_ack_beep(void) { fsfx_triggersfx(0x21u, 0xFFFF); }

/* 'System damaged' helper: fills argtable + queues MSG_SYSTEM_STATUS. */
static void ui_system_damaged(uint16_t system_arg) {
	argtable[0] = system_arg;
	argtable[1] = 25;
	msg_messageprintf(MSG_SYSTEM_STATUS);
}

/* 'System not equipped' helper: fills argtable + queues NO_SUCH_SYSTEM. */
static void ui_no_such_system(uint16_t system_arg) {
	argtable[0] = system_arg;
	msg_messageprintf(MSG_NO_SUCH_SYSTEM);
}

/* Numpad camera direction key dispatch for keys 0x35..0x39 (binary 0x5B41F). */
static void ui_numpad_camera_key(uint16_t k) {
	if (replayviewmode)
		return;
	if (!camera.view_zoom_flag && camera.view_target_obj == pstate.object_idx)
		panelrts_setnewpilotview(camera.view_dir_dirty + (uint8_t)squarerootable[234 + k]);
	camera.side_angle = ((int32_t)camera.view_dir_dirty) << 10;
	camera.up_angle = squarerootable[215 + k];
}

/* '0': toggle between wing-level and 45-degree high-angle views. Binary 0x5B367. */
static void ui_toggle_high_angle_view(void) {
	if (replayviewmode)
		return;
	if (camera.pilotview >= 16u && !camera.view_zoom_flag)
		return;
	camera.view_dir_dirty ^= 8u;
	camera.side_angle = ((int32_t)camera.view_dir_dirty) << 10;
	if (!camera.view_zoom_flag && camera.view_target_obj == pstate.object_idx)
		panelrts_setnewpilotview((uint8_t)(camera.pilotview ^ 8));
}

/* 0x34 (') key: snap side view to 0x4000. Binary 0x5B3CC. */
static void ui_snap_side_view(void) {
	if (replayviewmode)
		return;
	camera.up_angle = 0;
	camera.side_angle = 0x4000;
	if (!camera.view_zoom_flag && camera.view_target_obj == pstate.object_idx)
		panelrts_setnewpilotview(0x10u);
}

/* '/' or F3: toggle the external camera. Binary 0x5B63D. */
static void ui_toggle_external_camera(void) {
	if (replayviewmode || camera.view_heading_offset)
		return;
	int was_zoomed_out = !camera.view_zoom_flag;
	camera.view_zoom_flag = !camera.view_zoom_flag;
	if (was_zoomed_out && camera.view_target_obj == pstate.object_idx) {
		camera.view_saved_idx = camera.pilotview;
		camera.view_saved_side_angle = camera.side_angle;
		camera.view_saved_up_angle = camera.up_angle;
	}
	user_resetview();
}

/* T (0x74) or 119 ('w') target-cycle step. Binary 0x5C6FD. */
static void ui_target_cycle(int16_t step) {
	uint16_t start =
		(pstate.target_obj_idx == 0xFFFF) ? (uint16_t)pstate.radar_target0 : pstate.target_obj_idx;
	uint16_t pick = user_picknexttarget(start, step);
	user_setnewtarget(pick);
}

/*
 * 'R' (0x72) -- find nearest enemy craft or mine-gun and target it.
 * Skips own-side craft, side-2/3/5 craft unless their mission-file hostile
 * tag is '1', GENUS 3/4/5 (passive: starship/mine/debris), craft with
 * status_flags == 0 (dead) or flight_flag not in {0, 6}. Also checks
 * static mine-gun turrets (ship_class==8). Binary 0x5C7E1.
 *
 * Watcom unaligned load: `*(int*)&fg[i].version >> 24` = byte at +0x37,
 * which is `side`. Rewritten as direct read below.
 */
static void ui_target_nearest_fighter_or_mine(void) {
	uint32_t best_dist = 0xFFFFFFFFu;
	uint16_t best_idx = 0xFFFF;

	/* Pass 1: craft slots 0..NUM_CRAFTS-1. */
	for (int16_t i = 0; i < NUM_CRAFTS; ++i) {
		if (!objects[i].ship_idx)
			continue;
		if (i == pstate.object_idx)
			continue;
		uint8_t side = objects[i].side;
		if (side == objects[pstate.object_idx].side)
			continue;
		if (side == 2 && mission_file_header.mission.neutral_name[0][0] != '1')
			continue;
		if (side == 3 && mission_file_header.mission.neutral_name[1][0] != '1')
			continue;
		if (side == 5 && mission_file_header.mission.neutral_name[3][0] != '1')
			continue;
		int g = objects[i].genus;
		if (g == 3 || g == 4 || g == 5)
			continue;
		CraftData* cp = objects[i].craft_ptr;
		if (!cp->status_flags)
			continue;
		uint8_t ff = cp->flight_flag;
		if (ff && ff != 6)
			continue;

		pai_distancebetween(pstate.object_idx, i);
		if (best_dist > (uint32_t)trig2_polardistance) {
			best_idx = (uint16_t)i;
			best_dist = (uint32_t)trig2_polardistance;
		}
	}

	/* Pass 2: static mine-gun turrets (ship_class == 8). */
	uint16_t k = 14336;
	for (int16_t m = 0; m < 64; ++m, ++k) {
		if (!staticobjects[m].species)
			continue;
		if (staticobjects[m].ship_class != 8)
			continue;
		if (!staticobjects[m].status_flags)
			continue;
		uint8_t fg_side = fg_array[staticobjects[m].fg_idx].side;
		if (fg_side == objects[pstate.object_idx].side)
			continue;

		pai_distancebetween(pstate.object_idx, k);
		if (best_dist > (uint32_t)trig2_polardistance) {
			best_idx = k;
			best_dist = (uint32_t)trig2_polardistance;
		}
	}

	user_setnewtarget(best_idx);
}

/*
 * 'U' (0x75) -- target the newest craft in the area. Per the QRC manual
 * this is "Select the newest craft in the area." In the engine, age_ticks
 * is monotonic-ish per spawn slot (set by CREATE on reinforcement arrival)
 * so the craft with the lowest age_ticks among FG leaders / standalone
 * craft is the most recently spawned. Only craft with leader_obj_idx == 255
 * (FG leader or solo) are eligible -- wingmen are filtered out so the
 * player jumps to the leader of a freshly-arrived wave, not a follower.
 * Binary 0x5C77E.
 */
static void ui_target_newest_craft(void) {
	uint16_t best_obj = 0xFFFF;
	uint16_t best_tick = 0xFFFF;
	for (int16_t i = 0; i < NUM_ACTIVE_CRAFT_SLOTS; ++i) {
		if (!objects[i].ship_idx)
			continue;
		if (i == pstate.object_idx)
			continue;
		CraftData* cp = objects[i].craft_ptr;
		if (cp->leader_obj_idx != 255)
			continue;
		/* Accept flight_flag in {0, 2, 6}; reject everything else. */
		uint8_t ff = cp->flight_flag;
		if (ff && ff != 2 && ff != 6)
			continue;
		uint16_t tick = (uint16_t)objects[i].age_ticks;
		if (tick < best_tick) {
			best_obj = (uint16_t)i;
			best_tick = tick;
		}
	}
	user_setnewtarget(best_obj);
}

/* Shield front/rear selector toggle (keys 0x72 = 's'). Binary 0x5C0D1. */
static void ui_cycle_shield_mode(void) {
	CraftData* pc = pstate.player_craft;
	if ((pc->subsystem_active & 1) == 0) {
		ui_no_such_system(35);
		return;
	}
	if ((pc->status_flags & 1) == 0) {
		ui_system_damaged(35);
		return;
	}
	uint8_t new_mode = (uint8_t)(pc->is_player_craft + 1);
	pc->is_player_craft = new_mode;
	if (new_mode > 2) {
		pc->is_player_craft = 0;
		user_adjustshields(0, 1);
	} else if (new_mode == 2) {
		user_adjustshields(1, 0);
	} else {
		/* Balanced (mode 1): split evenly, honoring easy-diff 4x cap. */
		int16_t cap_base = (int16_t)(2 * spec_data[pstate.player_spec_num].shield_points);
		int16_t points = spec_data[pstate.player_spec_num].shield_points;
		int16_t cap = cap_base;
		if (!mission.difficulty) {
			cap = (int16_t)(4 * spec_data[pstate.player_spec_num].shield_points);
			points = cap_base;
		}
		uint16_t pct = math2_percentage(points, cap);
		int16_t total = (int16_t)(pc->forward_shield + pc->rear_shield);
		if (total > 0) {
			int16_t fwd = (int16_t)math2_fraction(total, pct);
			pc->forward_shield = fwd;
			pc->rear_shield = (int16_t)(total - fwd);
		}
	}
	argtable[0] = (uint16_t)(pc->is_player_craft + 14);
	msg_messageprintf(MSG_SHIELDS_SET);
	ui_ack_beep();
}

/* Cannon-rate cycle (keys 0xC3 = F10 / ']'). Binary 0x5BE3D. */
static void ui_cycle_cannon_rate(void) {
	CraftData* pc = pstate.player_craft;
	uint8_t nr = (uint8_t)(pc->laser_power + 1);
	pc->laser_power = nr;
	if (nr >= 5)
		pc->laser_power = 0;

	MsgTemplate msg;
	if (pc->status_flags & 0x10) {
		argtable[0] = (uint16_t)(pc->laser_power + 19);
		msg = MSG_CANNON_RATE;
	} else {
		argtable[0] = 29;
		argtable[1] = 25;
		msg = MSG_SYSTEM_STATUS;
	}
	msg_messageprintf(msg);
	ui_ack_beep();
}

/* Shield-rate cycle (key 0xC4). Binary 0x5C272. */
static void ui_cycle_shield_rate(void) {
	CraftData* pc = pstate.player_craft;
	if ((pc->subsystem_active & 1) == 0) {
		ui_no_such_system(35);
		return;
	}
	if ((pc->status_flags & 1) == 0) {
		ui_system_damaged(35);
		return;
	}
	uint8_t nr = (uint8_t)(pc->shield_power + 1);
	pc->shield_power = nr;
	if (nr >= 5)
		pc->shield_power = 0;
	argtable[0] = (uint16_t)(pc->shield_power + 19);
	msg_messageprintf(MSG_SHIELD_RATE);
	ui_ack_beep();
}

/* Beam-rate cycle (key 0xC2). Binary 0x5C615. */
static void ui_cycle_beam_rate(void) {
	CraftData* pc = pstate.player_craft;
	if ((pc->subsystem_active & 0x100) == 0) {
		ui_no_such_system(32);
		return;
	}
	if ((pc->status_flags & 0x100) == 0) {
		ui_system_damaged(32);
		return;
	}
	uint8_t nr = (uint8_t)(pc->beam_power + 1);
	pc->beam_power = nr;
	if (nr >= 5)
		pc->beam_power = 0;
	argtable[0] = (uint16_t)(pc->beam_power + 19);
	msg_messageprintf(MSG_BEAM_RATE);
	ui_ack_beep();
}

/* Beam toggle (key 0x62 = 'b'). Binary 0x5C55A. */
static void ui_toggle_beam(void) {
	CraftData* pc = pstate.player_craft;
	if ((pc->subsystem_active & 0x100) == 0) {
		ui_no_such_system(32);
		return;
	}
	if ((pc->status_flags & 0x100) == 0) {
		ui_system_damaged(32);
		return;
	}
	pc->beam_state ^= 0x80u;
	argtable[0] = (uint16_t)(pc->beam_type + 192);
	msg_messageprintf((pc->beam_state & 0x80) ? MSG_BEAM_ON : MSG_BEAM_OFF);
}

/* 'Reinforce' confirm on key 83 = 'S'. Binary 0x5D63B. */
static void ui_reinforce_request(void) {
	if (pstate.space_confirm_action)
		return;
	int reinforce_avail = 0;
	for (uint16_t i = 0; i < (uint16_t)mission_file_header.num_fg; ++i) {
		/* Watcom unaligned load on fg_array[i].link_code: the +3 byte is
		 * start_cond[0].type (byte 0x4A), same byte both paths check. */
		if (fg_array[i].link_code == 20 || fg_array[i].start_cond[0].type == 20) {
			reinforce_avail = 1;
			break;
		}
	}
	if (!reinforce_avail) {
		msg_messageprintf(MSG_NO_REINFORCEMENTS);
		fsfx_triggervoicesfx(0x66u);
		fsfx_triggervoicesfx(0x67u);
		fsfx_triggervoicesfx(0x69u);
		return;
	}
	if (mission.penalty_flag) {
		msg_messageprintf(MSG_REINFORCE_ALREADY_USED);
		fsfx_triggervoicesfx(0x66u);
		fsfx_triggervoicesfx(0x67u);
		fsfx_triggervoicesfx(0x69u);
	} else {
		msg_messageprintf(MSG_REINFORCE_PROMPT);
		pstate.space_confirm_action = 3;
		timers[TIMER_SPACE_CONFIRM] = 1888;
	}
}

/* SPACE: confirm pending action. Binary 0x5B22B. */
static void ui_space_confirm(void) {
	if (!pstate.space_confirm_action)
		return;
	if (pstate.space_confirm_action == 1) {
		pstate.target_obj_idx = (uint16_t)pstate.msg_arg_obj_idx;
		if (!replayviewmode && camera.view_heading_offset)
			camera.view_target_obj = pstate.target_obj_idx;
		pstate.radar_subtarget_state = 0;
		pstate.player_craft->missile_count_total = 0;
		msg_messageprintf(MSG_WARHEAD_TARGETED);
		pstate.space_confirm_action = 0;
	} else if (pstate.space_confirm_action == 2) {
		if (recordingreplay) {
			if (!replayio_spoolreplayinput())
				replaytotalcnt -= replaybuffercnt;
			replaybuffercnt = 0;
			recordingreplay = 0;
			msg_messageprintf(MSG_REPLAY_CAMERA_OFF);
			calcframerate = 0;
		}
		mission.end_flag = 1;
		mission.player_status = 3;
	} else if (pstate.space_confirm_action == 3) {
		mission.penalty_flag = 1;
		msg_messageprintf(MSG_REINFORCE_ACK);
		pstate.space_confirm_action = 0;
		fsfx_triggervoicesfx(0x66u);
		fsfx_triggervoicesfx(0x67u);
		fsfx_triggervoicesfx(0x68u);
	}
}

/* F2: select or cycle the view through launched warheads. Binary 0x5B539. */
static void ui_cycle_warhead_view(void) {
	frameticksmsgflag = !frameticksmsgflag;
	if (replayviewmode || camera.view_heading_offset)
		return;

	uint16_t scan =
		(camera.view_target_obj == pstate.object_idx) ? (uint16_t)(NUM_CRAFTS - 1) : camera.view_target_obj;
	uint16_t found = 0xFFFF;
	for (int k = NUM_CRAFTS; k < WARHEAD_SLOT_END; ++k) {
		scan = (uint16_t)(scan + 1);
		if (scan >= WARHEAD_SLOT_END)
			scan = NUM_CRAFTS;
		if (objects[scan].genus == 6 &&
			projectile_is_warhead_type[laser_species_idx(objects[scan].ship_idx)]) {
			if (pstate.object_idx == (uint16_t)objects[scan].self_idx)
				found = scan;
			break;
		}
	}
	if (found != 0xFFFF) {
		if (camera.view_target_obj == pstate.object_idx) {
			camera.view_saved_idx = camera.pilotview;
			camera.view_saved_side_angle = camera.side_angle;
			camera.view_saved_up_angle = camera.up_angle;
			camera.view_zoom_flag = 0;
		}
		camera.view_target_obj = found;
		user_resetview();
	}
}

/* Target viewer toggle (0x7A = 'z'/Y). Binary 0x5B6DF. */
static void ui_target_viewer_toggle(void) {
	if (replayviewmode)
		return;
	if (camera.view_heading_offset) {
		camera.view_heading_offset = 0;
		targetblinkflag = 0;
		camera.view_zoom_flag = (camera.view_saved_idx == 18);
		lasttargetnum = -2;
		camera.view_target_obj = pstate.object_idx;
		user_resetview();
	} else if (pstate.target_obj_idx == 0xFFFF) {
		msg_messageprintf(MSG_NO_TARGET);
	} else {
		if (!camera.view_zoom_flag) {
			camera.view_saved_idx = camera.pilotview;
			camera.view_saved_side_angle = camera.side_angle;
			camera.view_saved_up_angle = camera.up_angle;
		}
		camera.view_zoom_flag = 1;
		camera.view_heading_offset = 1;
		camera.view_target_obj = pstate.target_obj_idx;
		targetblinkflag = 1024;
		user_resetview();
	}
}

/* 'H' (0x92) -> eject or surrender. Binary 0x5D034. */
static void ui_eject_or_surrender(void) {
	if (mission.train_craft_type) {
		if (recordingreplay) {
			if (!replayio_spoolreplayinput())
				replaytotalcnt -= replaybuffercnt;
			replaybuffercnt = 0;
			recordingreplay = 0;
			msg_messageprintf(MSG_REPLAY_CAMERA_OFF);
			calcframerate = 0;
		}
		mission.end_flag = 1;
		mission.player_status = 3;
		return;
	}
	if ((pstate.player_craft->status_flags & 2) == 0) {
		ui_system_damaged(34);
		return;
	}
	if (hyperspaceflag || replayviewmode)
		return;

	int16_t status;
	if (user_isrescued(pstate.object_idx)) {
		status = 0;
		mission.player_status = 2;
	} else {
		status = 1;
		mission.player_status = 1;
	}
	fediskio_updatepilotrecord(status, 1);
	user_ejectcamera();
	int sp = pstate.player_craft->species_idx;
	uint16_t spin = (uint16_t)math2_getrandom();
	spin = (uint16_t)(((uint8_t)((spin >> 8) & 0x3F) + 32) << 8);
	while (spin > (uint16_t)spec_data[sp].max_spin_rate)
		spin >>= 1;
	pstate.player->spin_rate = (int16_t)spin;
	pstate.player_craft->flight_flag = 3;
	uint16_t rnd = (uint16_t)math2_getrandom();
	pstate.player->death_timer = (int16_t)(236 * ((rnd & 3) + 3));
	TIE_FLIGHT_TRACE_DEATH(pstate.object_idx, 0xFFFFu, TIE_TRACE_DEATH_EJECTED,
						   pstate.player->death_timer);
}

/*
 * No-hyperdrive "return to X" helper. Called when the ship spec has
 * no hyperdrive installed. Scans for primary/secondary stop-FG craft
 * in the player's own FG record and announces where to return.
 *
 * Watcom unaligned-dword loads from the binary:
 *   *(int *)&fg[i].cur_start_fg + 1 >> 24 -> pri_stop_fg   (byte at +0x62)
 *   *(int *)&fg[i].start_fg_used    >> 24 -> sec_stop_fg   (byte at +0x64)
 * Rewritten as direct field reads below.
 */
static void ui_no_hyperdrive_return(void) {
	EFGStruct* pfg = &fg_array[pstate.player_fg_idx];
	uint16_t pri_stop_obj = 0xFFFF;
	uint16_t sec_stop_obj = 0xFFFF;

	for (uint16_t i = 0; i < NUM_ACTIVE_CRAFT_SLOTS; ++i) {
		if (pfg->pri_stop_fg_used && objects[i].ship_idx && objects[i].fg_idx == pfg->pri_stop_fg)
			pri_stop_obj = i;
		if (pfg->sec_stop_fg_used && objects[i].ship_idx && objects[i].fg_idx == pfg->sec_stop_fg)
			sec_stop_obj = i;
	}

	if (pri_stop_obj != 0xFFFF && sec_stop_obj != 0xFFFF) {
		msg_createobjectname(pri_stop_obj, 0, tempstring);
		msg_addmessageptr(0, tempstring);
		msg_createobjectname(sec_stop_obj, 0, temp2string);
		msg_addmessageptr(1u, temp2string);
		msg_messageprintf(MSG_NO_HYPERDRIVE_RETURN_ALT);
		return;
	}
	uint16_t obj = (pri_stop_obj != 0xFFFF) ? pri_stop_obj : sec_stop_obj;
	if (obj == 0xFFFF)
		return; /* neither found -> silent. */
	msg_createobjectname(obj, 0, tempstring);
	msg_addmessageptr(0, tempstring);
	msg_messageprintf(MSG_NO_HYPERDRIVE_RETURN);
}

/* Hyperspace jump (key 0x68 = 'h'). */
static void ui_hyperspace(void) {
	if ((pstate.player_craft->subsystem_active & 0x80) == 0) {
		ui_no_hyperdrive_return();
		return;
	}
	if (mission.train_craft_type) {
		if (recordingreplay) {
			if (!replayio_spoolreplayinput())
				replaytotalcnt -= replaybuffercnt;
			replaybuffercnt = 0;
			recordingreplay = 0;
			msg_messageprintf(MSG_REPLAY_CAMERA_OFF);
			calcframerate = 0;
		}
		mission.end_flag = 1;
		mission.player_status = 3;
		return;
	}
	if ((pstate.player_craft->status_flags & 0x80) == 0) {
		ui_system_damaged(36);
		return;
	}

	int interdictor = 0;
	for (int i = 0; i < NUM_CRAFTS; ++i) {
		if (objects[i].ship_idx == 51 && objects[i].side != pstate.player->side)
			interdictor = 1;
	}
	if (interdictor) {
		msg_messageprintf(MSG_INTERDICTOR_BLOCK);
		return;
	}
	fscript_MsSetSequence(17);
	msg_clearmessagequeue();
	msg_messageprintf(MSG_HYPER_PREP);
	hyperspaceflag = 1;
	hyperabortflag = 1;
	camera.view_zoom_flag = 0;
	camera.view_target_obj = pstate.object_idx;
	camera.view_pitch_offset = 0;
	panelrts_setnewpilotview(0);
	camera.side_angle = 0;
	camera.up_angle = 0;
	hyperticks = 0;
	ui_ack_beep();
}

/* Weapon cycle (0x77 = 'w'). Binary 0x5BB0D. */
static void ui_cycle_weapon_group(void) {
	CraftData* pc = pstate.player_craft;
	uint8_t nwg = (++pstate.player_weapon_group);
	if (pstate.player_weapon_mode) {
		if (nwg >= pc->missile_group_cnt) {
			if (pc->laser_group_cnt)
				pstate.player_weapon_mode = 0;
			pstate.player_weapon_group = 0;
		}
	} else if (nwg >= pc->laser_group_cnt) {
		pstate.player_weapon_mode = (uint8_t)(pc->missile_group_cnt != 0);
		pstate.player_weapon_group = 0;
	}

	argtable[1] = 25;
	MsgTemplate msg;
	if (pstate.player_weapon_mode) {
		if (pc->status_flags & 8) {
			int32_t w = pc->warhead_type[pstate.player_weapon_group];
			argtable[0] = (uint16_t)user_mapmissiletomessage((uint8_t)w, 0);
			msg = MSG_LAUNCHERS_ARMED;
		} else {
			argtable[0] = 31;
			msg = MSG_SYSTEM_STATUS;
		}
	} else if (pc->status_flags & 0x10) {
		msg = (MsgTemplate)(pstate.player_weapon_group + 3);
	} else {
		argtable[0] = (uint16_t)(pstate.player_weapon_group + 29);
		msg = MSG_SYSTEM_STATUS;
	}
	msg_messageprintf(msg);
	ui_ack_beep();
}

/* 'x': cycle cannon linking or the selected warhead firing mode. Binary 0x5BC9B. */
static void ui_cycle_weapon_firing_mode(void) {
	CraftData* pc = pstate.player_craft;
	if (pstate.player_weapon_mode) {
		pc->missile_armed[pstate.player_weapon_group] ^= 2u;
		int32_t w = pc->warhead_type[pstate.player_weapon_group];
		argtable[0] = (uint16_t)user_mapmissiletomessage((uint8_t)w, 0);
		MsgTemplate m = (MsgTemplate)(((pc->missile_armed[pstate.player_weapon_group] & 0x7F) >> 1) + 6);
		msg_messageprintf(m);
		return;
	}
	if (spec_data[pstate.player_spec_num].laser_count[pstate.player_weapon_group] == 1) {
		ui_ack_beep();
		return;
	}
	uint16_t nl = (uint16_t)(pc->laser_owner_player[pstate.player_weapon_group] + 1);
	if (nl > 3)
		nl = 1;
	if (spec_data[pstate.player_spec_num].laser_count[pstate.player_weapon_group] != 4 && nl == 2)
		nl = 3;
	pc->laser_owner_player[pstate.player_weapon_group] = (uint8_t)nl;
	pc->laser_first_slot[pstate.player_weapon_group] =
		spec_data[pstate.player_spec_num].laser_start[pstate.player_weapon_group];
	msg_messageprintf((MsgTemplate)(nl + 9));
	ui_ack_beep();
}

/* Overdrive (SLAM) toggle on 0x6E 'n'. Binary 0x5BA4C. */
static void ui_overdrive_toggle(void) {
	CraftData* pc = pstate.player_craft;
	if (pstate.player_spec_num != spec_getspecnum(0xCu))
		return;
	int slam_was_off = (pc->slam_active == 0xFFFF);
	pc->slam_active = (uint16_t)~pc->slam_active;
	if (!slam_was_off) {
		msg_messageprintf(MSG_OVERDRIVE_DISENGAGED);
		fsfx_triggersfx(0x6Cu, 0xFFFF);
		return;
	}
	int has_charge = 0;
	int n = pc->weapon_group_cnt;
	for (int i = 0; i < n; ++i) {
		if ((int8_t)pc->weapon_slots[i].charge > 0) {
			has_charge = 1;
			break;
		}
	}
	if (has_charge) {
		msg_messageprintf(MSG_OVERDRIVE_ENGAGED);
		fsfx_triggersfx(0x6Bu, 0xFFFF);
	} else {
		pc->slam_active = 0xFFFF;
		msg_messageprintf(MSG_OVERDRIVE_FAIL);
	}
}

/* Radar toggle 0x69 'i'. Binary 0x5CB40. */
static void ui_toggle_radar(void) {
	pstate.radar_enable ^= 1u;
	argtable[0] = (uint16_t)(pstate.radar_enable + 86);
	msg_messageprintf(MSG_CMD_TRACK_TOGGLE);
	ui_ack_beep();
}

/* Match-speed key (enter). Binary 0x5B918. */
static void ui_match_speed(void) {
	CraftData* pc = pstate.player_craft;
	if (pstate.target_obj_idx == 0xFFFF)
		return;
	if (pstate.target_obj_idx >= 0x3800u) {
		pc->throttle_speed = 0;
		return;
	}
	if (pstate.target_obj_idx >= NUM_ACTIVE_CRAFT_SLOTS) {
		pc->throttle_speed = 0xFFFF;
		return;
	}
	uint16_t cur = (uint16_t)objects[pstate.target_obj_idx].current_speed;
	uint16_t slack = (uint16_t)(6 - (pc->shield_power + pc->beam_power + pc->laser_power));
	uint16_t maxs = (uint16_t)pc->max_speed_cache;
	uint16_t match_speed;
	if (slack < 0x8000u) {
		match_speed =
			(uint16_t)((uint16_t)pc->max_speed_cache + math2_fraction((int16_t)(slack << 13), maxs));
	} else {
		uint16_t adj = math2_fraction((int16_t)(-8192 * slack), maxs);
		match_speed = (uint16_t)(pc->max_speed_cache - adj);
	}
	if (cur < match_speed) {
		uint16_t pct = math2_percentage(cur, match_speed);
		pc->throttle_speed = pct;
		msg_messageprintf(MSG_MATCHING_SPEED);
	} else {
		pc->throttle_speed = 0xFFFF;
		msg_messageprintf(MSG_MATCHING_SPEED_MAX);
	}
}

/*
 * Axis-driven throttle. The deflected stick steps
 * throttle by a fixed amount per frame, scaled by deflection
 * magnitude. Center-stick = no change (deflection is zero after the
 * ±12 dead zone applied at sampling time, so small jitter is
 * already filtered out).
 *
 * Full deflection changes the throttle by approximately 254 per frame.
 *
 * No interaction with `inputbuttons`-modifier throttle — both paths
 * are additive. Holding the modifier + pushing the axis stacks both
 * nudges in a single frame.
 */
static void ui_apply_throttle_axis(void) {
	TieUserTimingState* high_rate = TieFlightTiming_IsHighRate() ? TieFlightTimingState_User() : NULL;
	if (!joystickthrottle) {
		if (high_rate) {
			high_rate->throttle_remainder[1] = 0;
			high_rate->throttle_sign[1] = 0;
		}
		return;
	}
	if ((pstate.player_craft->status_flags & 0x20) == 0) {
		if (high_rate) {
			high_rate->throttle_remainder[1] = 0;
			high_rate->throttle_sign[1] = 0;
		}
		return;
	}

	int32_t step = (int32_t)joystickthrottle * 2;
	if (high_rate)
		step = TieUserTiming_ScaleCompatibilityIncrement(step, &high_rate->throttle_remainder[1],
														 &high_rate->throttle_sign[1]);
	uint16_t cur = pstate.player_craft->throttle_speed;
	int32_t nxt = (int32_t)cur + step;
	if (nxt < 0)
		nxt = 0;
	if (nxt > 65535)
		nxt = 65535;
	pstate.player_craft->throttle_speed = (uint16_t)nxt;
}

/* Roll keys 1,2 (left/right). Binary 0x5D7DC. */
static void ui_roll_input(int16_t direction_key) {
	int roll_pct = math2_percentage(pstate.player_craft->roll_rate_cache, 0x3000u) >> 1;
	int scale = (direction_key == 1) ? 53248 : 12288;
	int q15 = (roll_pct * scale) >> 15;
	int16_t rd = (int16_t)math2_ABoverC32((int16_t)q15, frameticks, 236);
	objects[pstate.object_idx].roll = (int16_t)(objects[pstate.object_idx].roll - rd);
}

/*
 * Transfer energy between weapons/shields (keys 59 -> shield->cannon,
 * 119 ('w'+SHIELDS_TO_CANNON) and counterpart 0xC4 scancode for
 * cannon->shields). Binary 0x5BEDE / 0x5C2F3.
 */
static void ui_xfer_cannon_to_shields(void) {
	CraftData* pc = pstate.player_craft;
	/* Bail if shields are offline. */
	if ((pc->subsystem_active & 1) == 0) {
		ui_no_such_system(35);
		return;
	}
	if ((pc->status_flags & 1) == 0) {
		ui_system_damaged(35);
		return;
	}
	int16_t cap = (int16_t)(2 * spec_data[pstate.player_spec_num].shield_points);
	if (!mission.difficulty)
		cap *= 2;

	int16_t need;
	if (pc->is_player_craft) {
		if (pc->is_player_craft == 2)
			need = (int16_t)(cap - pc->rear_shield);
		else {
			need = (int16_t)(cap - pc->rear_shield + cap - pc->forward_shield);
			if (need & 0x8000)
				need = 800;
		}
	} else {
		need = (int16_t)(cap - pc->forward_shield);
	}
	if (need > 800)
		need = 800;

	int step, cycles;
	if (pstate.player_spec_num == spec_getspecnum(0xCu)) {
		cycles = need >> 5;
		step = 32;
	} else {
		cycles = need >> 3;
		step = 8;
	}
	if (cycles == 0)
		return;

	int k = 0;
	for (int i = 0; i < 100 && cycles > 0; ++i) {
		int8_t ch = (int8_t)pc->weapon_slots[k].charge;
		if (ch > 0) {
			pc->weapon_slots[k].charge = (int8_t)(ch - 1);
			--cycles;
			if (!pc->is_player_craft)
				pc->forward_shield += (int16_t)step;
			else if (pc->is_player_craft == 2)
				pc->rear_shield += (int16_t)step;
			else {
				pc->forward_shield += (int16_t)(step / 2);
				pc->rear_shield += (int16_t)(step / 2);
			}
		}
		if ((uint16_t)++k >= (uint16_t)pc->weapon_group_cnt)
			k = 0;
	}
	ui_ack_beep();
	msg_messageprintf(MSG_XFER_CANNON_TO_SHIELDS);
}

static void ui_xfer_shields_to_cannon(void) {
	CraftData* pc = pstate.player_craft;
	if ((pc->subsystem_active & 1) == 0) {
		ui_no_such_system(35);
		return;
	}
	int charge_sum = 0;
	int n = pc->weapon_group_cnt;
	for (int j = 0; j < n; ++j)
		charge_sum += 127 - (int8_t)pc->weapon_slots[j].charge;
	int step = (pstate.player_spec_num == spec_getspecnum(0xCu)) ? 32 : 8;
	if (charge_sum > 100)
		charge_sum = 100;
	int xfer_amount = step * charge_sum;
	int xfer_total;
	if (!pc->is_player_craft) {
		int16_t fwd = pc->forward_shield;
		if (xfer_amount > fwd)
			xfer_amount = fwd;
		xfer_total = xfer_amount;
		pc->forward_shield -= (int16_t)xfer_amount;
	} else if (pc->is_player_craft == 2) {
		int16_t rear = pc->rear_shield;
		if (xfer_amount > rear)
			xfer_amount = rear;
		xfer_total = xfer_amount;
		pc->rear_shield -= (int16_t)xfer_amount;
	} else {
		/* Balanced mode. The binary tests different quantities on the
		 * forward vs rear halves:
		 *   forward: if (half > fwd) half = fwd;
		 *   rear:    if (xfer_amount > rear) tmp = rear;  (NOT half > rear)
		 * The rear branch therefore drains rear completely when the full
		 * xfer exceeds rear shield -- even though only half was intended. */
		int16_t fwd = pc->forward_shield;
		int half_fwd = xfer_amount >> 1;
		if (half_fwd > fwd)
			half_fwd = fwd;
		pc->forward_shield -= (int16_t)half_fwd;

		int16_t rear = pc->rear_shield;
		int half_rear = xfer_amount >> 1;
		if (xfer_amount > rear)
			half_rear = rear;
		pc->rear_shield -= (int16_t)half_rear;
		xfer_total = half_fwd + half_rear;
	}
	int pips = xfer_total / step;
	if (!pips)
		return;
	int k = 0;
	for (int i = 0; i < 100 && pips > 0; ++i) {
		/* Watcom unaligned load on weapon_slots[k]: +80 offset = ammo byte */
		if ((int8_t)pc->weapon_slots[k].ammo != 127)
			pc->weapon_slots[k].charge++;
		++k;
		--pips;
		if (k >= pc->weapon_group_cnt)
			k = 0;
	}
	ui_ack_beep();
	msg_messageprintf(MSG_XFER_SHIELDS_TO_CANNON);
}

/* Cycle radar component target forward/backward. Binary 0x5CA14 / 0x5CAAE. */
static void ui_cycle_radar_target1(int step) {
	if (pstate.target_obj_idx >= NUM_ACTIVE_CRAFT_SLOTS)
		return;
	CraftData* cp = objects[pstate.target_obj_idx].craft_ptr;
	const uint8_t model_type = objects[pstate.target_obj_idx].ship_idx;
	if (!TieProfile_UsesTie98Logic())
		draw_lockshipfileptrs(model_type);
	int nm = TieProfile_UsesTie98Logic() ? modelmesh_getcount(model_type) : objectblockptr->num_meshes;
	int guard = nm;
	do {
		if (--guard < 0)
			break;
		int rt = pstate.radar_target1 + step;
		if (rt >= nm)
			rt = 0;
		if (rt < 0)
			rt = nm - 1;
		pstate.radar_target1 = (int16_t)rt;
	} while (cp->mesh_state[pstate.radar_target1] != MESH_STATE_VISIBLE ||
			 (TieProfile_UsesTie98Logic() ? !user_validcomponent_tie98(model_type, pstate.radar_target1)
										  : !user_validcomponent(pstate.radar_target1)));
}

/* ================================================================== *
 *                   user_inputforplane (dispatcher)                   *
 * ================================================================== */

/* Orientation update at the end of the per-frame dispatch. Binary 0x5D835. */
static void ui_apply_view_or_flight_input(void) {
	if (camera.view_pitch_offset) {
		TieUserTimingState* high_rate = TieFlightTiming_IsHighRate() ? TieFlightTimingState_User() : NULL;
		int16_t up_delta = high_rate ? TieUserTiming_ScaleValue(inputdeltax, &high_rate->view_remainder[0])
									 : (int16_t)math2_ABoverC32(inputdeltax, frameticks, 236);
		int16_t side_delta = high_rate ? TieUserTiming_ScaleValue(inputdeltay, &high_rate->view_remainder[1])
									   : (int16_t)math2_ABoverC32(inputdeltay, frameticks, 236);
		camera.up_angle += up_delta;
		camera.side_angle = (int16_t)(camera.side_angle + side_delta);
		int zoom_btn = inputbuttons & 0xF;
		if (zoom_btn == 1 || zoom_btn == 2) {
			if (high_rate) {
				const uint32_t numerator = 32u * frameticks + high_rate->zoom_rate_remainder;
				camera.view_zoom_rate += (int16_t)(numerator / TieFlightTiming_CompatibilityTicks());
				high_rate->zoom_rate_remainder = (uint16_t)(numerator % TieFlightTiming_CompatibilityTicks());
			} else {
				camera.view_zoom_rate += 32;
			}
			if ((uint16_t)camera.view_zoom_rate > 0x400u)
				camera.view_zoom_rate = 1024;
			int32_t delta = high_rate
								? TieUserTiming_ScaleValue(camera.view_zoom_rate, &high_rate->zoom_remainder)
								: math2_ABoverC32(camera.view_zoom_rate, frameticks, 236);
			int32_t hiw = camera.view_zoom;
			if (zoom_btn == 1) {
				hiw -= delta;
				if (hiw < 48)
					hiw = 48;
			} else {
				hiw += delta;
				if (hiw > 5120)
					hiw = 5120;
			}
			camera.view_zoom = (int16_t)hiw;
		} else {
			camera.view_zoom_rate = 32;
			if (high_rate) {
				high_rate->zoom_remainder = 0;
				high_rate->zoom_rate_remainder = 0;
			}
		}
		return;
	}

	/* Watcom emits `xor eax,eax; mov ax,inputdeltax; imul eax,ebx; sar eax,15`
	 * for both axes — i.e. the inputdelta is unsigned-loaded to a 32-bit reg.
	 * For negative inputdelta the int32 result has bit-15 set, so the LOW 16
	 * bits, reinterpreted as int16, carry the correctly signed slew target.
	 * The binary's slew arithmetic at 0x5F886+ then operates only on the low
	 * 16 (sub bx,ax / test bx,bx / movsx edx,ax), discarding the poisoned
	 * upper half. Using the full int32 here would feed values up to 65533
	 * into a slew toward an int16 axis_*_accum, overshooting and wrapping
	 * every few frames — the "mouse-left banks right + flicker" symptom. */
	int16_t x_input = (int16_t)(((math2_percentage(pstate.player_craft->roll_rate_cache, 0x3000u) >> 1) *
								 (uint16_t)inputdeltax) >>
								15);
	int16_t y_input = (int16_t)(((math2_percentage(pstate.player_craft->heading_rate_cache, 0x1000u) >> 1) *
								 (uint16_t)inputdeltay) >>
								15);
	/* Analog roll input from the second-stick axis. Uses roll_rate_cache
	 * like the X-input modifier path so a fully-deflected stick produces
	 * the same per-tick rotation the held-button roll mode produces. */
	int16_t roll_input = (int16_t)(((math2_percentage(pstate.player_craft->roll_rate_cache, 0x3000u) >> 1) *
									(uint16_t)inputdeltaroll) >>
								   15);
	if ((pstate.player_craft->status_flags & 0x20) == 0) {
		x_input = 0;
		y_input = 0;
		roll_input = 0;
	}
	int x_roll_mode = (inputbuttons & 0xE) == 2;

	if (pstate.prev_x_roll_mode == x_roll_mode) {
		pstate.axis_x_accum = TieUserTiming_SlewAxis(pstate.axis_x_accum, x_input, 0);
		pstate.axis_y_accum = TieUserTiming_SlewAxis(pstate.axis_y_accum, y_input, 1);
	} else {
		pstate.axis_x_accum = 0;
		pstate.axis_y_accum = 0;
		if (TieFlightTiming_IsHighRate()) {
			TieUserTimingState* state = TieFlightTimingState_User();
			state->slew_remainder[0] = state->slew_remainder[1] = 0;
			state->slew_sign[0] = state->slew_sign[1] = 0;
		}
	}
	pstate.prev_x_roll_mode = (int16_t)x_roll_mode;

	/* Roll accumulator slews independently of the modifier-button latch
	 * — pulling the second stick should respond regardless of whether
	 * the player is also in held-button X-roll mode. */
	pstate.axis_roll_accum = TieUserTiming_SlewAxis(pstate.axis_roll_accum, roll_input, 2);

	TieUserTimingState* high_rate = TieFlightTiming_IsHighRate() ? TieFlightTimingState_User() : NULL;
	int16_t x_per_tick =
		high_rate ? TieUserTiming_ScaleValue(pstate.axis_x_accum, &high_rate->flight_axis_remainder[0])
				  : (int16_t)math2_ABoverC32(pstate.axis_x_accum, frameticks, 236);
	int16_t y_per_tick =
		high_rate ? TieUserTiming_ScaleValue(pstate.axis_y_accum, &high_rate->flight_axis_remainder[1])
				  : (int16_t)math2_ABoverC32(pstate.axis_y_accum, frameticks, 236);
	int16_t roll_per_tick =
		high_rate ? TieUserTiming_ScaleValue(pstate.axis_roll_accum, &high_rate->flight_axis_remainder[2])
				  : (int16_t)math2_ABoverC32(pstate.axis_roll_accum, frameticks, 236);
	if ((pstate.player_craft->status_flags & 0x20) == 0) {
		x_per_tick = 0;
		y_per_tick = 0;
		roll_per_tick = 0;
	}

	if (x_roll_mode) {
		if (x_per_tick) {
			objects[pstate.object_idx].roll -= (int16_t)(2 * x_per_tick);
			pstate.player->orient_dirty = 1;
			pstate.player->move_dirty = 1;
		}
		/* Throttle nudge via Y axis in roll mode. */
		if ((uint16_t)inputdeltay) {
			uint16_t iy = (uint16_t)inputdeltay;
			if (iy < 0x8000u || iy > 0xE000u) {
				if (iy <= 0x8000u && iy >= 0x2000u) {
					uint16_t decrement = 256;
					if (high_rate)
						decrement = (uint16_t)-TieUserTiming_ScaleCompatibilityIncrement(
							-256, &high_rate->throttle_remainder[0], &high_rate->throttle_sign[0]);
					uint16_t cur = pstate.player_craft->throttle_speed;
					pstate.player_craft->throttle_speed = (uint16_t)(cur - decrement);
					if (cur < decrement)
						pstate.player_craft->throttle_speed = 0;
				}
			} else {
				uint16_t increment = 256;
				if (high_rate)
					increment = (uint16_t)TieUserTiming_ScaleCompatibilityIncrement(
						256, &high_rate->throttle_remainder[0], &high_rate->throttle_sign[0]);
				uint16_t cur = pstate.player_craft->throttle_speed;
				uint16_t nxt = (uint16_t)(cur + increment);
				pstate.player_craft->throttle_speed = nxt;
				if (cur > nxt)
					pstate.player_craft->throttle_speed = 0xFFFF;
			}
		} else if (high_rate) {
			high_rate->throttle_remainder[0] = 0;
			high_rate->throttle_sign[0] = 0;
		}
	} else {
		if (high_rate) {
			high_rate->throttle_remainder[0] = 0;
			high_rate->throttle_sign[0] = 0;
		}
		if (y_per_tick || x_per_tick) {
			user_calcdeltapitch(y_per_tick, (int16_t)-x_per_tick, pstate.object_idx, pstate.player_craft);
			pstate.player->orient_dirty = 1;
			pstate.player->move_dirty = 1;
		}
		/* Auto-bank-into-turn: only when the player isn't supplying
		 * their own analog roll input. Otherwise the auto component
		 * fights the stick. */
		if (x_per_tick && !roll_per_tick)
			objects[pstate.object_idx].roll -= x_per_tick;
	}

	/* Apply analog roll on top of either branch (same 2× gain as the
	 * held-button mode for parity). */
	if (roll_per_tick) {
		objects[pstate.object_idx].roll -= (int16_t)(2 * roll_per_tick);
		pstate.player->orient_dirty = 1;
		pstate.player->move_dirty = 1;
	}
}

/*
 * user_inputforplane -- per-frame in-flight control dispatcher. Large
 * switch on inputkey covering ~150 bindings (flight controls, weapons,
 * shields, view, replay, F-keys, info-rooms). See binary 0x5ADC0.
 *
 * Intentionally monolithic: the binary is a single 12kB function; the
 * only structural abstractions here are the ui_* static helpers above.
 */
// FUNCTION: TIE 0x5CDA0
void user_inputforplane(void) {
	/* Phase 0: hyperspace-abort on 'h'. */
	if (hyperspaceflag < 2u && hyperabortflag && inputkey == KEY_h && hyperspaceflag) {
		msg_messageprintf(MSG_HYPER_ABORTED);
		hyperspaceflag = 0;
		return;
	}
	/* Phase 1: if mid-hyperspace, just advance the warp animation. */
	if (hyperspaceflag) {
		anim_dohyperspace();
		return;
	}

	/* Phase 2: de-jitter joystick then scale inputdeltay by 2. */
	feinput_degitterinput();
	inputdeltay = (int16_t)(inputdeltay * 2);

	/* Mission-type gate: simulator missions skip many keys by
	 * zeroing the key; skipping happens in a preamble for a dense
	 * range near the top of the decompiler's binary search. */
	if (mission.train_craft_type) {
		uint16_t k = (uint16_t)inputkey;
		int skip = 0;
		if (k == KEY_a)
			skip = 1;
		else if (k == KEY_e)
			skip = 1;
		else if (k == KEY_r)
			skip = 1;
		else if (k == KEY_t || k == KEY_u)
			skip = 1;
		else if (k == KEY_y)
			skip = 1;
		else if (k >= KEY_F5 && k <= KEY_F7)
			skip = 1;
		if (skip)
			inputkey = KEY_NONE;
	}

	switch ((uint16_t)inputkey) {
		/* Left/right arrow: roll. */
		case KEY_LEFT_ARROW:
		case KEY_RIGHT_ARROW:
			ui_roll_input(inputkey);
			break;
		/* Throttle full. */
		case KEY_BACKSPACE:
			pstate.player_craft->throttle_speed = 0xFFFF;
			ui_ack_beep();
			argtable[0] = 84;
			msg_messageprintf(MSG_THROTTLE_SET);
			break;
		/* Match target speed. */
		case KEY_ENTER:
			ui_match_speed();
			break;
		/* In-flight options room. */
		case KEY_ESCAPE:
			if (replayviewmode)
				s_info_room_pending = 6;
			else
				TieRuntime_RequestSettingsMenu();
			break;
		/* Confirm pending action. */
		case KEY_SPACE:
			ui_space_confirm();
			break;
		/* Transfer cannon -> shields. */
		case KEY_APOSTROPHE:
			ui_xfer_cannon_to_shields();
			break;
		/* Toggle camera.view_pitch_offset when zoomed (LABEL_213). */
		case KEY_ASTERISK:
			if (!replayviewmode && camera.view_zoom_flag)
				camera.view_pitch_offset = (camera.view_pitch_offset == 0);
			break;
		/* Throttle up step. */
		case KEY_PLUS:
		case KEY_EQUALS: {
			uint16_t cur = pstate.player_craft->throttle_speed;
			uint16_t nxt = (uint16_t)(cur + 0x800);
			pstate.player_craft->throttle_speed = nxt;
			if (cur > nxt)
				pstate.player_craft->throttle_speed = 0xFFFF;
			ui_ack_beep();
			break;
		}
		/* Radar subtarget forward. */
		case KEY_COMMA:
			ui_cycle_radar_target1(+1);
			break;
		/* Throttle down step + ack beep (LABEL_479). */
		case KEY_MINUS: {
			uint16_t cur = pstate.player_craft->throttle_speed;
			pstate.player_craft->throttle_speed = (uint16_t)(cur - 0x800);
			if (cur < 0x800)
				pstate.player_craft->throttle_speed = 0;
			ui_ack_beep();
			break;
		}
		/* Toggle cockpit and recenter the view. */
		case KEY_PERIOD:
			if (!replayviewmode) {
				if (!camera.view_zoom_flag && camera.view_target_obj == pstate.object_idx)
					panelrts_setnewpilotview((camera.pilotview == 19) ? 0u : 19u);
				camera.side_angle = 0;
				camera.up_angle = 0;
			}
			break;
		/* Toggle external camera (LABEL_207). */
		case KEY_SLASH:
			ui_toggle_external_camera();
			break;
		/* Toggle wing-level / high-angle view. */
		case KEY_0:
			ui_toggle_high_angle_view();
			break;
		/* Numpad camera angles 1..4. */
		case KEY_1:
		case KEY_2:
		case KEY_3:
		case KEY_4:
			ui_numpad_camera_key((uint16_t)inputkey);
			break;
		/* Snap side view (center of numpad). */
		case KEY_5:
			ui_snap_side_view();
			break;
		/* Numpad camera angles 6..9. */
		case KEY_6:
		case KEY_7:
		case KEY_8:
		case KEY_9:
			ui_numpad_camera_key((uint16_t)inputkey);
			break;
		/* Transfer shields -> cannon. */
		case KEY_SEMICOLON:
			ui_xfer_shields_to_cannon();
			break;
		/* Cycle subtarget back. */
		case KEY_LESS:
			ui_cycle_radar_target1(-1);
			break;
		/* 'A' (Shift+a): wingmen attack player's target. */
		case KEY_A:
			if (pstate.target_obj_idx != 0xFFFF) {
				if (pstate.target_obj_idx == pstate.radio_target)
					pstate.radio_target = -1;
				user_assigntarget(pstate.target_obj_idx, 0x75u);
			}
			break;
		/* 'B': wingman 'go home'. */
		case KEY_B:
			if (pstate.target_obj_idx < NUM_ACTIVE_CRAFT_SLOTS) {
				craftptr = objects[pstate.target_obj_idx].craft_ptr;
				pai_setupcraftaivars(pstate.target_obj_idx);
				if (craftptr->current_order == 28 && pai_isobjectvalidtarget(pstate.object_idx)) {
					craftptr->pending_radio_command = pstate.object_idx;
					pstate.player_craft->throttle_speed = 0;
					msg_radiomessage(pstate.target_obj_idx, craftptr, 0xC6u, 0);
				}
			}
			break;
		/* 'C': attack my attacker. */
		case KEY_C: {
			uint16_t a = user_findclosestattacker(pstate.object_idx);
			if (a != 0xFFFF)
				user_assigntarget(a, 0x71u);
			break;
		}
		/* 'E': wingman 'wait'. */
		case KEY_E:
			if (!user_checkradio()) {
				msg_messageprintf(MSG_CRAFT_NOT_RESPONDING);
				break;
			}
			if (craftptr->current_order == 44) {
				craftptr->current_order = craftptr->saved_current_order;
				pai_setupcraftaivars(pstate.target_obj_idx);
				pai_initplan();
			}
			craftptr->pending_radio_command = 251;
			msg_radiomessage(pstate.target_obj_idx, craftptr, 0x72u, 0);
			break;
		/* 'G': wingman 'return to base'. */
		case KEY_G:
			if (!user_checkradio()) {
				msg_messageprintf(MSG_CRAFT_NOT_RESPONDING);
				break;
			}
			if (craftptr->current_order == 44) {
				craftptr->current_order = craftptr->saved_current_order;
				pai_setupcraftaivars(pstate.target_obj_idx);
				pai_initplan();
				msg_radiomessage(pstate.target_obj_idx, craftptr, 0x74u, 0);
			}
			break;
		/* 'H': wingman 'attack starship'. */
		case KEY_H:
			if (!user_checkradio()) {
				msg_messageprintf(MSG_CRAFT_NOT_RESPONDING);
				break;
			}
			craftptr = objects[pstate.target_obj_idx].craft_ptr;
			if (craftptr->current_order != 47 && craftptr->current_order != 53) {
				craftptr->special_order_flag = 1;
				craftptr->current_order = (objects[pstate.target_obj_idx].genus == GENUS_STARSHIP) ? 53 : 47;
				pai_setupcraftaivars(pstate.target_obj_idx);
				pai_initplan();
			}
			msg_radiomessage(pstate.target_obj_idx, craftptr, 0x70u, 0);
			break;
		/* 'I': assign target to group. */
		case KEY_I:
			if (pstate.target_obj_idx != 0xFFFF) {
				pstate.radio_target = pstate.target_obj_idx;
				user_assigntarget(0xFFu, 0x76u);
			}
			break;
		/* 'R': order report. */
		case KEY_R:
			if (pstate.target_obj_idx < NUM_ACTIVE_CRAFT_SLOTS && objects[pstate.target_obj_idx].side == 1) {
				craftptr = objects[pstate.target_obj_idx].craft_ptr;
				msg_reportmessage(pstate.target_obj_idx, craftptr, convertmessage[craftptr->current_order]);
			}
			break;
		/* 'S': reinforce request. */
		case KEY_S:
			ui_reinforce_request();
			break;
		/* 'W': wait in place. */
		case KEY_W:
			if (!user_checkradio()) {
				msg_messageprintf(MSG_CRAFT_NOT_RESPONDING);
				break;
			}
			if (craftptr->current_order != 44 && craftptr->current_order != 51 &&
				craftptr->current_order != 52) {
				craftptr->saved_current_order = craftptr->current_order;
				craftptr->current_order = (objects[pstate.target_obj_idx].genus == GENUS_STARSHIP) ? 64 : 44;
				pai_setupcraftaivars(pstate.target_obj_idx);
				pai_initplan();
				msg_radiomessage(pstate.target_obj_idx, craftptr, 0x73u, 0);
			}
			break;
		/* 'Z': wingmen info room. */
		case KEY_Z:
			s_info_room_pending = 4;
			break;
		/* '[': throttle 1/3. */
		case KEY_LBRACKET:
			pstate.player_craft->throttle_speed = 21845;
			ui_ack_beep();
			argtable[0] = 82;
			msg_messageprintf(MSG_THROTTLE_SET);
			break;
		/* '\\': throttle off. */
		case KEY_BACKSLASH:
			pstate.player_craft->throttle_speed = 0;
			ui_ack_beep();
			argtable[0] = 81;
			msg_messageprintf(MSG_THROTTLE_SET);
			break;
		/* ']': throttle 2/3. */
		case KEY_RBRACKET:
			pstate.player_craft->throttle_speed = (uint16_t)(int16_t)-21846;
			ui_ack_beep();
			argtable[0] = 83;
			msg_messageprintf(MSG_THROTTLE_SET);
			break;
		/* 'a': target closest attacker of current target. */
		case KEY_a: {
			uint16_t a = user_findclosestattacker(pstate.target_obj_idx);
			user_setnewtarget(a);
			break;
		}
		/* 'b': beam on/off. */
		case KEY_b:
			ui_toggle_beam();
			break;
		/* 'd': damage info room. */
		case KEY_d:
			s_info_room_pending = 3;
			break;
		/* 'e': target my closest attacker. */
		case KEY_e: {
			uint16_t a = user_findclosestattacker(pstate.object_idx);
			user_setnewtarget(a);
			break;
		}
		/* 'g': goals info room. */
		case KEY_g:
			s_info_room_pending = 0;
			break;
		/* 'h': hyperspace. */
		case KEY_h:
			ui_hyperspace();
			break;
		/* 'i': radar toggle. */
		case KEY_i:
			ui_toggle_radar();
			break;
		/* 'k': help info room. */
		case KEY_k:
			s_info_room_pending = 5;
			break;
		/* 'l': messages info room. */
		case KEY_l:
			s_info_room_pending = 2;
			break;
		/* 'm': map info room. */
		case KEY_m:
			s_info_room_pending = 1;
			break;
		/* 'n': overdrive toggle. */
		case KEY_n:
			ui_overdrive_toggle();
			break;
		/* 'o': drop current target lock and reset external view back to
		 * the player's cockpit. */
		case KEY_o:
			pstate.target_obj_idx = 0xFFFF;
			if (!replayviewmode && camera.view_heading_offset) {
				camera.view_heading_offset = 0;
				camera.view_zoom_flag = 0;
				camera.view_zoom = 0;
				camera.view_pitch_offset = 0;
				camera.view_target_obj = pstate.object_idx;
				panelrts_setnewpilotview(0);
				camera.side_angle = 0;
				camera.up_angle = 0;
				lasttargetnum = -2;
			}
			break;
		/* 'q': end-mission prompt. */
		case KEY_q:
			if (!pstate.space_confirm_action) {
				msg_messageprintf(MSG_END_MISSION_PROMPT);
				pstate.space_confirm_action = 2;
				timers[TIMER_SPACE_CONFIRM] = 1888;
			}
			break;
		/* 'r': find nearest enemy and target it. Also synthesized by
		 * user_userinterface on button-chord-4 release. */
		case KEY_r:
			ui_target_nearest_fighter_or_mine();
			break;
		/* 's': shield mode cycle. Also synthesized on button-chord-7
		 * release. */
		case KEY_s:
			ui_cycle_shield_mode();
			break;
		/* 't': target cycle forward. */
		case KEY_t:
			ui_target_cycle(+1);
			break;
		/* 'u': target newest craft in the area (QRC manual). */
		case KEY_u:
			ui_target_newest_craft();
			break;
		/* 'w': weapon group cycle. */
		case KEY_w:
			ui_cycle_weapon_group();
			break;
		/* 'x': weapon firing-mode cycle. */
		case KEY_x:
			ui_cycle_weapon_firing_mode();
			break;
		/* 'y': target cycle backward. */
		case KEY_y:
			ui_target_cycle(-1);
			break;
		/* 'z': target-viewer toggle. */
		case KEY_z:
			ui_target_viewer_toggle();
			break;
		/* Alt+E: eject / surrender. */
		case KEY_ALT_E:
			ui_eject_or_surrender();
			break;
		/* Alt+O: screenshot. */
		case KEY_ALT_O:
			if (TieClassicDisplay_UsesDx5())
				FrontendDisplay_CaptureScreenshot();
			else
				rtsvga2_takeScreenshot();
			break;
		/* Alt+T: time acceleration toggle. */
		case KEY_ALT_T:
			acceleratedtimesetting *= 2;
			if (acceleratedtimesetting == 8) {
				acceleratedtimesetting = 1;
				msg_messageprintf(MSG_TIME_NORMAL);
			} else {
				argtable[0] = acceleratedtimesetting;
				acceleratedtimectr = acceleratedtimesetting;
				msg_messageprintf(MSG_TIME_ACCEL);
			}
			break;
		/* Alt+S: sound volume toggle. */
		case KEY_ALT_S:
			if (soundvolflag) {
				imuse_set_sfx_vol(im, 0);
				imuse_set_voice_vol(im, 0);
				soundvolflag = 0;
			} else {
				/* Volume scaling: 0..16 -> iMUSE group volume.
				 * Binary used unaligned-dword-load anchors `*_vol_loadbase`
				 * (base - 3) and `>> 24` to extract the inflight_*_vol byte;
				 * the C port reads the named field directly. */
				int16_t sfx_v = inflight_sound_vol ? (int16_t)(8 * inflight_sound_vol - 1) : 0;
				int16_t voc_v = inflight_speech_vol ? (int16_t)(8 * inflight_speech_vol - 1) : 0;
				imuse_set_sfx_vol(im, sfx_v);
				imuse_set_voice_vol(im, voc_v);
				soundvolflag = 1;
			}
			break;
		/* Alt+D: detail level toggle. */
		case KEY_ALT_D: {
			uint8_t dl = (uint8_t)(detaillevel + 1);
			if (dl >= 4)
				dl = 0;
			detaillevel = dl;
			user_setdetaillevel(dl);
			argtable[0] = (uint16_t)(dl + 38);
			msg_messageprintf(MSG_GFX_DETAIL);
			ui_ack_beep();
			break;
		}
		/* Alt+M: music volume toggle. */
		case KEY_ALT_M:
			if (musicvolflag) {
				imuse_set_music_vol(im, 0);
				if (TieMusicPolicy_UsesTie98())
					gamesnd_Set_CD_Volume(0);
				musicvolflag = 0;
			} else {
				int16_t v = inflight_music_vol ? (int16_t)(8 * inflight_music_vol - 1) : 0;
				imuse_set_music_vol(im, v);
				if (TieMusicPolicy_UsesTie98())
					gamesnd_Set_CD_Volume(inflight_music_vol);
				musicvolflag = 1;
			}
			break;
		/* F1: return to the forward cockpit. */
		case KEY_F1:
			if (!replayviewmode) {
				camera.view_heading_offset = 0;
				camera.view_zoom_flag = 0;
				camera.view_target_obj = pstate.object_idx;
				camera.view_pitch_offset = 0;
				panelrts_setnewpilotview(0);
				camera.side_angle = 0;
				camera.up_angle = 0;
			}
			break;
		/* F2: select or cycle warhead view. */
		case KEY_F2:
			ui_cycle_warhead_view();
			break;
		/* F3: toggle external camera (LABEL_207). */
		case KEY_F3:
			ui_toggle_external_camera();
			break;
		/* F4: toggle external-camera positioning controls (LABEL_213). */
		case KEY_F4:
			if (!replayviewmode && camera.view_zoom_flag)
				camera.view_pitch_offset = (camera.view_pitch_offset == 0);
			break;
		/* F8: beam-rate cycle. */
		case KEY_F8:
			ui_cycle_beam_rate();
			break;
		/* F9: cannon-rate cycle. */
		case KEY_F9:
			ui_cycle_cannon_rate();
			break;
		/* F10: shield-rate cycle. */
		case KEY_F10:
			ui_cycle_shield_rate();
			break;
		/* Shift+F9: xfer shields -> cannon (LABEL_303). Also synthesized
		 * from joystick chord-11 release in user_userinterface. */
		case KEY_SHIFT_F9:
			ui_xfer_shields_to_cannon();
			break;
		/* Shift+F5..F7: quicksave target recall slots. */
		case KEY_SHIFT_F5:
		case KEY_SHIFT_F6:
		case KEY_SHIFT_F7:
			if (pstate.target_obj_idx != 0xFFFF)
				pstate.target_presets[(uint16_t)inputkey - KEY_SHIFT_F5] = pstate.target_obj_idx;
			ui_ack_beep();
			break;
		/* Shift+F10: xfer cannon -> shields (LABEL_354). */
		case KEY_SHIFT_F10:
			ui_xfer_cannon_to_shields();
			break;
		/* Alt+1: auto-target. */
		case KEY_ALT_1: {
			uint16_t a = user_picktarget();
			user_setnewtarget(a);
			break;
		}

		default:
			/* F5..F7: quick-recall slot keys. Binary 0x5CBBC. */
			if ((uint16_t)inputkey >= KEY_F5 && (uint16_t)inputkey <= KEY_F7) {
				uint16_t tgt = pstate.target_presets[(uint16_t)inputkey - KEY_F5];
				if (tgt != 0xFFFFu) {
					uint32_t idx = (tgt >= 0x3800u) ? (tgt - 14336) : tgt;
					if (objects[idx].ship_idx)
						user_setnewtarget(tgt);
				}
				break;
			}
			/* No binding for this key — no-op. */
			break;
	}

	ui_apply_throttle_axis();
	ui_apply_view_or_flight_input();
}
