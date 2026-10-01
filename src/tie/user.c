#include "tie/user.h"
#include "tie/edition.h"
#include "tie/help.h"
#ifdef TIE_MODERN
#include "tie_runtime/runtime/help_task.h"
#include "tie_runtime/runtime/inflight_info_task.h"
#include "tie_runtime/runtime/wingman_task.h"
#endif
#ifdef TIE_MODERN
#include "tie_runtime/storage/string_table.h"
#endif
#include "tie_runtime/runtime/damage_task.h"
#include "tie_runtime/runtime/goals_task.h"
#include "tie_runtime/runtime/maproom_task.h"
#include "tie_runtime/runtime/msgroom_task.h"
/*
 * USER.C -- cockpit input / view / replay camera dispatcher.
 */

#include "tie_runtime/audio/config.h"
#include "tie_runtime/audio/imuse_session.h"
#include "tie_runtime/diagnostics/diagnostics.h"
#include "tie_runtime/diagnostics/flight_trace.h"
#include "tie_runtime/display/classic_display.h"
#include "tie_runtime/display/classic_framebuffer.h"
#include "tie_runtime/flight_assets/model_types.h"
#include "tie_runtime/hooks/orientation.h"
#include "tie_runtime/input/input.h"
#include "tie_runtime/runtime/exports.h"
#include "tie_runtime/runtime/flight_requests.h"
#include "tie_runtime/runtime/inflight_state.h"
#include "tie_runtime/runtime/profile.h"
#include "tie_runtime/runtime/replay_format.h"
#include "tie_runtime/runtime/runtime.h"
#include "tie_runtime/storage/storage.h"
#include "tie_runtime/timing/flight_timing_state.h"
#include "tie_runtime/timing/user_timing.h"

#include "tie/anim.h"
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
#include "tie_runtime/audio/music_policy.h"
#include "tie_runtime/runtime/profile.h"
#include "tie_runtime/timing/chase_camera.h"
#include "tie_runtime/timing/replay_recording.h"
#include "tie_runtime/timing/replay_timing.h"
#include "util/binio.h"

#include "tie/damage.h"
#include "tie/goals.h"
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

#ifdef TIE_MODERN
#include <landru/task.h>
#endif
#include <imuse/hilevel.h>
#include <imuse/lolevel.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* ================================================================== *
 *                        MODULE DATA TABLES                           *
 * ================================================================== */

/*
 * convertmessage -- 69-byte AI-order -> display-message-index table.
 * Used by the 'radio order report' key (R) to show the target's current order by name.
 */
// GLOBAL: TIE95 0xCDD2E
// GLOBAL: TIE98 0x4F3C98
uint8_t convertmessage[69] = { 0x7a, 0x7a, 0x7a, 0x92, 0x92, 0x92, 0x92, 0x7d, 0x7e, 0x7d, 0x7f, 0x80,
							   0x80, 0x81, 0x82, 0x7f, 0x80, 0x81, 0x80, 0x83, 0x84, 0x7f, 0x80, 0x81,
							   0x84, 0x7f, 0x80, 0x81, 0x85, 0x85, 0x85, 0x86, 0x85, 0x87, 0x85, 0x88,
							   0x89, 0x98, 0x98, 0x89, 0x8a, 0x89, 0x8b, 0x8c, 0x96, 0x8d, 0x8d, 0x8d,
							   0x8d, 0x8e, 0x8f, 0x90, 0x91, 0x90, 0x8d, 0x7a, 0x92, 0x92, 0x93, 0x94,
							   0x7c, 0x7c, 0x92, 0x92, 0x96, 0x95, 0x97, 0x7a, 0x85 };

/*
 * viewtranslate[10] -- scan-key -> pilotview slot for the numpad view keys.
 * Indexed 0..9. Binary bytes: {0,3,4,5,2,16,6,1,0,7}.
 */
// GLOBAL: TIE95 0xCDD10
// GLOBAL: TIE98 0x4F3C70
uint8_t viewtranslate[10] = { 0, 3, 4, 5, 2, 16, 6, 1, 0, 7 };

/*
 * looktranslate[10] -- camera.up_angle per numpad view key, indexed like
 * viewtranslate.
 */
// GLOBAL: TIE95 0xCDD1A
// GLOBAL: TIE98 0x4F3C80
int16_t looktranslate[10] = { 0, -24576, -32768, 24576, -16384, 0, 16384, -8192, 0, 8192 };

/*
 * LOD preset tables. Indexed 0..3 by user_setdetaillevel. Level 0 is
 * the shipped default (moderate detail); higher levels increase
 * fidelity at frame-rate cost.
 */
// GLOBAL: TIE95 0xCDD96
// GLOBAL: TIE98 0x4F3CE0
const uint16_t starshipexplodtl[4] = { 0x1000, 0x2000, 0x4000, 0x7FFF };
// GLOBAL: TIE95 0xCDD9E
// GLOBAL: TIE98 0x4F3CE8
const uint16_t starshipdtl[4] = { 1, 2, 3, 4 };
// GLOBAL: TIE95 0xCDDA6
// GLOBAL: TIE98 0x4F3CF0
const uint16_t stardtl[4] = { 2, 1, 1, 1 };
// GLOBAL: TIE95 0xCDDAE
// GLOBAL: TIE98 0x4F3CF8
const uint16_t backdtl[4] = { 0, 0, 0, 1 };
// GLOBAL: TIE95 0xCDDB6
// GLOBAL: TIE98 0x4F3D00
const uint16_t debrisdtl[4] = { 0, 0, 1, 1 };
// GLOBAL: TIE95 0xCDDBE
// GLOBAL: TIE98 0x4F3D08
const int16_t polydtl[4] = { 1, 1, 0, -1 };
// GLOBAL: TIE95 0xCDDC6
// GLOBAL: TIE98 0x4F3D10
const uint16_t numpolydtl[4] = { 8, 12, 16, 16 };
// GLOBAL: TIE95 0xCDDCE
// GLOBAL: TIE98 0x4F3D18
const uint16_t markdtl[4] = { 0, 0, 1, 1 };
// GLOBAL: TIE95 0xCDDD6
// GLOBAL: TIE98 0x4F3D20
const uint16_t hyperdtl[4] = { 16, 32, 44, 50 };
// GLOBAL: TIE95 0xCDDDE
const uint8_t gourauddtl[6] = { 0, 0, 0x40, 0x40, 0, 0 };

/* Per-frame scratch + volume-toggle latches (user.c per watdbg). */
// GLOBAL: TIE95 0xEC208
// GLOBAL: TIE98 0x58E018
uint16_t screendist;
// GLOBAL: TIE95 0xEC20A
// GLOBAL: TIE98 0x58E01A
uint8_t soundvolflag;
// GLOBAL: TIE95 0xEC20B
// GLOBAL: TIE98 0x58E01B
uint8_t musicvolflag;

/* ================================================================== *
 *                          SMALL HELPERS                              *
 * ================================================================== */

/*
 * user_framerateadjust -- scale a per-frame increment by elapsed PIT
 * ticks. 236 = one nominal-rate frame, so at target framerate this is
 * the identity. See binary 0x5DCB8.
 */
// FUNCTION: TIE95 0x5FC50
int16_t user_framerateadjust(int16_t per_236) { return (int16_t)math2_ABoverC32(per_236, frameticks, 236); }

/*
 * user_increasepower / user_decreasepower -- saturating throttle adjust.
 * Binary 0x5DCD4 / 0x5DD04.
 */
// FUNCTION: TIE95 0x5FC6C
void user_increasepower(uint16_t delta) {
	uint16_t cur = pstate.player_craft->throttle_speed;
	pstate.player_craft->throttle_speed += delta;
	if (pstate.player_craft->throttle_speed < cur)
		pstate.player_craft->throttle_speed = 0xFFFF;
}

// FUNCTION: TIE95 0x5FC9C
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
// FUNCTION: TIE95 0x5FCCC
void user_adjustshields(uint16_t dst_idx, uint16_t src_idx) {
	int cap;
	int16_t headroom;

	if ((&pstate.player_craft->forward_shield)[src_idx] <= 0)
		return;

	cap = (uint16_t)spec_data[pstate.player_spec_num].shield_points * 2;
	if (!mission.difficulty)
		cap *= 2;

	headroom = (int16_t)(cap - (&pstate.player_craft->forward_shield)[dst_idx]);
	if (headroom <= 0)
		return;

	if (headroom < (&pstate.player_craft->forward_shield)[src_idx]) {
		(&pstate.player_craft->forward_shield)[dst_idx] += headroom;
		(&pstate.player_craft->forward_shield)[src_idx] -= headroom;
	} else {
		(&pstate.player_craft->forward_shield)[dst_idx] += (&pstate.player_craft->forward_shield)[src_idx];
		(&pstate.player_craft->forward_shield)[src_idx] = 0;
	}
}

/*
 * user_resetview -- recenter the view after threat-view zoom or after
 * a target change. When zoomed (camera.view_zoom_flag != 0) prime the 60-slot
 * cam-chase angle history ring to the current orientation; otherwise
 * leave camera-control mode and either restore the saved angles (if still
 * tracking the player) or default back to pilotview 18. Binary 0x5DDE4.
 */
// FUNCTION: TIE95 0x5FD7C
void user_resetview(void) {
	if (camera.view_zoom_flag) {
		uint16_t view_idx = camera.view_target_tracking ? 20u : 18u;
		int i;

		panelrts_setnewpilotview(view_idx);
		for (i = 0; i < 60; ++i) {
			camera.cam_chase_roll_hist[i] = camera.roll;
			camera.cam_chase_pitch_hist[i] = (int16_t)camera.cam_pitch;
			camera.cam_chase_heading_hist[i] = (int16_t)camera.cam_heading;
		}
		TieChaseCamera_Reset();
	} else {
		uint16_t view_idx_out;

		camera.view_camera_control = 0;

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
// FUNCTION: TIE95 0x60FD0
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
// FUNCTION: TIE95 0x61E40
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
// FUNCTION: TIE95 0x61E9C
int16_t user_validcomponent(uint16_t comp_idx) {
	const ShipModelMesh* m = &componentblockptr[comp_idx];
	int mtype = m->mesh_type;
	int16_t has_pos;
	const ShipModelMesh* probe;
	int n;
	int i;

	if (mtype == 18 || mtype == 19)
		return 0;

	has_pos = m->has_position;
	if (!has_pos)
		return 1;
	if (has_pos == 1 && mtype != 1 /* MainHull */)
		return 1;

	probe = componentblockptr;
	n = objectblockptr->num_meshes;
	for (i = 0; i < n; ++i, ++probe) {
		if (probe->has_position == has_pos && probe->mesh_type == mtype)
			return (i == comp_idx);
	}
	return 0;
}

// FUNCTION: TIE98 0x498D00
// USER_validcomponent
int16_t user_validcomponent_tie98(uint16_t model_type, uint16_t mesh_index) {
	const int mesh_type = modelmesh_gettype(model_type, mesh_index);
	int target_id;
	int count;
	int index;

	if (mesh_type == TIE_MESH_MISC_HULL || mesh_type == TIE_MESH_ANTENNA)
		return 0;
	target_id = modelmesh_gettargetid(model_type, mesh_index);
	if (target_id == 0)
		return 1;
	if (target_id == 1 && mesh_type != TIE_MESH_MAIN_HULL && mesh_type != TIE_MESH_FUSELAGE)
		return 1;
	count = modelmesh_getcount(model_type);
	for (index = 0; index < count; ++index) {
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
// FUNCTION: TIE95 0x5FE4C
uint16_t user_picktarget(void) {
	uint32_t best_in_cross_rough = 0xFFFFFFFFu;
	uint16_t best_offscreen_dist = 0xFFFF;
	uint16_t best_in_cross_idx = 0xFFFF;
	uint16_t best_offscreen_idx = 0xFFFF;

	uint16_t i;
	uint16_t static_obj_idx;
	uint16_t j;

	for (i = 0; i < NUM_OBJECTS; ++i) {
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

	static_obj_idx = 14336;
	for (j = 0; j < 0x40u; ++j, ++static_obj_idx) {
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
// FUNCTION: TIE95 0x5FF78
uint16_t user_picknexttarget(uint16_t start, int32_t step) {
	/* Total search budget = NUM_OBJECTS + NUM_STATIC_OBJECTS. Retail = 184
	 * (120 + 64); demo was 180 (116 + 64). */
	int16_t iter = NUM_OBJECTS + NUM_STATIC_OBJECTS;

	while (--iter != (int16_t)0xFFFF) {
		uint16_t obj_species;
		uint8_t flight;

		start = (uint16_t)(start + step);
		if (start >= 0x8000)
			start = 14399;
		/* Underflow past first static (0x3800) -> last active flight
		 * slot (NUM_OBJECTS - 1 = 119 retail / 115 demo). */
		else if (start == 0x37FF)
			start = NUM_OBJECTS - 1;
		/* Overflow past last active flight slot -> first static. */
		else if (start == NUM_OBJECTS)
			start = 14336;
		/* Overflow past last static -> first active flight slot. */
		else if (start == 0x3840)
			start = 0;
		if (start == pstate.object_idx)
			continue;

		if (start < 0x3800)
			obj_species = objects[start].ship_idx;
		else
			obj_species = staticobjects[start - 14336].species;
		if (!obj_species)
			continue;
		if ((species_table[obj_species].side & 1) == 0)
			continue;

		if (start >= NUM_ACTIVE_CRAFT_SLOTS)
			return start;
		if (objects[start].genus == GENUS_EXPLOSION)
			continue;
		if (objects[start].category)
			return start;
		craftptr = objects[start].craft_ptr;
		flight = craftptr->flight_flag;
		/* flight_flag 3 or 4 (hyperspace) -> continue loop, but
		 * craftptr now carries this craft's ptr for the next exit. */
		if (flight != 3 && flight != 4)
			return start;
	}
	return iter;
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
// FUNCTION: TIE95 0x60080
int16_t user_targetincross(uint16_t obj_idx, int32_t strict) {
	FlightObject* pl = pstate.player;
	int32_t delta_x, delta_y, delta_z;
	int8_t dist_shift;
	int32_t eye_z;
	int32_t half_wide;
	int32_t eye_side_dot;
	int32_t screen_x_rel;
	int32_t screen_dx_abs;
	int32_t eye_up_dot;
	int32_t screen_y_rel;
	int32_t screen_dy_abs;
	int32_t bound_hwidth;
	int32_t reticle;

	screendist = 0xFFFF;
	pai_roughdistancebetween(obj_idx, pstate.object_idx);

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
		fview_calcrotatemove(pl->pitch, pl->heading, pl);
		fview_calcrotateorient(pl->roll, 0, pl);
	}

	eye_z = ((pl->fwd_z * (int16_t)delta_z) >> 15) + ((pl->fwd_y * (int16_t)delta_y) >> 15) +
			((pl->fwd_x * (int16_t)delta_x) >> 15);
	if (eye_z <= 0 || eye_z > 0x20000)
		return 0;
	if (eye_z < 0x2000)
		++dist_shift;

	half_wide = pixelswide / 2;
	eye_side_dot = ((pl->side_z * (int16_t)delta_z) >> 15) + ((pl->side_y * (int16_t)delta_y) >> 15) +
				   ((pl->side_x * (int16_t)delta_x) >> 15);
	screen_x_rel = transfm2_getscreenx(eye_side_dot, eye_z) - half_wide;

	screen_dx_abs = (int32_t)(int16_t)screen_x_rel;
	if (screen_dx_abs & 0x8000)
		screen_dx_abs = -(int32_t)(int16_t)screen_x_rel;
	if ((int16_t)screen_dx_abs > (int32_t)pixelswide / 2)
		return 0;

	eye_up_dot = ((pl->up_z * (int16_t)delta_z) >> 15) + ((pl->up_y * (int16_t)delta_y) >> 15) +
				 ((pl->up_x * (int16_t)delta_x) >> 15);
	screen_y_rel = transfm2_getscreeny(eye_up_dot, eye_z) - (pixelsdeep / 2) - transfm2_screenyoffset;

	screen_dy_abs = (int32_t)(int16_t)screen_y_rel;
	if (screen_dy_abs & 0x8000)
		screen_dy_abs = -(int32_t)(int16_t)screen_y_rel;
	/* Retail fix: compare |screen_y| against the vertical half-extent, and
	 * apply aspect-ratio correction (59578/65536 ≈ 0.909) to make the
	 * reticle circular on 320x200 VGA (non-square pixels). */
	screen_dy_abs = (screen_dy_abs * 59578) >> 16;
	if ((int16_t)screen_dy_abs > (int32_t)pixelsdeep / 2)
		return 0;

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

	reticle = ((bound_hwidth >> dist_shift) << 8) / eye_z;
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
// FUNCTION: TIE95 0x603EC
int16_t user_targetonscreen(uint16_t obj_or_kind) {
	FlightObject* pl = pstate.player;
	uint16_t obj_idx_loc;
	int32_t bound_hwidth_pre;
	int32_t delta_x, delta_y, delta_z;
	int8_t dist_shift;
	int32_t eye_z;
	int32_t eye_side;
	int16_t screen_x;
	int32_t eye_up;
	int16_t screen_y;
	int32_t bound_hwidth;
	int32_t reticle;
	int32_t threshold;
	int32_t box_w;
	int32_t box_h;

	if (obj_or_kind == 0xFFFF || replayviewmode)
		return 0;
	if (!pstate.radar_enable)
		return 0;
	if (camera.pilotview && camera.pilotview != 19)
		return 0;

	obj_idx_loc = obj_or_kind;
	pai_distancebetween(obj_idx_loc, pstate.object_idx);

	/* HD snapshot publish for target_box_engine_ok + target_bound_hwidth
	 * is now driven by TieHudSnapshot_Capture (runs unconditionally per
	 * host frame, including when paused — see panel_publish_target_box_state).
	 * user_targetonscreen still runs the classic 4:3 projection + xtrans
	 * emit below, but skipping it during pause no longer makes the HD
	 * target box vanish. bound_hwidth_pre is still computed locally
	 * for the apparent-size threshold check at line ~620. */

	if (obj_idx_loc >= NUM_ACTIVE_CRAFT_SLOTS) {
		int species = (obj_idx_loc >= 0x3800u) ? staticobjects[obj_idx_loc - 14336].species
											   : objects[obj_idx_loc].ship_idx;
		bound_hwidth_pre =
			TIE_FLIGHT_EDITION(species_table[species].bound_hwidth, modelbounds_getmaxextent(species));
	} else {
		int sp = objects[obj_idx_loc].craft_ptr->species_idx;
		bound_hwidth_pre =
			(((int16_t)(spec_data[sp].bound_height + spec_data[sp].bound_depth + spec_data[sp].bound_width) /
			  3)
			 << spec_data[sp].model_scale_shift);
	}

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
		fview_calcrotatemove(pl->pitch, pl->heading, pl);
		fview_calcrotateorient(pl->roll, 0, pl);
	}

	eye_z = ((pl->fwd_x * (int16_t)delta_x) >> 15) + ((pl->fwd_y * (int16_t)delta_y) >> 15) +
			((pl->fwd_z * (int16_t)delta_z) >> 15);
	if (eye_z <= 0)
		return 0;

	eye_side = ((pl->side_z * (int16_t)delta_z) >> 15) + ((pl->side_y * (int16_t)delta_y) >> 15) +
			   ((pl->side_x * (int16_t)delta_x) >> 15);
	screen_x = (int16_t)transfm2_getscreenx(eye_side, eye_z);
	if ((int32_t)screen_x < 0 || screen_x > (int32_t)pixelswide)
		return 0;

	eye_up = -(((pl->up_y * (int16_t)delta_y) >> 15) + ((pl->up_x * (int16_t)delta_x) >> 15) +
			   ((pl->up_z * (int16_t)delta_z) >> 15));
	/* transfm2_getscreeny already adds halfpixelsdeep + screenyoffset, so
	 * the result is an absolute screen-space Y. Retail uses it directly;
	 * the demo subtracted from pixelsdeep because its getscreencoordy
	 * returned a relative offset instead. Don't flip here. */
	screen_y = (int16_t)transfm2_getscreeny(eye_up, eye_z);
	if (screen_y < 0 || screen_y > (int16_t)pixelsdeep)
		return 0;

	bound_hwidth = bound_hwidth_pre;
	reticle = (perspFactor * (bound_hwidth >> dist_shift)) / eye_z;
	threshold = (flightResolution == TIE_FLIGHT_RES_VGA) ? 5 : 10;
	if ((int16_t)reticle > threshold)
		return 0;

	box_w = screenXRes / 0x30;
	box_h = screenYRes / 0x30;
	panel_drawboxinxtrans((int16_t)(screen_x - box_w / 2), (int16_t)(screen_y - box_h / 2), (uint16_t)box_w,
						  (uint16_t)box_h, 0xCE);
	return 0;
}

// FUNCTION: TIE98 0x4974A0
static int32_t user_gettargetdisplayextent_tie98(uint16_t object_reference) {
	FlightObject* object;
	uint16_t spec_index;

	if (object_reference >= OBJ_REF_STATIC_BASE) {
		const uint8_t model_type = staticobjects[object_reference - OBJ_REF_STATIC_BASE].species;
		return species_table[model_type].bound_hwidth;
	}
	object = &objects[object_reference];
	spec_index = object->craft_ptr->species_idx;
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
	int32_t world_x;
	int32_t world_y;
	int32_t world_z;
	int32_t relative_x;
	int32_t relative_y;
	int32_t relative_z;

	create_getworldposition(object_reference, 0);
	world_x = worldlocx;
	world_y = worldlocy;
	world_z = worldlocz;
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
	relative_x = world_x - camera.x;
	relative_y = world_y - camera.y;
	relative_z = world_z - camera.z;
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
	int32_t screen_x;
	int32_t screen_y;
	int32_t depth;
	int32_t extent;
	int minimum;
	int size;
	int maximum;
	int outer_size;

	if (object_reference == 0xffff || replayviewmode || !pstate.radar_enable)
		return;

	user_projectobjectmeshcenter_tie98(object_reference, mesh_index, &screen_x, &screen_y, &depth);
	if (depth > 0) {
		int32_t extent;
		int minimum;
		int size;
		int maximum;

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
		minimum = flightResolution == TIE_FLIGHT_RES_VGA ? 4 : 8;
		size = (int)((uint32_t)perspFactor * (uint32_t)extent / (uint32_t)depth);
		if (size < minimum)
			size = minimum;
		maximum = screenXRes / 2 + screenXRes / 4;
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
	extent = user_gettargetdisplayextent_tie98(bluetarget);
	minimum = flightResolution == TIE_FLIGHT_RES_VGA ? 4 : 8;
	size = (int)((uint32_t)perspFactor * (uint32_t)extent / (uint32_t)depth) -
		   (extent - minimum) / (blinkticks + 1);
	if (size < minimum)
		size = minimum;
	maximum = screenXRes / 2 + screenXRes / 4;
	if (size > maximum)
		size = maximum;
	outer_size = size + 2;
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
// FUNCTION: TIE95 0x60790
void user_setnewtarget(uint16_t new_obj) {
	uint16_t i;
	uint16_t working_subsystems;
	CraftData* cp_t;

	if (pstate.player_craft->status_flags & 4) {
		if (new_obj == 0xFFFF || new_obj == pstate.target_obj_idx)
			return;

		fsfx_triggersfx(0x22u, 0xFFFF);
		pstate.target_obj_idx = new_obj;
		pstate.radar_target1 = 0;

		if (new_obj < NUM_ACTIVE_CRAFT_SLOTS) {
			if (!TIE_FLIGHT_TIE98)
				draw_Lockshipfileptrs(objects[new_obj].ship_idx);
			for (i = 0; i < TIE_FLIGHT_EDITION(objectblockptr->num_meshes,
											   modelmesh_getcount(objects[new_obj].ship_idx));
				 ++i) {
				int mt = TIE_FLIGHT_EDITION(componentblockptr[i].mesh_type,
											modelmesh_gettype(objects[new_obj].ship_idx, i));
				if (mt == 1 || mt == 3) {
					pstate.radar_target1 = (int16_t)i;
					break;
				}
			}
		}
		if (!replayviewmode && camera.view_target_tracking)
			camera.view_target_obj = pstate.target_obj_idx;
		pstate.radar_subtarget_state = 0;
		working_subsystems = pstate.player_craft->working_subsystems;
		pstate.player_craft->missile_count_total = 0;

		if ((working_subsystems & 1) == 0 || camera.pilotview != 19)
			return;

		if (new_obj < NUM_ACTIVE_CRAFT_SLOTS) {
			cp_t = objects[new_obj].craft_ptr;
#ifdef TIE_MODERN
			msg_addmessageptr(0, (char*)spec_name_ptrs[cp_t->species_idx]);
#else
			msg_addmessageptr(0, (char*)spec_data[cp_t->species_idx].name_ptr);
#endif
			if ((int8_t)fg_array[objects[new_obj].fg_idx].count > 1) {
				argtable[1] = (uint16_t)(cp_t->craft_idx_in_fg + 1);
				msg_addmessageptr(2, fg_array[objects[new_obj].fg_idx].name);
				argtable[3] = 103;
				msg_messageprintf(MSG_REPORT_FROM_FG);
			} else {
				msg_addmessageptr(1, fg_array[objects[new_obj].fg_idx].name);
				argtable[2] = 103;
				msg_messageprintf(MSG_REPORT_FROM);
			}
		} else if (new_obj >= 0x3800) {
			new_obj -= 0x3800;
			i = staticobjects[new_obj].species;
			if (i >= 0x46 && i <= 0x54)
				msg_addmessageptr(0, ((char**)buoystr)[i - 70]);
			msg_addmessageptr(1, fg_array[staticobjects[new_obj].fg_idx].name);
			argtable[2] = 103;
			msg_messageprintf(MSG_REPORT_FROM);
		}
	} else {
		argtable[0] = 33;
		argtable[1] = 25;
		msg_messageprintf(MSG_SYSTEM_STATUS);
	}
}

/* Quaternion pitch decomposition remains controllable at the world ±Z poles.
 *
 * TIE basis (columns S, U, f in world frame; f is the negated forward vector)
 * at (β=pitch, the forward vector's polar angle from +Z, α=heading azimuth,
 * γ=roll around forward):
 *     S_0 = ( cos α, -sin α, 0)
 *     U_0 = (-cos β sin α, -cos β cos α, sin β)
 *     f   = (-sin β sin α, -sin β cos α, -cos β)
 *   then S = R_f(γ) · S_0,  U = R_f(γ) · U_0.
 */

/*
 * user_calcdeltapitch -- rotate the craft basis by dpitch about its side
 * axis and dyaw about its up axis, then decompose back to Euler, writing
 * heading/pitch/roll into objects[obj_idx]. Binary 0x5EAF8.
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
// FUNCTION: TIE95 0x60A4C
void user_calcdeltapitch(int16_t dpitch, int16_t dyaw, uint16_t obj_idx, CraftData* cp) {
	FlightObject* o = &objects[obj_idx];

	uint16_t new_pitch;
	int16_t new_heading;
	int16_t cos_h;
	int16_t sin_h;
	int16_t cos_p;
	int16_t sin_p;
	int32_t cP_sH;
	int32_t sP_sH;
	int32_t cP_cH;
	int32_t sP_cH;
	int32_t neg_sin_h;
	int32_t neg_sin_p;
	int32_t S1;
	int32_t S2;
	int32_t S3;
	int32_t U1;
	int32_t U2;
	int32_t U3;
	int32_t F1;
	int32_t F2;
	int32_t F3;
	int16_t new_roll;

	if (TieOrientationHook_Enabled()) {
		int16_t new_pitch, new_heading, new_roll;
		TieOrientationHook_Apply(o->pitch, o->heading, o->roll, dpitch, dyaw, (inputbuttons & 0xE) != 2,
								 &new_pitch, &new_heading, &new_roll);
		cp->orient_pitch = (uint16_t)new_pitch;
		o->pitch = new_pitch;
		o->heading = new_heading;
		o->roll = new_roll;
		return;
	}

	/* Faithful Q15 reverse-engineered binary 0x5EAF8.  Gimbal-locks at the
	 * world ±Z poles: arctan(calcf1, -calcf2) below collapses to noise when
	 * both inputs are near zero.  All `*(int*)&obj->field >> 16` Watcom
	 * unaligned loads are rewritten here. */
	if (o->orient_dirty) {
		fview_calcrotatemove(o->pitch, o->heading, o);
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

	fview_transformaxes(calcS1, calcS2, calcS3, dpitch);
	if ((inputbuttons & 0xE) != 2)
		fview_transformaxes(calcU1, calcU2, calcU3, dyaw);

	new_pitch = (uint16_t)trig2_arccos(-(int16_t)calcf3);
	cp->orient_pitch = new_pitch;
	new_heading = (int16_t)-trig2_arctan((int16_t)calcf1, -(int16_t)calcf2);

	cos_h = trig2_getsignedcos(new_heading);
	sin_h = trig2_getsignedsin(new_heading);
	cos_p = trig2_getsignedcos((int16_t)new_pitch);
	sin_p = trig2_getsignedsin((int16_t)new_pitch);

	cP_sH = (cos_p * sin_h) >> 15;
	sP_sH = (sin_p * sin_h) >> 15;
	cP_cH = (cos_p * cos_h) >> 15;
	sP_cH = (sin_p * cos_h) >> 15;
	neg_sin_h = -(int32_t)sin_h;
	neg_sin_p = -(int32_t)sin_p;

	/* Rotate each of the three basis vectors (S, U, f) by the new euler. */

	S1 = neg_sin_h * calcS2 + (int32_t)cos_h * calcS1;
	if (S1 >= 0x40000000)
		S1 = 0x3FFF0000;
	if (S1 <= -0x40000000)
		S1 = -0x3FFF0000;
	S2 = neg_sin_p * calcS3 + (int16_t)cP_cH * calcS2 + (int16_t)cP_sH * calcS1;
	if (S2 >= 0x40000000)
		S2 = 0x3FFF0000;
	if (S2 <= -0x40000000)
		S2 = -0x3FFF0000;
	S3 = (int32_t)cos_p * calcS3 + (int16_t)sP_cH * calcS2 + (int16_t)sP_sH * calcS1;
	if (S3 >= 0x40000000)
		S3 = 0x3FFF0000;
	if (S3 <= -0x40000000)
		S3 = -0x3FFF0000;
	calcS1 = (int16_t)(S1 >> 15);
	calcS2 = (int16_t)(S2 >> 15);
	calcS3 = (int16_t)(S3 >> 15);

	U1 = neg_sin_h * calcU2 + (int32_t)cos_h * calcU1;
	if (U1 >= 0x40000000)
		U1 = 0x3FFF0000;
	if (U1 <= -0x40000000)
		U1 = -0x3FFF0000;
	U2 = neg_sin_p * calcU3 + (int16_t)cP_cH * calcU2 + (int16_t)cP_sH * calcU1;
	if (U2 >= 0x40000000)
		U2 = 0x3FFF0000;
	if (U2 <= -0x40000000)
		U2 = -0x3FFF0000;
	U3 = (int32_t)cos_p * calcU3 + (int16_t)sP_cH * calcU2 + (int16_t)sP_sH * calcU1;
	if (U3 >= 0x40000000)
		U3 = 0x3FFF0000;
	if (U3 <= -0x40000000)
		U3 = -0x3FFF0000;
	calcU1 = (int16_t)(U1 >> 15);
	calcU2 = (int16_t)(U2 >> 15);
	calcU3 = (int16_t)(U3 >> 15);

	F1 = neg_sin_h * calcf2 + (int32_t)cos_h * calcf1;
	if (F1 >= 0x40000000)
		F1 = 0x3FFF0000;
	if (F1 <= -0x40000000)
		F1 = -0x3FFF0000;
	F2 = neg_sin_p * calcf3 + (int16_t)cP_cH * calcf2 + (int16_t)cP_sH * calcf1;
	if (F2 >= 0x40000000)
		F2 = 0x3FFF0000;
	if (F2 <= -0x40000000)
		F2 = -0x3FFF0000;
	F3 = (int32_t)cos_p * calcf3 + (int16_t)sP_cH * calcf2 + (int16_t)sP_sH * calcf1;
	if (F3 >= 0x40000000)
		F3 = 0x3FFF0000;
	if (F3 <= -0x40000000)
		F3 = -0x3FFF0000;
	calcf1 = (int16_t)(F1 >> 15);
	calcf2 = (int16_t)(F2 >> 15);
	calcf3 = (int16_t)(F3 >> 15);

	new_roll = trig2_arctan((int16_t)calcS2, (int16_t)calcS1);
	o->roll = (int16_t)-new_roll;
	o->heading = new_heading;
}

/*
 * user_checkradio -- validate radio target. Binary 0x5F11C.
 * Side effect: writes craftptr.
 */
// FUNCTION: TIE95 0x61070
int16_t user_checkradio(void) {
	uint16_t status;

	if (pstate.target_obj_idx == 0xFFFF)
		return 0;
	if (pstate.target_obj_idx >= NUM_ACTIVE_CRAFT_SLOTS)
		return 0;
	if (objects[pstate.target_obj_idx].fg_idx != objects[pstate.object_idx].fg_idx
		&& !fg_array[objects[pstate.target_obj_idx].fg_idx].camo_flag)
		return 0;
	status = objects[pstate.target_obj_idx].craft_ptr->status_flags;
	craftptr = objects[pstate.target_obj_idx].craft_ptr;
	if (!status)
		return 0;
	return 1;
}

/*
 * user_assigntarget -- wingman 'attack my target' etc. Binary 0x5F1D8.
 */
// FUNCTION: TIE95 0x6112C
void user_assigntarget(uint16_t new_target_obj, uint16_t msg_template_id) {
	FlightObject* pl = pstate.player;
	/* No-op when the target is an ally. */
	int16_t wingman_count;
	uint16_t last_speaker_obj;
	uint16_t i;
	uint16_t cmdr_mode;

	if (new_target_obj < NUM_ACTIVE_CRAFT_SLOTS && objects[new_target_obj].side == pl->side)
		return;

	wingman_count = 0;
	last_speaker_obj = 0xFFFF;

	for (i = 0; i < NUM_ACTIVE_CRAFT_SLOTS; ++i) {
		CraftData* cp;
		int cur;

		if (i == pstate.object_idx)
			continue;
		if (!objects[i].ship_idx)
			continue;
		if (objects[i].side != pl->side)
			continue;

		cp = objects[i].craft_ptr;
		cur = cp->current_order;
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
			pai_initplan(i);
		}
		cp->pending_radio_command = new_target_obj;
		last_speaker_obj = i;
		++wingman_count;
	}

	if (last_speaker_obj == 0xFFFF)
		return;
	cmdr_mode = (wingman_count == 1) ? 0u : 1u;
	msg_radiomessage(last_speaker_obj, objects[last_speaker_obj].craft_ptr, msg_template_id, cmdr_mode);
}

/*
 * user_findclosestattacker -- nearest enemy targeting obj_idx. Binary 0x5F308.
 */
// FUNCTION: TIE95 0x61268
uint16_t user_findclosestattacker(uint16_t obj_idx) {
	uint32_t best_dist;
	uint16_t best_idx;
	uint16_t i;

	if (obj_idx == 0xFFFF)
		return 0xFFFF;
	best_dist = 0xFFFFFFFFu;
	best_idx = 0xFFFF;
	for (i = 0; i < NUM_ACTIVE_CRAFT_SLOTS; ++i) {
		CraftData* cp;
		int mode;
		uint8_t ff;

		if (!objects[i].ship_idx || i == obj_idx)
			continue;
		cp = objects[i].craft_ptr;
		if (cp->ai_target_ref != obj_idx)
			continue;
		if (!cp->status_flags)
			continue;
		mode = cp->mode_byte;
		if (mode != 12 && mode != 23)
			continue;
		ff = cp->flight_flag;
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
 * Mission-header byte +8 (mission.win_type) bit 0 forces rescue. Otherwise
 * returns true iff nearest hostile is more than twice as far as nearest
 * friendly.
 */
// FUNCTION: TIE95 0x61310
int16_t user_isrescued(uint16_t player_obj_idx) {
	uint32_t nearest_friend;
	uint32_t nearest_hostile;
	uint16_t i;

	if ((int8_t)mission_file_header.mission.win_type & 1)
		return 1;
	nearest_friend = 0x1000000u;
	nearest_hostile = 0x1000000u;

	for (i = 0; i < NUM_ACTIVE_CRAFT_SLOTS; ++i) {
		if (i == player_obj_idx)
			continue;
		if (!objects[i].ship_idx)
			continue;
		if (objects[i].category)
			continue;
		if (!objects[i].genus)
			continue;

		if (objects[i].side == 0 || objects[i].side == 4) {
			pai_distancebetween(player_obj_idx, i);
			if (nearest_friend > (uint32_t)trig2_polardistance)
				nearest_friend = (uint32_t)trig2_polardistance;
		} else if (objects[i].side == 1) {
			pai_distancebetween(player_obj_idx, i);
			if (nearest_hostile > (uint32_t)trig2_polardistance)
				nearest_hostile = (uint32_t)trig2_polardistance;
		}
	}
	if (nearest_hostile / 2 < nearest_friend)
		return 1;
	return 0;
}

/*
 * user_checkreplaycamera -- stop-and-flush helper. Binary 0x5F478.
 */
// FUNCTION: TIE95 0x613D8
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
// FUNCTION: TIE95 0x6142C
void user_ejectcamera(void) {
	fscript_MsSetSequence(16);
	hyperspaceflag = 0;
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
	camera.view_camera_control = 1;
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
// FUNCTION: TIE95 0x5CC44
void user_nextreplaycount(void) {
	uint16_t new_bufcnt;

	++replaytotalcntdown;
	new_bufcnt = (uint16_t)(replaybuffercnt + 1);
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
// FUNCTION: TIE95 0x5CC9C
void user_nextreplaystore(void) { TieReplayRecording_StoreRecord(true); }

/* ================================================================== *
 *                         TOP-LEVEL FRAME                             *
 * ================================================================== */

/*
 * user_userinterface -- top-of-frame dispatcher. Binary 0x5A6B0.
 * Phases:
 *   (0) if paused, poll for resume key and return; world update +
 *       render are skipped by tie_doframe via TieFlightPause_IsActive().
 *   (1) auto-drop destroyed camera.view_target_obj back to the player.
 *   (2) if in replay playback, read next (key,dx,dy,buttons) tuple.
 *   (3) else fetch raw hardware input and handle meta keys (pause/menu).
 *   (4) if recording, append to tape.
 *   (5) dispatch flight controls via user_inputforplane.
 */
// FUNCTION: TIE95 0x5C440
// FUNCTION: TIE98 0x493840
void user_userinterface(void) {
	inputthrottle = UINT32_MAX;
#ifdef TIE_MODERN
	if (TieFlightPause_IsActive()) {
		TieFlightPause_Service();
		return;
	}
#endif

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
		inputthrottle = frame.throttle_command;
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
		uint16_t k;

		feinput_getrawinput();
		feinput_checkinput();
		k = (uint16_t)inputkey;

		if (k >= KEY_ALT_P) {
			if (k == KEY_ALT_P) {
				/* Alt+P: pause. */
#ifdef TIE_MODERN
				TieFlightPause_Enter();
				return;
#else
				int16_t saved_vol = imuse_get_master_vol(im);

				imuse_set_master_vol(im, 0);
				imuse_pause(im);
				msg_messageprintf(MSG_PAUSED);
				while (!feinput_getrawinput())
					;
				msg_messageprintf(MSG_RESUMED);
				calcframerate = 0;
				keypress = 0;
				imuse_set_master_vol(im, saved_vol);
				imuse_resume(im);
				rtsvga2_InvalidatePageCache();
#endif
			} else if (k < KEY_ALT_C) {
				/* nothing */
			} else if (k == KEY_ALT_C) {
				/* Alt+C: pause + options menu. */
				int16_t saved_vol = imuse_get_master_vol(im);
				uint16_t next_view;

				imuse_set_master_vol(im, 0);
				imuse_pause(im);
				blank();

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
			/* 'p': pause (alternate binding). */
#ifdef TIE_MODERN
			TieFlightPause_Enter();
			return;
#else
			int16_t saved_vol = imuse_get_master_vol(im);

			imuse_set_master_vol(im, 0);
			imuse_pause(im);
			msg_messageprintf(MSG_PAUSED);
			while (!feinput_getrawinput())
				;
			msg_messageprintf(MSG_RESUMED);
			calcframerate = 0;
			keypress = 0;
			imuse_set_master_vol(im, saved_vol);
			imuse_resume(im);
			rtsvga2_InvalidatePageCache();
#endif
		} else if (k == KEY_v && !hyperspaceflag && maingameflag && !pstate.hyperin_state) {
			/* 'v': replay screen. The spool flush, recording stop,
			 * blank, and CAMERA-OFF banner all run on this tick; the
			 * port defers the viewer push to the next flight-task step
			 * and posts the RESUMED banner after the viewer pops (see
			 * flight_mission_step). */
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
#ifdef TIE_MODERN
				TieFlightRequest_ReplayViewer();
#else
				replayio_replayscreen();
				msg_messageprintf(MSG_RESUMED);
#endif
			} else {
				msg_messageprintf(MSG_FILM_NONE);
			}
		}

		if (!TieUserTiming_ThrottleCommandEligible())
			(void)TieInput_ReadThrottleCommand(false);
		else if (acceleratedtimesetting <= 1u || !acceleratedtimectr)
			inputthrottle = TieInput_ReadThrottleCommand(true);

		/* Phase 4: append to record tape. */
		if (recordingreplay == 1) {
			const bool records_info_payload = !pstate.hyperin_state && !hyperspaceflag &&
											  TieReplayRecording_KeyStartsInfoPayload((uint16_t)inputkey);
			const uint16_t required_slots = records_info_payload ? 5 : 1;
			if (TieReplayRecording_PrepareSlots(required_slots, records_info_payload)) {
				uint8_t* rp = (uint8_t*)replayptr;
				ReplayInputFrame frame = { 0 };
				frame.throttle_command = inputthrottle;
				/* One record represents the complete admitted PIT interval, even
				 * when it was accumulated from several shorter host calls. */
				frame.delta_us = (uint32_t)frameticks * 4000u;
				frame.key = (uint16_t)inputkey;
				frame.frameticks = (uint8_t)frameticks;
				if (camera.view_camera_control) {
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
				TieReplayFormat_EncodeInputFrame(rp, &frame);
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

				if ((inputbuttons & 0xD) == 1 && !camera.view_camera_control)
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

// FUNCTION: TIE95 0x61544
// FUNCTION: TIE98 0x498430
int32_t user_inflightinfo(int32_t screen_id) {
	uint16_t saved_master_vol;
	uint16_t old_target = 0;
	int16_t retreat_flag;
	int16_t exit_flag;
	int32_t screen;
	uint16_t sub;
#ifdef TIE_MODERN
	InflightInfoTask* continuation = landru_task_top();
	saved_master_vol = continuation->saved_master_vol;
	retreat_flag = continuation->retreat_flag;
	exit_flag = continuation->exit_flag;
	screen = continuation->screen;
	old_target = continuation->old_target;
	if (continuation->phase == INFLIGHT_PHASE_BEGIN)
#endif
	{
		if (mission.train_craft_type && (uint16_t)screen_id <= 4u) {
			rtsvga2_InvalidatePageCache();
#ifdef TIE_MODERN
			continuation->finished = true;
#endif
			return 0xFFFF;
		}
		if (replayviewmode) {
#ifdef TIE_MODERN
			continuation->finished = true;
			return TieInflightInfo_ReadReplay();
#else

			uint16_t* record = (uint16_t*)replayptr;
			uint16_t i;
			uint16_t value;
			int32_t recorded_screen = (int16_t)record[3];
			pstate.radar_target0 = (int16_t)record[0];
			pstate.target_obj_idx = record[1];
			inputkey = (int16_t)record[2];
			replayptr = record + 4;
			user_nextreplaycount();
			for (i = 0; i < 10; i += 2) {
				value = *(uint16_t*)replayptr;
				pstate.subsystem_repair_priority[i] = (uint8_t)(value >> 8);
				pstate.subsystem_repair_priority[i + 1] = (uint8_t)value;
				replayptr = (uint16_t*)replayptr + 1;
				if (((i + 2) & 7) == 0)
					user_nextreplaycount();
			}
			if (i & 7) {
				while (i & 7) {
					replayptr = (uint16_t*)replayptr + 1;
					i += 2;
				}
				user_nextreplaycount();
			}
			record = (uint16_t*)replayptr;
			starshipexplodetail = record[0];
			value = record[1];
			drawdebrisflag = value & 15;
			value >>= 4;
			drawbackdropflag = value & 15;
			value >>= 4;
			stardetaillevel = value & 15;
			starshipdetail = value >> 4;
			value = record[2];
			gouraudflag = (uint8_t)value;
			value >>= 8;
			drawmarkingsflag = value & 15;
			shipdetailvalue = value >> 4;
			value = record[3];
			hyperspacedetail = (uint8_t)value;
			shipdetailpolycnt = value >> 8;
			replayptr = record + 4;
			user_nextreplaycount();
			record = (uint16_t*)replayptr;
			value = record[0];
			cheatingflag = value & 15;
			value >>= 4;
			inflight_unlimited = value & 15;
			value >>= 4;
			inflight_invulnerable = value & 15;
			inflight_collision = value >> 4;
			soundvolflag = (uint8_t)record[1];
			inflight_sound_vol = (int8_t)(record[1] >> 8);
			musicvolflag = (uint8_t)record[2];
			inflight_music_vol = (int8_t)(record[2] >> 8);
			inflight_speech_vol = (int8_t)record[3];
			replayptr = record + 4;
			user_nextreplaycount();
			rtsvga2_InvalidatePageCache();
			return recorded_screen;

#endif
		}
		saved_master_vol = (uint16_t)imuse_get_master_vol(im);
		retreat_flag = 0;
		exit_flag = 0;
		screen = screen_id;
		if (TIE_FLIGHT_TIE98) {
			mapflag = 1;
			fsfx_UpdatePlayerEngineSound();
			mapflag = 0;
		}
		imuse_set_master_vol(im, 0);
		imuse_pause(im);
#ifdef TIE_MODERN
		continuation->saved_master_vol = saved_master_vol;
		continuation->retreat_flag = retreat_flag;
		continuation->exit_flag = exit_flag;
		continuation->screen = screen;
		continuation->old_target = old_target;
		continuation->phase = INFLIGHT_PHASE_DISPATCH;
		return 0;
#endif
	}
	while (!exit_flag) {
#ifdef TIE_MODERN
		if (continuation->phase == INFLIGHT_PHASE_DISPATCH)
#endif
		{
			int panel_idx;
#ifdef TIE_MODERN
			if (screen == 6) {
				TieRuntime_RequestSettingsMenu();
				continuation->exit_flag = 1;
				continuation->phase = INFLIGHT_PHASE_FINISH;
				return 0;
			}
			TieFlightScreen_SetActive(screen < 0 || screen > 6 ? TIE_FLIGHT_SCREEN_NORMAL
															   : (TieFlightScreen)(screen + 1));
#endif
			if (TIE_DISPLAY_DX5)
				FlightSurface_Lock();
			panel_idx = (int)(uint16_t)(screen + 21);
			if (!panelviewptrs[panel_idx].handle) {
				temppanelptr = newbuf;
				panel_loadcontrolpanel(panelviewdefs[panel_idx].name, &panelviewptrs[panel_idx].image, 3u);
			}
			buildpalette((const uint8_t*)panelviewptrs[panel_idx].palette, 0, 64);
			drawshape(panelviewptrs[panel_idx].image, 0, 0, 253, 0);
			festring_showscreen();
			if (TIE_DISPLAY_DX5)
				FlightSurface_Unlock();
			sub = 0;
#ifdef TIE_MODERN
			TieFlightRequest_SetSubmodalResult(0);
#endif
			switch ((int16_t)screen) {
				case 0:
					if (!mission.train_craft_type) {
#ifdef TIE_MODERN
						TieGoals_Begin();
#else
						sub = (uint16_t)goals_missiongoalsroom();
#endif
					}
					break;
				case 1:
					if (!mission.train_craft_type) {
						old_target = pstate.target_obj_idx;
#ifdef TIE_MODERN
						TieMaproom_Begin();
#else
						sub = (uint16_t)maproom_maproom();
#endif
					}
					break;
				case 2:
					if (!mission.train_craft_type) {
#ifdef TIE_MODERN
						TieMsgRoom_Begin();
#else
						sub = (uint16_t)msgroom_messageroom();
#endif
					}
					break;
				case 3:
					if (!mission.train_craft_type) {
#ifdef TIE_MODERN
						TieDamage_Begin();
#else
						sub = (uint16_t)damage_damageroom();
#endif
					}
					break;
				case 4:
					if (!mission.train_craft_type) {
#ifdef TIE_MODERN
						TieWingman_Begin();
#else
						sub = (uint16_t)wingman_wingmanroom();
#endif
					}
					break;
				case 5:
#ifdef TIE_MODERN
					TieHelp_Begin(retreat_flag);
#else
					sub = (uint16_t)help_helproom(retreat_flag);
#endif
					break;
				case 6:
#ifndef TIE_MODERN
					sub = (uint16_t)option_optionsroom(0);
#endif
					break;
				default:
					break;
			}
#ifdef TIE_MODERN
			continuation->saved_master_vol = saved_master_vol;
			continuation->retreat_flag = retreat_flag;
			continuation->exit_flag = exit_flag;
			continuation->screen = screen;
			continuation->old_target = old_target;
			continuation->phase = INFLIGHT_PHASE_AFTER_SUB;
			return 0;
#endif
		}
#ifdef TIE_MODERN
		sub = (uint16_t)TieFlightRequest_SubmodalResult();
#endif
		if (screen == 1 && !mission.train_craft_type && old_target != pstate.target_obj_idx) {
			uint16_t new_target = pstate.target_obj_idx;
			pstate.target_obj_idx = old_target;
			user_setnewtarget(new_target);
		}
		if (mission.end_flag) {
			festring_setfontsize(2);
			imuse_set_master_vol(im, saved_master_vol);
			imuse_resume(im);
			rtsvga2_InvalidatePageCache();
#ifdef TIE_MODERN
			continuation->finished = true;
#endif
			return screen;
		}
		if (sub == 0) {
			exit_flag = 1;
		} else if (sub == 0xFFFF) {
			--screen;
			if (screen & 0x8000)
				screen = 6;
			if (mission.train_craft_type && screen == 4)
				screen = 6;
			retreat_flag = 1;
		} else if (sub != 1) {
			screen = -1;
			exit_flag = 1;
		} else {
			if (++screen > 6)
				screen = mission.train_craft_type ? 5 : 0;
			retreat_flag = 0;
		}
#ifdef TIE_MODERN
		continuation->saved_master_vol = saved_master_vol;
		continuation->retreat_flag = retreat_flag;
		continuation->exit_flag = exit_flag;
		continuation->screen = screen;
		continuation->old_target = old_target;
		continuation->phase = exit_flag ? INFLIGHT_PHASE_FINISH : INFLIGHT_PHASE_DISPATCH;
		return 0;
#endif
	}
	blank();
#ifdef TIE_MODERN
	TieInflightInfo_RecordRoom(screen);
#else

	if (recordingreplay) {
		uint16_t* record = (uint16_t*)replayptr;
		uint16_t i;
		record[0] = (uint16_t)pstate.radar_target0;
		record[1] = pstate.target_obj_idx;
		record[2] = (uint16_t)inputkey;
		record[3] = (uint16_t)screen;
		replayptr = record + 4;
		user_nextreplaystore();
		for (i = 0; i < 10; i += 2) {
			*(uint16_t*)replayptr = (uint16_t)(pstate.subsystem_repair_priority[i + 1] +
											   (pstate.subsystem_repair_priority[i] << 8));
			replayptr = (uint16_t*)replayptr + 1;
			if (((i + 2) & 7) == 0)
				user_nextreplaystore();
		}
		if (i & 7) {
			while (i & 7) {
				*(uint16_t*)replayptr = 0;
				replayptr = (uint16_t*)replayptr + 1;
				i += 2;
			}
			user_nextreplaystore();
		}
		record = (uint16_t*)replayptr;
		record[0] = starshipexplodetail;
		record[1] = (uint16_t)(drawdebrisflag +
							   16 * (drawbackdropflag + 16 * (stardetaillevel + 16 * starshipdetail)));
		record[2] = (uint16_t)(gouraudflag + ((drawmarkingsflag + 16 * shipdetailvalue) << 8));
		record[3] = (uint16_t)(hyperspacedetail + (shipdetailpolycnt << 8));
		replayptr = record + 4;
		user_nextreplaystore();
		record = (uint16_t*)replayptr;
		record[0] = (uint16_t)(cheatingflag + 16 * (inflight_unlimited +
													16 * (inflight_invulnerable + 16 * inflight_collision)));
		record[1] = (uint16_t)(soundvolflag + (inflight_sound_vol << 8));
		record[2] = (uint16_t)(musicvolflag + (inflight_music_vol << 8));
		record[3] = (uint16_t)(int16_t)inflight_speech_vol;
		replayptr = record + 4;
		user_nextreplaystore();
	}

#endif
	{
		uint16_t pilotview_restore;
		festring_setfontsize(2);
		if (camera.view_zoom_flag) {
			camera.pilotview = 0xFF;
			lastpilotpaneldraw = -1;
			pilotview_restore = camera.view_target_tracking ? 20u : 18u;
		} else {
			pilotview_restore = (camera.view_target_obj == pstate.object_idx) ? camera.pilotview : 18u;
			lastpilotpaneldraw = -1;
			camera.pilotview = 0xFF;
		}
		panelrts_setnewpilotview(pilotview_restore);
		msg_messageinit();
		msg_messagerestore();
		if (TIE_FLIGHT_TIE98)
			g_flightInitialTextureCacheFlushPending = 1;
		fullupdateflag = 1;
		imuse_set_master_vol(im, (int16_t)saved_master_vol);
		imuse_resume(im);
		/* Retail USER_inflightinfo @ 0x61a53: force the next
		 * rtsvga2_SetCurrentPage to re-program the VESA bank so the
		 * cockpit panel and HUD regain their pages after the info room. */
		rtsvga2_InvalidatePageCache();
	}
#ifdef TIE_MODERN
	continuation->finished = true;
#endif
	return screen;
}

/*
 * user_inputforplane -- per-frame in-flight control dispatcher. Large
 * switch on inputkey covering ~150 bindings (flight controls, weapons,
 * shields, view, replay, F-keys, info-rooms). See binary 0x5ADC0.
 *
 * The binary is a single 12kB function; each binding is recovered in
 * place in its switch case.
 */
// FUNCTION: TIE95 0x5CDA0
void user_inputforplane(void) {
	uint16_t screen;
#ifdef TIE_MODERN
	int32_t frame_key = -1;
	int16_t room_key;
#endif

	/* Phase 0: hyperspace-abort on 'h'. */
	if (hyperspaceflag < 2 && hyperabortflag && (uint16_t)inputkey == KEY_h && hyperspaceflag) {
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

	/* An info room that closes on the wingmen screen (4) leaves the key
	 * pressed there in inputkey, which is dispatched again. */
#ifdef TIE_MODERN
	/* PORT: info rooms run as a task between frames, so a key left by the
	 * wingmen screen arrives with the next frame: dispatch it first, then
	 * this frame's own key. */
	if (TieFlightRequest_ConsumeRoomKey(&room_key)) {
		frame_key = (uint16_t)inputkey;
		inputkey = room_key;
	}
#endif
	do {
		screen = 0xFFFF;
		/* Training missions ignore the targeting and target-recall keys. */
		if (mission.train_craft_type) {
			switch ((uint16_t)inputkey) {
				case KEY_a:
				case KEY_e:
				case KEY_r:
				case KEY_t:
				case KEY_u:
				case KEY_y:
				case KEY_F5:
				case KEY_F6:
				case KEY_F7:
					inputkey = KEY_NONE;
					break;
			}
		}

		switch ((uint16_t)inputkey) {
			/* Alt+O: screenshot. */
			case KEY_ALT_O:
				if (TIE_DISPLAY_DX5)
					FrontendDisplay_CaptureScreenshot();
				else
					rtsvga2_takeScreenshot();
				break;
			/* 'o': drop current target lock and reset external view back to
			 * the player's cockpit. */
			case KEY_o:
				pstate.target_obj_idx = 0xFFFF;
				if (!replayviewmode && camera.view_target_tracking) {
					camera.view_target_tracking = 0;
					camera.view_zoom_flag = 0;
					camera.view_camera_control = 0;
					camera.view_target_obj = pstate.object_idx;
					panelrts_setnewpilotview(0);
					camera.side_angle = 0;
					camera.up_angle = 0;
					targetblinkflag = 0;
					lasttargetnum = -2;
				}
				break;
			/* Confirm pending action. */
			case KEY_SPACE: {
				if (!pstate.space_confirm_action)
					break;
				switch (pstate.space_confirm_action) {
				case 1:
					pstate.target_obj_idx = (uint16_t)pstate.msg_arg_obj_idx;
					if (!replayviewmode && camera.view_target_tracking)
						camera.view_target_obj = pstate.target_obj_idx;
					pstate.radar_subtarget_state = 0;
					pstate.player_craft->missile_count_total = 0;
					msg_messageprintf(MSG_WARHEAD_TARGETED);
					pstate.space_confirm_action = 0;
					break;
				case 2:
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
					break;
				case 3:
					mission.penalty_flag = 1;
					msg_messageprintf(MSG_REINFORCE_ACK);
					pstate.space_confirm_action = 0;
					fsfx_triggervoicesfx(0x66u);
					fsfx_triggervoicesfx(0x67u);
					fsfx_triggervoicesfx(0x68u);
					break;
				}
				break;
			}
			/* Toggle wing-level / high-angle view. */
			case KEY_0: {
				if (replayviewmode)
					break;
				if (camera.pilotview >= 16 && !camera.view_zoom_flag)
					break;
				camera.view_dir_dirty ^= 8u;
				camera.side_angle = (uint16_t)camera.view_dir_dirty << 10;
				if (!camera.view_zoom_flag && camera.view_target_obj == pstate.object_idx)
					panelrts_setnewpilotview((uint8_t)(camera.pilotview ^ 8));
				break;
			}
			/* Snap side view (center of numpad). */
			case KEY_5: {
				if (replayviewmode)
					break;
				camera.up_angle = 0;
				camera.side_angle = 0x4000;
				if (!camera.view_zoom_flag && camera.view_target_obj == pstate.object_idx)
					panelrts_setnewpilotview(0x10u);
				break;
			}
			/* Numpad camera angles 1..4 and 6..9. */
			case KEY_1:
			case KEY_2:
			case KEY_3:
			case KEY_4:
			case KEY_6:
			case KEY_7:
			case KEY_8:
			case KEY_9: {
				if (replayviewmode)
					break;
				if (!camera.view_zoom_flag && camera.view_target_obj == pstate.object_idx)
					panelrts_setnewpilotview(camera.view_dir_dirty +
											 viewtranslate[(uint16_t)inputkey - KEY_0]);
				camera.side_angle = (uint16_t)camera.view_dir_dirty << 10;
				camera.up_angle = looktranslate[(uint16_t)inputkey - KEY_0];
				break;
			}
			/* Toggle cockpit and recenter the view. */
			case KEY_PERIOD:
				if (!replayviewmode) {
					if (!camera.view_zoom_flag && camera.view_target_obj == pstate.object_idx)
						panelrts_setnewpilotview(camera.pilotview != 19 ? (uint16_t)19 : (uint16_t)0);
					camera.side_angle = 0;
					camera.up_angle = 0;
				}
				break;
			/* F1: return to the forward cockpit. */
			case KEY_F1:
				if (!replayviewmode) {
					camera.view_target_tracking = 0;
					camera.view_zoom_flag = 0;
					camera.view_target_obj = pstate.object_idx;
					camera.view_camera_control = 0;
					panelrts_setnewpilotview(0);
					camera.side_angle = 0;
					camera.up_angle = 0;
				}
				break;
			/* F2: select or cycle warhead view. */
			case KEY_F2: {
				uint16_t scan;
				uint16_t found;
				int16_t k;

				if (replayviewmode || camera.view_target_tracking)
					break;
				/* Follow the next of the player's warheads after the one in view. */
				scan = (camera.view_target_obj == pstate.object_idx) ? (uint16_t)(NUM_CRAFTS - 1)
																	 : camera.view_target_obj;
				found = 0xFFFF;
				for (k = NUM_CRAFTS; k < WARHEAD_SLOT_END; ++k) {
					if (++scan >= WARHEAD_SLOT_END)
						scan = NUM_CRAFTS;
					if (objects[scan].genus == GENUS_PROJECTILE_PLAYER &&
#ifdef TIE_MODERN
						/* PORT: freed slots keep their genus after ship_idx is cleared,
						 * which makes the original read before the table. */
						(unsigned int)(objects[scan].ship_idx - WEAPON_SPECIES_BASE) < WARHEAD_TYPE_COUNT &&
#endif
						projectile_is_warhead_type[objects[scan].ship_idx - WEAPON_SPECIES_BASE]) {
						if (pstate.object_idx == (uint16_t)objects[scan].self_idx)
							found = scan;
						break;
					}
				}
				if (found != 0xFFFF) {
					if (camera.view_target_obj == pstate.object_idx) {
						camera.view_zoom_flag = 0;
						camera.view_saved_idx = camera.pilotview;
						camera.view_saved_side_angle = camera.side_angle;
						camera.view_saved_up_angle = camera.up_angle;
					}
					camera.view_target_obj = scan;
					user_resetview();
				}
				break;
			}
			/* '/' or F3: toggle external camera (LABEL_207). */
			case KEY_SLASH:
			case KEY_F3:
				if (replayviewmode || camera.view_target_tracking)
					break;
				camera.view_zoom_flag = !camera.view_zoom_flag;
				if (camera.view_zoom_flag && camera.view_target_obj == pstate.object_idx) {
					camera.view_saved_idx = camera.pilotview;
					camera.view_saved_side_angle = camera.side_angle;
					camera.view_saved_up_angle = camera.up_angle;
				}
				user_resetview();
				break;
			/* '*' / F4: toggle external-camera positioning controls. */
			case KEY_ASTERISK:
			case KEY_F4:
				if (replayviewmode)
					break;
				if (camera.view_zoom_flag)
					camera.view_camera_control = (camera.view_camera_control == 0);
				fsfx_triggersfx(0x21u, 0xFFFF);
				break;
			/* 'z': target-viewer toggle. */
			case KEY_z: {
				if (replayviewmode)
					break;
				if (camera.view_target_tracking) {
					camera.view_target_tracking = 0;
					targetblinkflag = 0;
					camera.view_zoom_flag = 0;
					camera.view_camera_control = 0;
					camera.view_target_obj = pstate.object_idx;
					lasttargetnum = -2;
					panelrts_setnewpilotview(0);
					camera.side_angle = 0;
					camera.up_angle = 0;
				} else if (pstate.target_obj_idx != 0xFFFF) {
					if (!camera.view_zoom_flag) {
						camera.view_saved_idx = camera.pilotview;
						camera.view_saved_side_angle = camera.side_angle;
						camera.view_saved_up_angle = camera.up_angle;
					}
					camera.view_zoom_flag = 1;
					camera.view_target_tracking = 1;
					camera.view_target_obj = pstate.target_obj_idx;
					targetblinkflag = 1024;
					user_resetview();
				} else {
					msg_messageprintf(MSG_NO_TARGET);
				}
				break;
			}
			/* Throttle up step. */
			case KEY_PLUS:
			case KEY_EQUALS: {
				uint16_t cur = pstate.player_craft->throttle_speed;
				uint16_t nxt = (uint16_t)(cur + 0x800);
				pstate.player_craft->throttle_speed = nxt;
				if (cur > nxt)
					pstate.player_craft->throttle_speed = 0xFFFF;
				fsfx_triggersfx(0x21u, 0xFFFF);
				break;
			}
			/* ']': throttle 2/3. */
			case KEY_RBRACKET:
				pstate.player_craft->throttle_speed = (uint16_t)(int16_t)-21846;
				fsfx_triggersfx(0x21u, 0xFFFF);
				argtable[0] = 83;
				msg_messageprintf(MSG_THROTTLE_SET);
				break;
			/* Throttle down step + ack beep (LABEL_479). */
			case KEY_MINUS: {
				uint16_t cur = pstate.player_craft->throttle_speed;
				uint16_t nxt = (uint16_t)(cur - 0x800);
				pstate.player_craft->throttle_speed = nxt;
				if (cur < nxt)
					pstate.player_craft->throttle_speed = 0;
				fsfx_triggersfx(0x21u, 0xFFFF);
				break;
			}
			/* '[': throttle 1/3. */
			case KEY_LBRACKET:
				pstate.player_craft->throttle_speed = 21845;
				fsfx_triggersfx(0x21u, 0xFFFF);
				argtable[0] = 82;
				msg_messageprintf(MSG_THROTTLE_SET);
				break;
			/* '\\': throttle off. */
			case KEY_BACKSLASH:
				pstate.player_craft->throttle_speed = 0;
				fsfx_triggersfx(0x21u, 0xFFFF);
				argtable[0] = 81;
				msg_messageprintf(MSG_THROTTLE_SET);
				break;
			/* Throttle full. */
			case KEY_BACKSPACE:
				pstate.player_craft->throttle_speed = 0xFFFF;
				fsfx_triggersfx(0x21u, 0xFFFF);
				argtable[0] = 84;
				msg_messageprintf(MSG_THROTTLE_SET);
				break;
			/* Match target speed. */
			case KEY_ENTER: {
				uint16_t cur;
				uint16_t slack;
				uint16_t match_speed;

				if (pstate.target_obj_idx == 0xFFFF)
					break;
				if (pstate.target_obj_idx >= 0x3800) {
					pstate.player_craft->throttle_speed = 0;
					break;
				}
				if (pstate.target_obj_idx >= NUM_ACTIVE_CRAFT_SLOTS) {
					pstate.player_craft->throttle_speed = 0xFFFF;
					break;
				}
				/* Top speed scales with the power left over by the shield,
				 * beam and cannon settings. */
				cur = (uint16_t)objects[pstate.target_obj_idx].current_speed;
				slack = (uint16_t)(6 - (pstate.player_craft->beam_power + pstate.player_craft->shield_power +
										pstate.player_craft->laser_power));
				if (slack >= 0x8000)
					match_speed = (uint16_t)(pstate.player_craft->max_speed_cache -
											 math2_fraction((int16_t)(-slack << 13),
															(uint16_t)pstate.player_craft->max_speed_cache));
				else
					match_speed = (uint16_t)(math2_fraction((int16_t)(slack << 13),
															(uint16_t)pstate.player_craft->max_speed_cache) +
											 pstate.player_craft->max_speed_cache);
				if (cur >= match_speed) {
					pstate.player_craft->throttle_speed = 0xFFFF;
					msg_messageprintf(MSG_MATCHING_SPEED_MAX);
				} else {
					pstate.player_craft->throttle_speed = math2_percentage(cur, match_speed);
					msg_messageprintf(MSG_MATCHING_SPEED);
				}
				break;
			}
			/* 'n': overdrive toggle. */
			case KEY_n: {
				int16_t has_charge;
				uint16_t i;

				if (pstate.player_spec_num == spec_getspecnum(0xCu)) {
					pstate.player_craft->slam_active ^= 0xFFFF;
					if (!pstate.player_craft->slam_active) {
						/* Engaging needs a charged cannon to drain. */
						has_charge = 0;
						for (i = 0; i < pstate.player_craft->weapon_group_cnt; ++i) {
							if ((int8_t)pstate.player_craft->weapon_slots[i].charge > 0)
								has_charge = 1;
						}
						if (has_charge) {
							msg_messageprintf(MSG_OVERDRIVE_ENGAGED);
							fsfx_triggersfx(0x6Bu, 0xFFFF);
						} else {
							pstate.player_craft->slam_active = 0xFFFF;
							msg_messageprintf(MSG_OVERDRIVE_FAIL);
						}
					} else {
						msg_messageprintf(MSG_OVERDRIVE_DISENGAGED);
						fsfx_triggersfx(0x6Cu, 0xFFFF);
					}
				}
				break;
			}
			/* 'w': weapon group cycle. */
			case KEY_w: {
				uint8_t group = ++pstate.player_weapon_group;
				uint16_t warhead_msg;
				uint16_t msg;

#ifdef TIE_MODERN
				/* PORT: an unknown warhead type leaves this unset in the original. */
				warhead_msg = 0;
#endif
				if (!pstate.player_weapon_mode) {
					if (group >= pstate.player_craft->laser_group_cnt) {
						if (pstate.player_craft->missile_group_cnt)
							pstate.player_weapon_mode = 1;
						pstate.player_weapon_group = 0;
					}
				} else if (group >= pstate.player_craft->missile_group_cnt) {
					if (pstate.player_craft->laser_group_cnt)
						pstate.player_weapon_mode = 0;
					pstate.player_weapon_group = 0;
				}

				argtable[1] = 25;
				if (!pstate.player_weapon_mode) {
					if (pstate.player_craft->status_flags & 0x10) {
						msg = (uint16_t)(pstate.player_weapon_group + 3);
					} else {
						argtable[0] = (uint16_t)(pstate.player_weapon_group + 29);
						msg = MSG_SYSTEM_STATUS;
					}
				} else if (pstate.player_craft->status_flags & 8) {
					switch ((uint16_t)pstate.player_craft->warhead_type[pstate.player_weapon_group]) {
						case 0x8F:
							warhead_msg = 8;
							break;
						case 0x90:
							warhead_msg = 9;
							break;
						case 0x94:
							warhead_msg = 184;
							break;
						case 0x95:
							warhead_msg = 185;
							break;
						case 0x96:
							warhead_msg = 186;
							break;
						case 0x97:
							warhead_msg = 187;
							break;
						case 0x98:
							warhead_msg = 188;
							break;
					}
					argtable[0] = warhead_msg;
					msg = MSG_LAUNCHERS_ARMED;
				} else {
					argtable[0] = 31;
					msg = MSG_SYSTEM_STATUS;
				}
				msg_messageprintf((MsgTemplate)msg);
				fsfx_triggersfx(0x21u, 0xFFFF);
				break;
			}
			/* 'x': cycle the laser linking or toggle the missile firing mode. */
			case KEY_x: {
				uint16_t nl;
				uint16_t warhead_msg;

#ifdef TIE_MODERN
				/* PORT: an unknown warhead type leaves this unset in the original. */
				warhead_msg = 0;
#endif
				if (!pstate.player_weapon_mode) {
					if (spec_data[pstate.player_spec_num].laser_count[pstate.player_weapon_group] != 1) {
						nl = (uint16_t)(pstate.player_craft->laser_owner_player[pstate.player_weapon_group] +
										1);
						if (nl > 3)
							nl = 1;
						if (spec_data[pstate.player_spec_num].laser_count[pstate.player_weapon_group] != 4 &&
							nl == 2)
							nl = 3;
						pstate.player_craft->laser_owner_player[pstate.player_weapon_group] = (uint8_t)nl;
						pstate.player_craft->laser_first_slot[pstate.player_weapon_group] =
							spec_data[pstate.player_spec_num].laser_start[pstate.player_weapon_group];
						msg_messageprintf((MsgTemplate)(nl + 9));
					}
				} else {
					pstate.player_craft->missile_armed[pstate.player_weapon_group] ^= 2u;
					switch ((uint16_t)pstate.player_craft->warhead_type[pstate.player_weapon_group]) {
						case 0x8F:
							warhead_msg = 8;
							break;
						case 0x90:
							warhead_msg = 9;
							break;
						case 0x94:
							warhead_msg = 184;
							break;
						case 0x95:
							warhead_msg = 185;
							break;
						case 0x96:
							warhead_msg = 186;
							break;
						case 0x97:
							warhead_msg = 187;
							break;
						case 0x98:
							warhead_msg = 188;
							break;
					}
					argtable[0] = warhead_msg;
					msg_messageprintf(
						(MsgTemplate)(((pstate.player_craft->missile_armed[pstate.player_weapon_group] &
										0x7F) >>
									   1) +
									  6));
				}
				fsfx_triggersfx(0x21u, 0xFFFF);
				break;
			}
			/* F9: cannon-rate cycle. */
			case KEY_F9: {
				uint16_t msg;

				++pstate.player_craft->laser_power;
				if (pstate.player_craft->laser_power >= 5)
					pstate.player_craft->laser_power = 0;

				if (pstate.player_craft->status_flags & 0x10) {
					argtable[0] = (uint16_t)(pstate.player_craft->laser_power + 19);
					msg = MSG_CANNON_RATE;
				} else {
					argtable[0] = 29;
					argtable[1] = 25;
					msg = MSG_SYSTEM_STATUS;
				}
				msg_messageprintf((MsgTemplate)msg);
				fsfx_triggersfx(0x21u, 0xFFFF);
				break;
			}
			/* Transfer shields -> cannon (also Shift+F9, synthesized from the
			 * joystick chord-11 release in user_userinterface). */
			case KEY_SEMICOLON:
			case KEY_SHIFT_F9: {
				int16_t charge_sum;
				uint16_t j;
				int16_t step;
				int16_t xfer;
				int16_t half;
				int16_t total;
				int16_t pips;
				uint16_t k;
				int16_t i;

				if (pstate.player_craft->subsystem_active & 1) {
					charge_sum = 0;
					for (j = 0; j < pstate.player_craft->weapon_group_cnt; ++j)
						charge_sum += 127 - (int8_t)pstate.player_craft->weapon_slots[j].charge;
					if (pstate.player_spec_num == spec_getspecnum(0xCu))
						step = 32;
					else
						step = 8;
					if (charge_sum > 100)
						charge_sum = 100;
					xfer = (int16_t)(charge_sum * step);

					if (!pstate.player_craft->is_player_craft) {
						if (xfer > pstate.player_craft->forward_shield)
							xfer = pstate.player_craft->forward_shield;
						pstate.player_craft->forward_shield -= xfer;
						total = xfer;
					} else if (pstate.player_craft->is_player_craft == 2) {
						if (xfer > pstate.player_craft->rear_shield)
							xfer = pstate.player_craft->rear_shield;
						pstate.player_craft->rear_shield -= xfer;
						total = xfer;
					} else {
						/* Each half takes from its own shield, but the rear half is
						 * capped by comparing the whole transfer with the rear shield. */
						half = (int16_t)(xfer >> 1);
						if (half > pstate.player_craft->forward_shield)
							half = pstate.player_craft->forward_shield;
						pstate.player_craft->forward_shield -= half;
						total = half;
						half = (int16_t)(xfer >> 1);
						if (xfer > pstate.player_craft->rear_shield)
							half = pstate.player_craft->rear_shield;
						pstate.player_craft->rear_shield -= half;
						total += half;
					}

					/* Each pip recharges one cannon by one unit, cycling through them. */
					pips = total / step;
					if (!pips)
						break;
					k = 0;
					i = 0;
					while (pips > 0 && i < 100) {
						if ((int8_t)pstate.player_craft->weapon_slots[k].charge < 127)
							++pstate.player_craft->weapon_slots[k].charge;
						++k;
						--pips;
						if (k >= pstate.player_craft->weapon_group_cnt)
							k = 0;
						++i;
					}
					fsfx_triggersfx(0x21u, 0xFFFF);
					msg_messageprintf(MSG_XFER_SHIELDS_TO_CANNON);
				} else {
					argtable[0] = 35;
					msg_messageprintf(MSG_NO_SUCH_SYSTEM);
				}
				break;
			}
			/* 's': shield mode cycle. Also synthesized on button-chord-7
			 * release. */
			case KEY_s: {
				int16_t cap;
				uint16_t pct;
				int16_t total;
				int16_t fwd;

				if (pstate.player_craft->subsystem_active & 1) {
					if (pstate.player_craft->status_flags & 1) {
						if (++pstate.player_craft->is_player_craft > 2) {
							pstate.player_craft->is_player_craft = 0;
							user_adjustshields(0, 1);
						} else if (pstate.player_craft->is_player_craft == 2) {
							user_adjustshields(1, 0);
						} else {
							/* Balanced: split the total by the forward share of the cap. */
							cap = (int16_t)(2 * spec_data[pstate.player_spec_num].shield_points);
							if (!mission.difficulty) {
								cap *= 2;
								pct = math2_percentage(
									(uint16_t)(2 * spec_data[pstate.player_spec_num].shield_points),
									(uint16_t)cap);
							} else {
								pct = math2_percentage(spec_data[pstate.player_spec_num].shield_points,
													   (uint16_t)cap);
							}
							total = (int16_t)(pstate.player_craft->forward_shield +
											  pstate.player_craft->rear_shield);
							if (total > 0) {
								fwd = (int16_t)math2_fraction(total, pct);
								total -= fwd;
								pstate.player_craft->forward_shield = fwd;
								pstate.player_craft->rear_shield = total;
							}
						}
						argtable[0] = (uint16_t)(pstate.player_craft->is_player_craft + 14);
						msg_messageprintf(MSG_SHIELDS_SET);
						fsfx_triggersfx(0x21u, 0xFFFF);
					} else {
						argtable[1] = 25;
						argtable[0] = 35;
						msg_messageprintf(MSG_SYSTEM_STATUS);
					}
				} else {
					argtable[0] = 35;
					msg_messageprintf(MSG_NO_SUCH_SYSTEM);
				}
				break;
			}
			/* F10: shield-rate cycle. */
			case KEY_F10: {
				if (pstate.player_craft->subsystem_active & 1) {
					if (pstate.player_craft->status_flags & 1) {
						++pstate.player_craft->shield_power;
						if (pstate.player_craft->shield_power >= 5)
							pstate.player_craft->shield_power = 0;
						argtable[0] = (uint16_t)(pstate.player_craft->shield_power + 19);
						msg_messageprintf(MSG_SHIELD_RATE);
						fsfx_triggersfx(0x21u, 0xFFFF);
					} else {
						argtable[1] = 25;
						argtable[0] = 35;
						msg_messageprintf(MSG_SYSTEM_STATUS);
					}
				} else {
					argtable[0] = 35;
					msg_messageprintf(MSG_NO_SUCH_SYSTEM);
				}
				break;
			}
			/* Transfer cannon -> shields (also Shift+F10). */
			case KEY_APOSTROPHE:
			case KEY_SHIFT_F10: {
				int16_t cap;
				int16_t need;
				int16_t cycles;
				int16_t step;
				uint16_t k;
				int16_t i;

				if (pstate.player_craft->subsystem_active & 1) {
					if (pstate.player_craft->status_flags & 1) {
						cap = (int16_t)(2 * spec_data[pstate.player_spec_num].shield_points);
						if (!mission.difficulty)
							cap *= 2;
						if (!pstate.player_craft->is_player_craft) {
							need = (int16_t)(cap - pstate.player_craft->forward_shield);
						} else if (pstate.player_craft->is_player_craft == 2) {
							need = (int16_t)(cap - pstate.player_craft->rear_shield);
						} else {
							need = (int16_t)((cap - pstate.player_craft->forward_shield) +
											 (cap - pstate.player_craft->rear_shield));
							if (need < 0)
								need = 800;
						}
						if (need > 800)
							need = 800;

						/* Each cycle moves one unit of cannon charge into the shields. */
						if (pstate.player_spec_num == spec_getspecnum(0xCu)) {
							need /= 32;
							cycles = need;
							step = 32;
						} else {
							need /= 8;
							cycles = need;
							step = 8;
						}
						if (!cycles)
							break;
						k = 0;
						i = 0;
						while (cycles > 0 && i < 100) {
							if ((int8_t)pstate.player_craft->weapon_slots[k].charge > 0) {
								--pstate.player_craft->weapon_slots[k].charge;
								--cycles;
								if (!pstate.player_craft->is_player_craft) {
									pstate.player_craft->forward_shield += step;
								} else if (pstate.player_craft->is_player_craft == 2) {
									pstate.player_craft->rear_shield += step;
								} else {
									pstate.player_craft->forward_shield += step / 2;
									pstate.player_craft->rear_shield += step / 2;
								}
							}
							if (++k >= pstate.player_craft->weapon_group_cnt)
								k = 0;
							++i;
						}
						fsfx_triggersfx(0x21u, 0xFFFF);
						msg_messageprintf(MSG_XFER_CANNON_TO_SHIELDS);
					} else {
						argtable[0] = 35;
						argtable[1] = 25;
						msg_messageprintf(MSG_SYSTEM_STATUS);
					}
				} else {
					argtable[0] = 35;
					msg_messageprintf(MSG_NO_SUCH_SYSTEM);
				}
				break;
			}
			/* 'b': beam on/off. */
			case KEY_b: {
				if (pstate.player_craft->subsystem_active & 0x100) {
					if (pstate.player_craft->status_flags & 0x100) {
						pstate.player_craft->beam_state ^= 0x80u;
						argtable[0] = (uint16_t)(pstate.player_craft->beam_type + 192);
						if (pstate.player_craft->beam_state & 0x80)
							msg_messageprintf(MSG_BEAM_ON);
						else
							msg_messageprintf(MSG_BEAM_OFF);
					} else {
						argtable[0] = 32;
						argtable[1] = 25;
						msg_messageprintf(MSG_SYSTEM_STATUS);
					}
				} else {
					argtable[0] = 32;
					msg_messageprintf(MSG_NO_SUCH_SYSTEM);
				}
				break;
			}
			/* F8: beam-rate cycle. */
			case KEY_F8: {
				if (pstate.player_craft->subsystem_active & 0x100) {
					if (pstate.player_craft->status_flags & 0x100) {
						++pstate.player_craft->beam_power;
						if (pstate.player_craft->beam_power >= 5)
							pstate.player_craft->beam_power = 0;
						argtable[0] = (uint16_t)(pstate.player_craft->beam_power + 19);
						msg_messageprintf(MSG_BEAM_RATE);
						fsfx_triggersfx(0x21u, 0xFFFF);
					} else {
						argtable[0] = 32;
						argtable[1] = 25;
						msg_messageprintf(MSG_SYSTEM_STATUS);
					}
				} else {
					argtable[0] = 32;
					msg_messageprintf(MSG_NO_SUCH_SYSTEM);
				}
				break;
			}
			/* Alt+1: auto-target. */
			case KEY_ALT_1: {
				uint16_t a = user_picktarget();
				user_setnewtarget(a);
				break;
			}
			/* 't': next target. */
			case KEY_t:
				if (pstate.target_obj_idx != 0xFFFF)
					user_setnewtarget(user_picknexttarget(pstate.target_obj_idx, 1));
				else
					user_setnewtarget(user_picknexttarget((uint16_t)pstate.radar_target0, 1));
				break;
			/* 'y': previous target. */
			case KEY_y:
				if (pstate.target_obj_idx != 0xFFFF)
					user_setnewtarget(user_picknexttarget(pstate.target_obj_idx, -1));
				else
					user_setnewtarget(user_picknexttarget((uint16_t)pstate.radar_target0, -1));
				break;
			/* 'u': target newest craft in the area (QRC manual). */
			case KEY_u: {
				uint16_t best_obj = 0xFFFF;
				uint16_t best_tick = 0xFFFF;
				int16_t i;

				for (i = 0; i < NUM_ACTIVE_CRAFT_SLOTS; ++i) {
					CraftData* cp;
					uint8_t ff;
					uint16_t tick;

					if (!objects[i].ship_idx)
						continue;
					if (i == pstate.object_idx)
						continue;
					cp = objects[i].craft_ptr;
					if (cp->leader_obj_idx != 255)
						continue;
					/* Accept flight_flag in {0, 2, 6}; reject everything else. */
					ff = cp->flight_flag;
					if (ff && ff != 2 && ff != 6)
						continue;
					tick = (uint16_t)objects[i].age_ticks;
					if (best_tick > tick) {
						best_obj = (uint16_t)i;
						best_tick = tick;
					}
				}
				user_setnewtarget(best_obj);
				break;
			}
			/* 'r': find nearest enemy and target it. Also synthesized by
			 * user_userinterface on button-chord-4 release. */
			case KEY_r: {
				uint32_t best_dist = 0xFFFFFFFFu;
				uint16_t best_idx = 0xFFFF;

				/* Pass 1: craft slots 0..NUM_CRAFTS-1. */
				int16_t i;
				uint16_t k;
				int16_t m;

				for (i = 0; i < NUM_CRAFTS; ++i) {
					int g;
					CraftData* cp;
					uint8_t ff;

					if (!objects[i].ship_idx)
						continue;
					if (i == pstate.object_idx)
						continue;
					if (objects[i].side == objects[pstate.object_idx].side)
						continue;
					if (objects[i].side == 2 && mission_file_header.mission.neutral_name[0][0] != '1')
						continue;
					if (objects[i].side == 3 && mission_file_header.mission.neutral_name[1][0] != '1')
						continue;
					if (objects[i].side == 5 && mission_file_header.mission.neutral_name[3][0] != '1')
						continue;
					g = objects[i].genus;
					if (g == 4 || g == 3 || g == 5)
						continue;
					cp = objects[i].craft_ptr;
					if (!cp->status_flags)
						continue;
					ff = cp->flight_flag;
					if (ff && ff != 6)
						continue;

					pai_distancebetween(pstate.object_idx, i);
					if (best_dist > (uint32_t)trig2_polardistance) {
						best_idx = (uint16_t)i;
						best_dist = (uint32_t)trig2_polardistance;
					}
				}

				/* Pass 2: static mine-gun turrets (ship_class == 8). */
				k = 14336;
				for (m = 0; m < 64; ++k, ++m) {
					uint8_t fg_side;

					if (!staticobjects[m].species)
						continue;
					if (staticobjects[m].ship_class != 8)
						continue;
					if (!staticobjects[m].status_flags)
						continue;
					fg_side = fg_array[staticobjects[m].fg_idx].side;
					if (fg_side == objects[pstate.object_idx].side)
						continue;

					pai_distancebetween(pstate.object_idx, k);
					if (best_dist > (uint32_t)trig2_polardistance) {
						best_idx = k;
						best_dist = (uint32_t)trig2_polardistance;
					}
				}

				user_setnewtarget(best_idx);
				break;
			}
			/* 'e': target my closest attacker. */
			case KEY_e: {
				uint16_t a = user_findclosestattacker(pstate.object_idx);
				user_setnewtarget(a);
				break;
			}
			/* 'a': target closest attacker of current target. */
			case KEY_a: {
				uint16_t a = user_findclosestattacker(pstate.target_obj_idx);
				user_setnewtarget(a);
				break;
			}
			/* ',': next radar subtarget. */
			case KEY_COMMA: {
				CraftData* cp;
				uint16_t guard;

				if (pstate.target_obj_idx == 0xFFFF)
					break;
				if (pstate.target_obj_idx >= NUM_ACTIVE_CRAFT_SLOTS)
					break;
				cp = objects[pstate.target_obj_idx].craft_ptr;
				if (!TIE_FLIGHT_TIE98)
					draw_Lockshipfileptrs(objects[pstate.target_obj_idx].ship_idx);
				/* Try each mesh once at most. */
				for (guard = (uint16_t)(TIE_FLIGHT_EDITION(
											objectblockptr->num_meshes,
											modelmesh_getcount(objects[pstate.target_obj_idx].ship_idx)) -
										1);
					 guard != 0xFFFF; --guard) {
					if ((uint16_t)++pstate.radar_target1 >=
						TIE_FLIGHT_EDITION(objectblockptr->num_meshes,
										   modelmesh_getcount(objects[pstate.target_obj_idx].ship_idx)))
						pstate.radar_target1 = 0;
					if (cp->mesh_state[pstate.radar_target1] == MESH_STATE_VISIBLE &&
						TIE_FLIGHT_EDITION(user_validcomponent(pstate.radar_target1),
										   user_validcomponent_tie98(objects[pstate.target_obj_idx].ship_idx,
																	 pstate.radar_target1)))
						break;
				}
				break;
			}
			/* '<': previous radar subtarget. */
			case KEY_LESS: {
				CraftData* cp;
				uint16_t guard;

				if (pstate.target_obj_idx == 0xFFFF)
					break;
				if (pstate.target_obj_idx >= NUM_ACTIVE_CRAFT_SLOTS)
					break;
				cp = objects[pstate.target_obj_idx].craft_ptr;
				if (!TIE_FLIGHT_TIE98)
					draw_Lockshipfileptrs(objects[pstate.target_obj_idx].ship_idx);
				/* Try each mesh once at most. */
				for (guard = (uint16_t)(TIE_FLIGHT_EDITION(
											objectblockptr->num_meshes,
											modelmesh_getcount(objects[pstate.target_obj_idx].ship_idx)) -
										1);
					 guard != 0xFFFF; --guard) {
					if ((uint16_t)--pstate.radar_target1 == 0xFFFF)
						pstate.radar_target1 =
							(int16_t)(TIE_FLIGHT_EDITION(
										  objectblockptr->num_meshes,
										  modelmesh_getcount(objects[pstate.target_obj_idx].ship_idx)) -
									  1);
					if (cp->mesh_state[pstate.radar_target1] == MESH_STATE_VISIBLE &&
						TIE_FLIGHT_EDITION(user_validcomponent(pstate.radar_target1),
										   user_validcomponent_tie98(objects[pstate.target_obj_idx].ship_idx,
																	 pstate.radar_target1)))
						break;
				}
				break;
			}
			/* 'i': radar toggle. */
			case KEY_i: {
				pstate.radar_enable ^= 1u;
				argtable[0] = (uint16_t)(pstate.radar_enable + 86);
				msg_messageprintf(MSG_CMD_TRACK_TOGGLE);
				fsfx_triggersfx(0x21u, 0xFFFF);
				break;
			}
			/* 'q': end-mission prompt. */
			case KEY_q:
				if (!pstate.space_confirm_action) {
					msg_messageprintf(MSG_END_MISSION_PROMPT);
					pstate.space_confirm_action = 2;
					timers[TIMER_SPACE_CONFIRM] = 1888;
				}
				break;
			/* F5..F7: recall the saved target slots. */
			case KEY_F5:
			case KEY_F6:
			case KEY_F7: {
				uint16_t tgt;

				if (pstate.target_presets[(uint16_t)inputkey - KEY_F5] != 0xFFFF) {
					tgt = pstate.target_presets[(uint16_t)inputkey - KEY_F5];
					if (objects[tgt < 0x3800 ? tgt : tgt - 0x3800].ship_idx)
						user_setnewtarget(tgt);
				}
				break;
			}
			/* Shift+F5..F7: quicksave target recall slots. */
			case KEY_SHIFT_F5:
			case KEY_SHIFT_F6:
			case KEY_SHIFT_F7:
				if (pstate.target_obj_idx != 0xFFFF)
					pstate.target_presets[(uint16_t)inputkey - KEY_SHIFT_F5] = pstate.target_obj_idx;
				fsfx_triggersfx(0x21u, 0xFFFF);
				break;
			/* 'h': hyperspace. */
			case KEY_h: {
				int16_t interdictor;
				int16_t i;
				uint16_t obj;
				uint16_t pri_stop_obj;
				uint16_t sec_stop_obj;

				if (pstate.player_craft->subsystem_active & 0x80) {
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
					} else if (pstate.player_craft->status_flags & 0x80) {
						/* An enemy Interdictor in the area blocks the jump. */
						interdictor = 0;
						for (i = 0; i < NUM_ACTIVE_CRAFT_SLOTS; ++i) {
							if (objects[i].ship_idx == 51 && objects[i].side != pstate.player->side)
								interdictor = 1;
						}
						if (interdictor) {
							msg_messageprintf(MSG_INTERDICTOR_BLOCK);
						} else {
							fscript_MsSetSequence(17);
							msg_clearmessagequeue();
							msg_messageprintf(MSG_HYPER_PREP);
							camera.view_target_tracking = 0;
							hyperspaceflag = 1;
							hyperabortflag = 1;
							camera.view_zoom_flag = 0;
							camera.view_target_obj = pstate.object_idx;
							camera.view_camera_control = 0;
							panelrts_setnewpilotview(0);
							camera.side_angle = 0;
							camera.up_angle = 0;
							hyperticks = 0;
							fsfx_triggersfx(0x21u, 0xFFFF);
						}
					} else {
						argtable[1] = 25;
						argtable[0] = 36;
						msg_messageprintf(MSG_SYSTEM_STATUS);
					}
				} else {
					/* No hyperdrive: name the motherships the player's flight group
					 * departs to. */
					pri_stop_obj = 0xFFFF;
					sec_stop_obj = 0xFFFF;
					for (obj = 0; obj < NUM_ACTIVE_CRAFT_SLOTS; ++obj) {
						if (fg_array[pstate.player_fg_idx].pri_stop_fg_used && objects[obj].ship_idx &&
							objects[obj].fg_idx == fg_array[pstate.player_fg_idx].pri_stop_fg)
							pri_stop_obj = obj;
						if (fg_array[pstate.player_fg_idx].sec_stop_fg_used && objects[obj].ship_idx &&
							objects[obj].fg_idx == fg_array[pstate.player_fg_idx].sec_stop_fg)
							sec_stop_obj = obj;
					}
					if (pri_stop_obj != 0xFFFF && sec_stop_obj != 0xFFFF) {
						msg_createobjectname(pri_stop_obj, 0, tempstring);
						msg_addmessageptr(0, tempstring);
						msg_createobjectname(sec_stop_obj, 0, temp2string);
						msg_addmessageptr(1u, temp2string);
						msg_messageprintf(MSG_NO_HYPERDRIVE_RETURN_ALT);
					} else if (pri_stop_obj != 0xFFFF) {
						msg_createobjectname(pri_stop_obj, 0, tempstring);
						msg_addmessageptr(0, tempstring);
						msg_messageprintf(MSG_NO_HYPERDRIVE_RETURN);
					} else if (sec_stop_obj != 0xFFFF) {
						msg_createobjectname(sec_stop_obj, 0, tempstring);
						msg_addmessageptr(0, tempstring);
						msg_messageprintf(MSG_NO_HYPERDRIVE_RETURN);
					}
				}
				break;
			}
			/* Alt+D: detail level toggle. */
			case KEY_ALT_D: {
				uint8_t dl = (uint8_t)(detaillevel + 1);
				if (dl >= 4)
					dl = 0;
				detaillevel = dl;
				user_setdetaillevel(dl);
				argtable[0] = (uint16_t)(dl + 38);
				msg_messageprintf(MSG_GFX_DETAIL);
				fsfx_triggersfx(0x21u, 0xFFFF);
				break;
			}
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
				fsfx_triggersfx(0x21u, 0xFFFF);
				break;
			/* Alt+E: eject / surrender. */
			case KEY_ALT_E: {
				uint16_t species;
				uint16_t spin;

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
				} else if (pstate.player_craft->status_flags & 2) {
					if (!hyperspaceflag && !replayviewmode) {
						if (user_isrescued(pstate.object_idx)) {
							mission.player_status = 2;
							fediskio_updatepilotrecord(0, 1);
						} else {
							mission.player_status = 1;
							fediskio_updatepilotrecord(1, 1);
						}
						user_ejectcamera();
						/* Tumble at a random rate within the craft's spin limit. */
						species = pstate.player_craft->species_idx;
						spin = (uint16_t)((math2_getrandom() & 0x3FFF) + 0x2000);
						while (spin > (uint16_t)spec_data[species].max_spin_rate)
							spin >>= 1;
						pstate.player->spin_rate = (int16_t)spin;
						pstate.player_craft->flight_flag = 3;
						pstate.player->death_timer = (int16_t)(236 * ((math2_getrandom() & 3) + 3));
						TIE_FLIGHT_TRACE_DEATH(pstate.object_idx, 0xFFFFu, TIE_TRACE_DEATH_EJECTED,
											   pstate.player->death_timer);
					}
				} else {
					argtable[0] = 34;
					argtable[1] = 25;
					msg_messageprintf(MSG_SYSTEM_STATUS);
				}
				break;
			}
			/* Alt+M: music volume toggle. */
			case KEY_ALT_M:
				if (musicvolflag) {
					imuse_set_music_vol(im, 0);
#if defined(TIE_MODERN) || defined(TIE98)
#ifdef TIE_MODERN
					if (TieMusicPolicy_UsesTie98())
#endif
						gamesnd_Set_CD_Volume(0);
#endif
					musicvolflag = 0;
				} else {
					imuse_set_music_vol(im, inflight_music_vol ? inflight_music_vol * 8 - 1 : 0);
#if defined(TIE_MODERN) || defined(TIE98)
#ifdef TIE_MODERN
					if (TieMusicPolicy_UsesTie98())
#endif
						gamesnd_Set_CD_Volume(inflight_music_vol);
#endif
					musicvolflag = 1;
				}
				break;
			/* Alt+S: sound volume toggle. */
			case KEY_ALT_S:
				if (soundvolflag) {
					imuse_set_sfx_vol(im, 0);
					imuse_set_voice_vol(im, 0);
					soundvolflag = 0;
				} else {
					/* Volume scaling: 0..16 -> iMUSE group volume. */
					imuse_set_sfx_vol(im, inflight_sound_vol ? inflight_sound_vol * 8 - 1 : 0);
					imuse_set_voice_vol(im, inflight_speech_vol ? inflight_speech_vol * 8 - 1 : 0);
					soundvolflag = 1;
				}
				break;
			/* 'm': map info room. */
			case KEY_m:
#ifdef TIE_MODERN
				TieFlightRequest_InfoRoom(1);
#else
				screen = (uint16_t)user_inflightinfo(1);
#endif
				break;
			/* 'l': messages info room. */
			case KEY_l:
#ifdef TIE_MODERN
				TieFlightRequest_InfoRoom(2);
#else
				screen = (uint16_t)user_inflightinfo(2);
#endif
				break;
			/* 'g': goals info room. */
			case KEY_g:
#ifdef TIE_MODERN
				TieFlightRequest_InfoRoom(0);
#else
				screen = (uint16_t)user_inflightinfo(0);
#endif
				break;
			/* 'd': damage info room. */
			case KEY_d:
#ifdef TIE_MODERN
				TieFlightRequest_InfoRoom(3);
#else
				screen = (uint16_t)user_inflightinfo(3);
#endif
				break;
			/* 'Z': wingmen info room. */
			case KEY_Z:
#ifdef TIE_MODERN
				TieFlightRequest_InfoRoom(4);
#else
				screen = (uint16_t)user_inflightinfo(4);
#endif
				break;
			/* 'k': help info room. */
			case KEY_k:
#ifdef TIE_MODERN
				TieFlightRequest_InfoRoom(5);
#else
				screen = (uint16_t)user_inflightinfo(5);
#endif
				break;
			/* In-flight options room. */
			case KEY_ESCAPE:
#ifdef TIE_MODERN
				if (replayviewmode)
					TieFlightRequest_InfoRoom(6);
				else
					TieRuntime_RequestSettingsMenu();
#else
				screen = (uint16_t)user_inflightinfo(6);
#endif
				break;
			/* 'H': wingman 'attack starship'. */
			case KEY_H:
				if (user_checkradio()) {
					craftptr = objects[pstate.target_obj_idx].craft_ptr;
					if (craftptr->current_order != 47 && craftptr->current_order != 53) {
						craftptr->special_order_flag = 1;
						if (objects[pstate.target_obj_idx].genus == GENUS_STARSHIP)
							craftptr->current_order = 53;
						else
							craftptr->current_order = 47;
						pai_setupcraftaivars(pstate.target_obj_idx);
						pai_initplan(pstate.target_obj_idx);
					}
					msg_radiomessage(pstate.target_obj_idx, craftptr, 0x70u, 0);
				} else {
					msg_messageprintf(MSG_CRAFT_NOT_RESPONDING);
				}
				break;
			/* 'W': wait in place. */
			case KEY_W:
				if (user_checkradio()) {
					if (craftptr->current_order != 44 && craftptr->current_order != 51 &&
						craftptr->current_order != 52) {
						craftptr->saved_current_order = craftptr->current_order;
						if (objects[pstate.target_obj_idx].genus == GENUS_STARSHIP)
							craftptr->current_order = 64;
						else
							craftptr->current_order = 44;
						pai_setupcraftaivars(pstate.target_obj_idx);
						pai_initplan(pstate.target_obj_idx);
						msg_radiomessage(pstate.target_obj_idx, craftptr, 0x73u, 0);
					}
				} else {
					msg_messageprintf(MSG_CRAFT_NOT_RESPONDING);
				}
				break;
			/* 'G': wingman 'return to base'. */
			case KEY_G:
				if (user_checkradio()) {
					if (craftptr->current_order == 44) {
						craftptr->current_order = craftptr->saved_current_order;
						pai_setupcraftaivars(pstate.target_obj_idx);
						pai_initplan(pstate.target_obj_idx);
						msg_radiomessage(pstate.target_obj_idx, craftptr, 0x74u, 0);
					}
				} else {
					msg_messageprintf(MSG_CRAFT_NOT_RESPONDING);
				}
				break;
			/* 'E': wingman 'wait'. */
			case KEY_E:
				if (user_checkradio()) {
					if (craftptr->current_order == 44) {
						craftptr->current_order = craftptr->saved_current_order;
						pai_setupcraftaivars(pstate.target_obj_idx);
						pai_initplan(pstate.target_obj_idx);
					}
					craftptr->pending_radio_command = 251;
					msg_radiomessage(pstate.target_obj_idx, craftptr, 0x72u, 0);
				} else {
					msg_messageprintf(MSG_CRAFT_NOT_RESPONDING);
				}
				break;
			/* 'C': attack my attacker. */
			case KEY_C: {
				uint16_t a = user_findclosestattacker(pstate.object_idx);
				if (a != 0xFFFF)
					user_assigntarget(a, 0x71u);
				break;
			}
			/* 'A' (Shift+a): wingmen attack player's target. */
			case KEY_A:
				if (pstate.target_obj_idx != 0xFFFF) {
					if (pstate.target_obj_idx == (uint16_t)pstate.radio_target)
						pstate.radio_target = -1;
					user_assigntarget(pstate.target_obj_idx, 0x75u);
				}
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
				if (pstate.target_obj_idx != 0xFFFF && pstate.target_obj_idx < NUM_ACTIVE_CRAFT_SLOTS &&
					objects[pstate.target_obj_idx].side == 1) {
					craftptr = objects[pstate.target_obj_idx].craft_ptr;
					msg_reportmessage(pstate.target_obj_idx, craftptr,
									  convertmessage[craftptr->current_order]);
				}
				break;
			/* 'S': reinforce request. */
			case KEY_S: {
				uint8_t reinforce_avail;
				uint16_t i;

				if (pstate.space_confirm_action)
					break;
				reinforce_avail = 0;
				for (i = 0; i < mission_file_header.num_fg; ++i) {
					/* Arrival condition 20: "reinforced by". */
					if (fg_array[i].start_cond[0].cond == 20 || fg_array[i].start_cond[1].cond == 20)
						reinforce_avail = 1;
				}
				if (!reinforce_avail) {
					msg_messageprintf(MSG_NO_REINFORCEMENTS);
					fsfx_triggervoicesfx(0x66u);
					fsfx_triggervoicesfx(0x67u);
					fsfx_triggervoicesfx(0x69u);
					break;
				}
				if (!mission.penalty_flag) {
					msg_messageprintf(MSG_REINFORCE_PROMPT);
					timers[TIMER_SPACE_CONFIRM] = 1888;
					pstate.space_confirm_action = 3;
				} else {
					msg_messageprintf(MSG_REINFORCE_ALREADY_USED);
					fsfx_triggervoicesfx(0x66u);
					fsfx_triggervoicesfx(0x67u);
					fsfx_triggervoicesfx(0x69u);
				}
				break;
			}
			/* 'B': wingman 'go home'. */
			case KEY_B:
				if (pstate.target_obj_idx != 0xFFFF && pstate.target_obj_idx < NUM_ACTIVE_CRAFT_SLOTS) {
					craftptr = objects[pstate.target_obj_idx].craft_ptr;
					pai_setupcraftaivars(pstate.target_obj_idx);
					if (craftptr->current_order == 28 && pai_isobjectvalidtarget(pstate.object_idx)) {
						craftptr->pending_radio_command = pstate.object_idx;
						pstate.player_craft->throttle_speed = 0;
						msg_radiomessage(pstate.target_obj_idx, craftptr, 0xC6u, 0);
					}
				}
				break;
			/* Left/right arrow: roll. */
			case KEY_LEFT_ARROW:
			case KEY_RIGHT_ARROW: {
				uint16_t roll_pct = math2_percentage(pstate.player_craft->roll_rate_cache, 0x3000u);
				int16_t roll;

				roll_pct >>= 1;

				roll = (int16_t)math2_ABoverC32(
					(int16_t)((((uint16_t)inputkey == KEY_LEFT_ARROW ? 0xD000 : 0x3000) * roll_pct) >> 15),
					frameticks, 236);
				objects[pstate.object_idx].roll -= roll;
				break;
			}
		}
#ifdef TIE_MODERN
		/* PORT: info rooms never return here in the modern build, so the
		 * screen only repeats the loop for this frame's own key. */
		if (frame_key >= 0) {
			inputkey = (int16_t)frame_key;
			frame_key = -1;
			screen = 4;
		}
#endif
	} while (screen == 4);

#ifdef TIE_MODERN
	/* Orientation update at the end of the per-frame dispatch. */
	{
		int16_t x_input;
		int16_t y_input;
#ifdef TIE_MODERN
		int16_t roll_input;
#endif
		int x_roll_mode;
		TieUserTimingState* high_rate;
		int16_t x_per_tick;
		int16_t y_per_tick;
#ifdef TIE_MODERN
		int16_t roll_per_tick;
#endif

		if (camera.view_camera_control) {
			TieUserTimingState* high_rate = TieFlightTiming_IsHighRate() ? TieFlightTimingState_User() : NULL;
			int16_t up_delta = high_rate
								   ? TieUserTiming_ScaleValue(inputdeltax, &high_rate->view_remainder[0])
								   : (int16_t)math2_ABoverC32(inputdeltax, frameticks, 236);
			int16_t side_delta = high_rate
									 ? TieUserTiming_ScaleValue(inputdeltay, &high_rate->view_remainder[1])
									 : (int16_t)math2_ABoverC32(inputdeltay, frameticks, 236);
			int zoom_btn;

			camera.up_angle += up_delta;
			camera.side_angle = (int16_t)(camera.side_angle + side_delta);
			zoom_btn = inputbuttons & 0xF;
			if (zoom_btn == 1 || zoom_btn == 2) {
				int32_t delta;
				int32_t hiw;

				if (high_rate) {
					const uint32_t numerator = 32u * frameticks + high_rate->zoom_rate_remainder;
					camera.view_zoom_rate += (int16_t)(numerator / TieFlightTiming_CompatibilityTicks());
					high_rate->zoom_rate_remainder =
						(uint16_t)(numerator % TieFlightTiming_CompatibilityTicks());
				} else {
					camera.view_zoom_rate += 32;
				}
				if ((uint16_t)camera.view_zoom_rate > 0x400u)
					camera.view_zoom_rate = 1024;
				delta = high_rate
							? TieUserTiming_ScaleValue(camera.view_zoom_rate, &high_rate->zoom_remainder)
							: math2_ABoverC32(camera.view_zoom_rate, frameticks, 236);
				hiw = camera.view_zoom;
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
		} else {

			/* Watcom emits `xor eax,eax; mov ax,inputdeltax; imul eax,ebx; sar eax,15`
			 * for both axes — i.e. the inputdelta is unsigned-loaded to a 32-bit reg.
			 * For negative inputdelta the int32 result has bit-15 set, so the LOW 16
			 * bits, reinterpreted as int16, carry the correctly signed slew target.
			 * The binary's slew arithmetic at 0x5F886+ then operates only on the low
			 * 16 (sub bx,ax / test bx,bx / movsx edx,ax), discarding the poisoned
			 * upper half. Using the full int32 here would feed values up to 65533
			 * into a slew toward an int16 axis_*_accum, overshooting and wrapping
			 * every few frames — the "mouse-left banks right + flicker" symptom. */
			x_input = (int16_t)(((math2_percentage(pstate.player_craft->roll_rate_cache, 0x3000u) >> 1) *
								 (uint16_t)inputdeltax) >>
								15);
			y_input = (int16_t)(((math2_percentage(pstate.player_craft->pitch_rate_cache, 0x1000u) >> 1) *
								 (uint16_t)inputdeltay) >>
								15);
#ifdef TIE_MODERN
			/* PORT: analog roll input from the second-stick axis. Uses
			 * roll_rate_cache like the X-input modifier path so a fully-deflected
			 * stick produces the same per-tick rotation the held-button roll mode
			 * produces. */
			roll_input = (int16_t)(((math2_percentage(pstate.player_craft->roll_rate_cache, 0x3000u) >> 1) *
									(uint16_t)inputdeltaroll) >>
								   15);
#endif
			if ((pstate.player_craft->status_flags & 0x20) == 0) {
				x_input = 0;
				y_input = 0;
#ifdef TIE_MODERN
				roll_input = 0;
#endif
			}
			x_roll_mode = (inputbuttons & 0xE) == 2;

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

#ifdef TIE_MODERN
			/* PORT: the roll accumulator slews independently of the modifier-button
			 * latch, so the second stick responds whether or not the player is
			 * also in held-button X-roll mode. */
			pstate.axis_roll_accum = TieUserTiming_SlewAxis(pstate.axis_roll_accum, roll_input, 2);
#endif

			high_rate = TieFlightTiming_IsHighRate() ? TieFlightTimingState_User() : NULL;
			x_per_tick = high_rate ? TieUserTiming_ScaleValue(pstate.axis_x_accum,
															  &high_rate->flight_axis_remainder[0])
								   : (int16_t)math2_ABoverC32(pstate.axis_x_accum, frameticks, 236);
			y_per_tick = high_rate ? TieUserTiming_ScaleValue(pstate.axis_y_accum,
															  &high_rate->flight_axis_remainder[1])
								   : (int16_t)math2_ABoverC32(pstate.axis_y_accum, frameticks, 236);
#ifdef TIE_MODERN
			roll_per_tick = high_rate ? TieUserTiming_ScaleValue(pstate.axis_roll_accum,
																 &high_rate->flight_axis_remainder[2])
									  : (int16_t)math2_ABoverC32(pstate.axis_roll_accum, frameticks, 236);
#endif
			if ((pstate.player_craft->status_flags & 0x20) == 0) {
				x_per_tick = 0;
				y_per_tick = 0;
#ifdef TIE_MODERN
				roll_per_tick = 0;
#endif
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
							uint16_t cur;

							if (high_rate)
								decrement = (uint16_t)-TieUserTiming_ScaleCompatibilityIncrement(
									-256, &high_rate->throttle_remainder[0], &high_rate->throttle_sign[0]);
							cur = pstate.player_craft->throttle_speed;
							pstate.player_craft->throttle_speed = (uint16_t)(cur - decrement);
							if (cur < decrement)
								pstate.player_craft->throttle_speed = 0;
						}
					} else {
						uint16_t increment = 256;
						uint16_t cur;
						uint16_t nxt;

						if (high_rate)
							increment = (uint16_t)TieUserTiming_ScaleCompatibilityIncrement(
								256, &high_rate->throttle_remainder[0], &high_rate->throttle_sign[0]);
						cur = pstate.player_craft->throttle_speed;
						nxt = (uint16_t)(cur + increment);
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
					user_calcdeltapitch(y_per_tick, (int16_t)-x_per_tick, pstate.object_idx,
										pstate.player_craft);
					pstate.player->orient_dirty = 1;
					pstate.player->move_dirty = 1;
				}
				/* Auto-bank-into-turn. PORT: suppressed while the player supplies
				 * analog roll input, so the auto component does not fight the
				 * stick. */
#ifdef TIE_MODERN
				if (x_per_tick && !roll_per_tick)
#else
				if (x_per_tick)
#endif
					objects[pstate.object_idx].roll -= x_per_tick;
			}

#ifdef TIE_MODERN
			/* PORT: apply analog roll on top of either branch (same 2x gain as
			 * the held-button mode for parity). */
			if (roll_per_tick) {
				objects[pstate.object_idx].roll -= (int16_t)(2 * roll_per_tick);
				pstate.player->orient_dirty = 1;
				pstate.player->move_dirty = 1;
			}
#endif
		}
	}
	TieUserTiming_ApplyThrottleCommand();
#else
	if (!camera.view_camera_control) {
		int16_t x_input;
		int16_t y_input;
		int16_t delta;
		uint16_t step;
		uint16_t x_roll_mode;

		/* The axis scale is a 16-bit unsigned product: negative deltas load
		 * zero-extended, and only the low 16 bits of the result are used. */
		x_input =
			(int16_t)(((uint16_t)(math2_percentage(pstate.player_craft->roll_rate_cache, 0x3000u) >> 1) *
					   (uint16_t)inputdeltax) >>
					  15);
		y_input =
			(int16_t)(((uint16_t)(math2_percentage(pstate.player_craft->pitch_rate_cache, 0x1000u) >> 1) *
					   (uint16_t)inputdeltay) >>
					  15);
		if ((pstate.player_craft->status_flags & 0x20) == 0) {
			y_input = 0;
			x_input = 0;
		}
		x_roll_mode = 0;
		if (((uint16_t)inputbuttons & 0xE) == 2)
			x_roll_mode = 1;

		/* Slew each axis toward its input: snap within 8, otherwise step by
		 * the whole difference, or by 4 * difference / framerate. */
		if (pstate.prev_x_roll_mode == x_roll_mode) {
			delta = (int16_t)(x_input - pstate.axis_x_accum);
			if (delta) {
				step = delta;
				if (delta < 0)
					step = (int16_t)-delta;
				if ((int16_t)step < 8) {
					pstate.axis_x_accum += delta;
				} else {
					if (framerate > 4) {
						step /= framerate;
						if (!step)
							step = 1;
						step <<= 2;
					}
					if (delta < 0)
						pstate.axis_x_accum -= step;
					else
						pstate.axis_x_accum += step;
				}
			}
			delta = (int16_t)(y_input - pstate.axis_y_accum);
			if (delta) {
				step = delta;
				if (delta < 0)
					step = (int16_t)-delta;
				if ((int16_t)step < 8) {
					pstate.axis_y_accum += delta;
				} else {
					if (framerate > 4) {
						step /= framerate;
						if (!step)
							step = 1;
						step <<= 2;
					}
					if (delta < 0)
						pstate.axis_y_accum -= step;
					else
						pstate.axis_y_accum += step;
				}
			}
		} else {
			pstate.axis_x_accum = 0;
			pstate.axis_y_accum = 0;
		}
		pstate.prev_x_roll_mode = (int16_t)x_roll_mode;

		x_input = (int16_t)math2_ABoverC32(pstate.axis_x_accum, frameticks, 236);
		y_input = (int16_t)math2_ABoverC32(pstate.axis_y_accum, frameticks, 236);
		if ((pstate.player_craft->status_flags & 0x20) == 0) {
			y_input = 0;
			x_input = 0;
		}

		if (x_roll_mode) {
			if (x_input) {
				objects[pstate.object_idx].roll -= (int16_t)(2 * x_input);
				pstate.player->move_dirty = pstate.player->orient_dirty = 1;
			}
			/* In roll mode the Y axis nudges the throttle. */
			if (inputdeltay) {
				if ((uint16_t)inputdeltay >= 0x8000 && (uint16_t)inputdeltay <= 0xE000) {
					uint16_t cur = pstate.player_craft->throttle_speed;
					pstate.player_craft->throttle_speed = (uint16_t)(cur + 0x100);
					if (cur > pstate.player_craft->throttle_speed)
						pstate.player_craft->throttle_speed = 0xFFFF;
				} else if ((uint16_t)inputdeltay <= 0x8000 && (uint16_t)inputdeltay >= 0x2000) {
					uint16_t cur = pstate.player_craft->throttle_speed;
					pstate.player_craft->throttle_speed = (uint16_t)(cur - 0x100);
					if (cur < pstate.player_craft->throttle_speed)
						pstate.player_craft->throttle_speed = 0;
				}
			}
		} else {
			if (y_input || x_input) {
				user_calcdeltapitch(y_input, (int16_t)-x_input, pstate.object_idx, pstate.player_craft);
				pstate.player->move_dirty = pstate.player->orient_dirty = 1;
			}
			/* Bank into the turn. */
			if (x_input)
				objects[pstate.object_idx].roll -= x_input;
		}
	} else {
		int16_t up_delta = (int16_t)math2_ABoverC32(inputdeltax, frameticks, 236);
		int16_t side_delta = (int16_t)math2_ABoverC32(inputdeltay, frameticks, 236);
		uint16_t zoom_btn;

		camera.up_angle += up_delta;
		camera.side_angle += side_delta;
		zoom_btn = inputbuttons & 0xF;
		if (zoom_btn == 1 || zoom_btn == 2) {
			camera.view_zoom_rate += 32;
			if ((uint16_t)camera.view_zoom_rate > 0x400)
				camera.view_zoom_rate = 0x400;
			if (zoom_btn == 1) {
				camera.view_zoom -= (int16_t)math2_ABoverC32(camera.view_zoom_rate, frameticks, 236);
				if (camera.view_zoom < 48)
					camera.view_zoom = 48;
			} else {
				camera.view_zoom += (int16_t)math2_ABoverC32(camera.view_zoom_rate, frameticks, 236);
				if (camera.view_zoom > 5120)
					camera.view_zoom = 5120;
			}
		} else {
			camera.view_zoom_rate = 32;
		}
	}
#endif
}
