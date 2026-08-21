#include <stdint.h>
#include <stdlib.h>

#include "tie/create.h"
#include "tie/draw.h"
#include "tie/fsfx.h"
#include "tie/laser.h"
#include "tie/math2.h"
#include "tie/mission.h"
#include "tie/modelbounds.h"
#include "tie/modelmesh.h"
#include "tie/msg.h"
#include "tie/msg_templates.h"
#include "tie/pai.h"
#include "tie/paiman.h"
#include "tie/paiorder.h"
#include "tie/panel.h"
#include "tie/score.h"
#include "tie/spec.h"
#include "tie/tie.h"
#include "tie/trig2.h"
#include "tie_runtime/timing/ai_lead.h"

/* ---- External globals referenced by PAIMAN ------------------------- */

/* ai (AiContext) is declared in pai.h — 52-byte PAI tick-state block.
 * Board/dropoff snapshot-restore uses plain struct-assignment because
 * ai IS the contiguous 52-byte region (matches the binary's qmemcpy). */

/* ---- Data tables ponted at by the binary --------------------------- */

/* 13 formations × 6 slots each = 78 unit offsets per axis.
 * formposz has a 79th trailing zero preserved for byte-exact layout. */
const int16_t _formposx[78] = {
	0, 1, -1, 2, -2, 3,  0,  -2, 4,  5,  -4, -5, 0,  0,  0, 0,  0, 0, 0, 1,  -1, 2, -2, 3, 0, 1,
	2, 3, 4,  5, 0,  -1, -2, -3, -4, -5, 0,  -1, 0,  -1, 0, -1, 0, 1, 0, -1, 0,  0, 0,  0, 0, 0,
	0, 0, 0,  1, -1, 1,  -1, 0,  0,  1,  -1, 2,  -2, 3,  0, 0,  0, 0, 0, 0,  0,  0, 0,  0, 0, 0,
};

const int16_t _formposy[78] = {
	0,  -1, -1, -2, -2, -3, 0,  -2, -4, -5, -4, -5, 0,  -1, -2, -3, -4, -5, 0,  0,  0,  0,  0, 0, 0, -1,
	-2, -3, -4, -5, 0,  -1, -2, -3, -4, -5, 0,  0,  -1, -1, -2, -2, 0,  -1, -2, -1, -1, -1, 0, 0, 0, 0,
	0,  0,  0,  0,  0,  0,  0,  -1, 0,  1,  1,  2,  2,  3,  0,  -1, -1, -2, -2, -3, 0,  1,  1, 2, 2, 3,
};

const int16_t _formposz[79] = {
	0, 0, 0, 0,  0,  0, 0, -1, 2, 1, 3, 2, 0, 1, 2,  3, 4,  5, 0, 0, 0,  0, 0,  0, 0, 0, 0,
	0, 0, 0, 0,  0,  0, 0, 0,  0, 0, 0, 0, 0, 0, 0,  0, 0,  0, 0, 1, -1, 0, 1,  2, 3, 4, 5,
	0, 1, 1, -1, -1, 0, 0, 0,  0, 0, 0, 0, 0, 1, -1, 2, -2, 3, 0, 1, -1, 2, -2, 3, 0,
};

const int16_t _escortsidepos[27] = {
	-3072, 0,     3072, -3072, 0,     3072, -3072, 0,     3072, -3072, 0,     3072, -3072, 0,
	3072,  -3072, 0,    3072,  -3072, 0,    3072,  -3072, 0,    3072,  -3072, 0,    3072,
};

const int16_t _escortuppos[27] = {
	3072, 3072, 3072, 3072, 3072,  3072,  3072,  3072,  3072,  0,     0,     0,     0,     0,
	0,    0,    0,    0,    -3072, -3072, -3072, -3072, -3072, -3072, -3072, -3072, -3072,
};

const int16_t _escortfwdpos[27] = {
	3072, 3072,  3072,  0,     0,    0,    -3072, -3072, -3072, 3072, 3072,  3072,  0,     0,
	0,    -3072, -3072, -3072, 3072, 3072, 3072,  0,     0,     0,    -3072, -3072, -3072,
};

/* Maps EAIStruct.speed (0..11) to a 16-bit throttle_speed. The final
 * entry 0xF400 is an overspeed/slam sentinel reachable from afterburner
 * code; normal orders use entries 0..10. */
const uint16_t _throttleconvert[12] = {
	0x0000, 0x1999, 0x3334, 0x4CCE, 0x6668, 0x8000, 0x999A, 0xB334, 0xCCCE, 0xE668, 0xFFFF, 0xF400,
};

/* Stage-velocity ladder for outofhyperspacemaneuver: mode_subbyte 0..10
 * picks deceleration steps 3600 → 900 → 900 → 132. */
const uint16_t _stagevel[11] = {
	0x0E10, 0x0E10, 0x0E10, 0x0E10, 0x0E10, 0x0E10, 0x0708, 0x0708, 0x0708, 0x0384, 0x0384,
};

/* Per-skill hold time (pre-236x scaling) between turn-inside/turn-away
 * re-picks. Skill tiers: 0=novice, 1=veteran, 2=ace. */
const uint16_t _delayturninside[3] = { 9, 6, 3 };

/* Currently-dispatched pointers (mirrors 0xE3B4C / 0xE3B50 in the binary). */
ManeuverFunc _initmanvrfunctionptr = 0;
ManeuverFunc _manvrfunctionptr = 0;

/* ---- Small utility ------------------------------------------------- */

/* Compute |delta| in 0x8000-wrapping modular space (heading/pitch). */
static inline uint16_t abs_angle_delta(uint16_t a, uint16_t b) {
	uint16_t d = (uint16_t)(a - b);
	if (d >= 0x8000u)
		d = (uint16_t)-d;
	return d;
}

/* Pick heading direction for the short-way rotation (1=descending,
 * 2=ascending). The binary computes this identically everywhere:
 *   state = (target > current) + 1. */
static inline uint8_t pick_heading_state(uint16_t target, uint16_t current) {
	return (uint8_t)((target > current) ? 2u : 1u);
}

/* ---- Helpers ------------------------------------------------------- */

/* Snap or drive craftptr->ai_pitch toward ai_target_pitch. Threshold
 * 0x300 (~4.2°) distinguishes "close enough, snap now" from "rotate
 * over time". */
// FUNCTION: TIE 0x3D3B4
void paiman_setturn(int16_t pitch_step) {
	CraftData* cd = craftptr;
	uint16_t delta = abs_angle_delta((uint16_t)objects[ai.active_obj_idx].pitch, cd->ai_target_pitch);

	if (delta > 0x300u) {
		cd->ai_pitch_state = 2;
		cd->ai_pitch_step = (uint16_t)pitch_step;
	} else {
		objects[ai.active_obj_idx].pitch = (int16_t)cd->ai_target_pitch;
		objects[ai.active_obj_idx].orient_dirty = 1;
		objects[ai.active_obj_idx].move_dirty = 1;
		cd->ai_pitch_state = 3;
	}
}

/* Write the 16-bit throttle value passed in DX by the binary ABI. */
// FUNCTION: TIE 0x3D45C
void paiman_setpower(uint16_t throttle) { craftptr->throttle_speed = throttle; }

/* Convert absolute desired_speed to throttle_speed, accounting for the
 * shield/beam/laser power-balance margin and the craft's max_speed. */
// FUNCTION: TIE 0x3D46C
void paiman_setspeed(uint16_t obj_idx_param, uint16_t desired_speed) {
	CraftData* cd = objects[obj_idx_param].craft_ptr;
	int16_t margin = 6 - (int16_t)(cd->shield_power + cd->beam_power + cd->laser_power);
	uint16_t adj_max;

	if (margin >= 0) {
		adj_max = (uint16_t)(cd->max_speed_cache +
							 math2_fraction((uint16_t)(margin << 13), (uint16_t)cd->max_speed_cache));
	} else {
		uint16_t neg = (uint16_t)((-margin) << 13);
		adj_max = (uint16_t)(cd->max_speed_cache - math2_fraction(neg, (uint16_t)cd->max_speed_cache));
	}

	if (desired_speed < adj_max)
		craftptr->throttle_speed = math2_percentage(desired_speed, adj_max);
	else
		craftptr->throttle_speed = 0xFFFFu;
}

/* Point the AI craft's flight vector at craftptr->waypoint_*_cache.
 * Uses the current PAI skill_value to pick turn rate. */
// FUNCTION: TIE 0x3CD04
void paiman_setflighttotarget(int16_t pitch_bias, int16_t drive_heading) {
	CraftData* cd = craftptr;

	trig2_ctop(cd->waypoint_x_cache - objects[ai.active_obj_idx].world_x,
			   cd->waypoint_y_cache - objects[ai.active_obj_idx].world_y,
			   cd->waypoint_z_cache - objects[ai.active_obj_idx].world_z);

	cd->ai_target_pitch = (uint16_t)(trig2_xyangle + pitch_bias);
	paiman_setturn((int16_t)(((int32_t)cd->skill_value >> 1) + 0x4000));

	if (drive_heading) {
		cd->ai_heading_step = 0xFFFFu;
		cd->ai_heading_force = 0;
		cd->ai_target_heading = (uint16_t)trig2_zangle;
		cd->ai_climb_state = 0;
		cd->ai_dive_state = 0;
		cd->ai_heading_state = pick_heading_state(cd->ai_target_heading, cd->orient_heading);
	}
}

/* Level-the-wings helper: roll to 0, freeze pitch, clear climb/dive,
 * heading → 0x4000 with short-way direction. */
// FUNCTION: TIE 0x3CDC4
void paiman_controlplane(void) {
	CraftData* cd = craftptr;

	cd->ai_dive_state = 0;
	cd->ai_climb_state = 0;
	cd->ai_target_heading = 0x4000;
	cd->ai_heading_step = 0xFFFFu;
	cd->ai_heading_force = 0;

	if (cd->orient_heading < 0x4000u)
		cd->ai_heading_state = 2;
	else if (cd->orient_heading > 0x4000u)
		cd->ai_heading_state = 1;
	else
		cd->ai_heading_state = 3;

	cd->ai_roll_state = 1;
	cd->ai_roll_step = 0xFFFFu;
	cd->ai_target_roll = 0;
	cd->ai_pitch_state = 0;
}

/* Pick a new turn-inside target orientation and hold-timer. */
// FUNCTION: TIE 0x39740
uint16_t paiman_setnewturninside(uint16_t own_obj_idx) {
	CraftData* cd = craftptr;
	uint16_t ref_idx = (cd->attacker_idx == 0xFFu) ? own_obj_idx : cd->attacker_idx;
	uint16_t away_pitch = (uint16_t)objects[ref_idx].pitch;
	uint16_t hold_ticks;

	away_pitch = (uint16_t)(away_pitch + 0x8000u); /* flip 180° */
	cd->ai_target_pitch = away_pitch;

	paiman_setturn((int16_t)(((int32_t)cd->skill_value >> 1) + 0x8000));

	hold_ticks = (uint16_t)(236u * _delayturninside[ai.skill_tier]);
	cd->ai_plan_state = hold_ticks;
	return hold_ticks;
}

/* Pick a new turn-away target orientation: face the attacker's own pitch
 * (not flipped), or flip our own pitch if there is no attacker. */
// FUNCTION: TIE 0x3C8C0
uint16_t paiman_setnewturnaway(uint16_t own_obj_idx) {
	CraftData* cd = craftptr;
	uint16_t new_target_pitch;
	uint16_t hold_ticks;

	if (cd->attacker_idx == 0xFFu) {
		new_target_pitch = (uint16_t)(objects[own_obj_idx].pitch + 0x8000u);
	} else {
		new_target_pitch = (uint16_t)objects[cd->attacker_idx].pitch;
	}
	cd->ai_target_pitch = new_target_pitch;

	paiman_setturn((int16_t)(((int32_t)cd->skill_value >> 1) + 0x8000));

	hold_ticks = (uint16_t)(236u * _delayturninside[ai.skill_tier]);
	cd->ai_plan_state = hold_ticks;
	return hold_ticks;
}

/* Random z-axis jink used by speedaway. */
// FUNCTION: TIE 0x3A8D8
void paiman_setjink(uint16_t self_idx) {
	CraftData* cd = craftptr;
	int32_t push_z = (int32_t)(math2_getrandom() & 0x1F) + 50;
	int32_t pitch_off = (int32_t)((uint8_t)math2_getrandom()) + 384;

	/* Flip sign if the previous push was non-negative — alternates dir. */
	if (cd->push_accum_z >= 0) {
		push_z = -push_z;
		pitch_off = -pitch_off;
	}

	/* Retail zero-extends the 16-bit value before storing into the
	 * 32-bit push_accum_z (xor eax,eax / mov ax,dx). For negative
	 * push_z (the "previous push was non-negative" branch) this
	 * produces a positive int32 around 65455..65486; apply_push_accum
	 * reads the field as int32 so the sign matters. */
	cd->push_accum_z = (int32_t)(uint16_t)push_z;
	cd->ai_target_pitch = (uint16_t)(objects[self_idx].pitch + pitch_off);
	paiman_setturn((int16_t)(((int32_t)cd->skill_value >> 1) + 0x8000));
	cd->ai_plan_state = 118;
}

/* Advance to the next waypoint in a cruise/patrol cycle. */
// FUNCTION: TIE 0x39D74
void paiman_gonextwaypoint(void) {
	CraftData* cd = craftptr;
	uint8_t next_idx = (uint8_t)(cd->active_waypoint_idx + 1);
	uint8_t order_ldr = cd->default_order_ldr;

	cd->active_waypoint_idx = next_idx;
	if (next_idx > 11 || !fg_array[ai.fg_idx].way_used[next_idx]) {
		cd->active_waypoint_idx = 4;
		if (order_ldr == 3 || order_ldr == 5 || order_ldr == 56)
			++cd->ai_goal_progress[ai.ai_entry_count];
	}
	cd->ai_target_ref = (int16_t)(((uint16_t)cd->active_waypoint_idx) | 0x8000u);
	pai_settarget();
}

/* Compute aim-lead waypoint for target tgt_obj_idx. */
// FUNCTION: TIE 0x3CF50
void paiman_calcplanelead(uint16_t tgt_obj_idx) {
	CraftData* cd = craftptr;
	uint32_t lead_ticks = 0;
	uint16_t tgt_speed = (uint16_t)objects[tgt_obj_idx].current_speed;
	int32_t dx, dy, dz;

	if (tgt_speed) {
		uint16_t laser_id;
		int16_t closing_speed;
		uint16_t tgt_speed_cap;
		int32_t pitch_delta;
		uint16_t rel_closing;
		uint16_t time_scale;

		pai_distancebetween(ai.active_obj_idx, (uint16_t)cd->ai_target_ref);

		/* Pick projectile speed bucket: missile-lock (order 19) always
		 * uses bucket 141; otherwise clamp laser_type[0] to a minimum
		 * of 137 (lightest laser). */
		if (cd->default_order_ldr == 19) {
			laser_id = 141;
		} else if (cd->laser_type[0] <= 0x89u) {
			laser_id = 137;
		} else {
			laser_id = cd->laser_type[0];
		}

		closing_speed = (int16_t)((int16_t)projectilevelocity[laser_species_idx(laser_id)] +
								  objects[ai.active_obj_idx].current_speed);

		/* Scale by cos(pitch_delta) between our pitch and target's. */
		pitch_delta = (int32_t)abs_angle_delta((uint16_t)objects[tgt_obj_idx].pitch,
											   (uint16_t)objects[ai.active_obj_idx].pitch);

		tgt_speed_cap = (tgt_speed >= 0x384u) ? 900u : tgt_speed;

		if ((uint16_t)pitch_delta >= 0x4000u)
			rel_closing = (uint16_t)(trig2_cosinewordmult((int16_t)tgt_speed_cap, (uint16_t)pitch_delta) +
									 closing_speed);
		else
			rel_closing = (uint16_t)(closing_speed -
									 trig2_cosinewordmult((int16_t)tgt_speed_cap, (uint16_t)pitch_delta));

		/* 18 * rel_closing + rel_closing/5 — Watcom idiom for "× 18.2". */
		time_scale = (uint16_t)(18u * rel_closing + rel_closing / 5u);
		if (!time_scale)
			time_scale = 19;

		lead_ticks =
			math2_fraction((uint16_t)(framerate * (trig2_polardistance / time_scale)), cd->skill_value);
	}

	dx = objects[tgt_obj_idx].world_x - objects[tgt_obj_idx].world_x_prev;
	dy = objects[tgt_obj_idx].world_y - objects[tgt_obj_idx].world_y_prev;
	dz = objects[tgt_obj_idx].world_z - objects[tgt_obj_idx].world_z_prev;
	(void)TieAiLead_GetDisplacement(tgt_obj_idx, &dx, &dy, &dz);

	cd->waypoint_x_cache = objects[tgt_obj_idx].world_x + (int32_t)lead_ticks * dx;
	cd->waypoint_y_cache = objects[tgt_obj_idx].world_y + (int32_t)lead_ticks * dy;
	cd->waypoint_z_cache = objects[tgt_obj_idx].world_z + (int32_t)lead_ticks * dz;
}

// FUNCTION: TIE 0x3D164
void paiman_calcformation(void) {
	CraftData* cd = craftptr;
	uint8_t species_idx = cd->species_idx;
	int16_t bound_w = spec_data[species_idx].bound_width;
	int16_t bound_h = spec_data[species_idx].bound_height;
	int16_t bound_d = spec_data[species_idx].bound_depth;
	uint8_t craft_idx = cd->craft_idx_in_fg;
	uint8_t sep_units = (uint8_t)(ai.leader_craft->formation_separation + 1);
	int16_t form_idx = (int16_t)(6 * cd->formation + craft_idx);
	int32_t off_x = (int32_t)sep_units * _formposx[form_idx] * bound_w;
	int32_t off_y = (int32_t)sep_units * _formposy[form_idx] * bound_d;
	int32_t off_z = (int32_t)sep_units * _formposz[form_idx] * bound_h;

	/* Watcom unaligned-dword load: `*(int*)&unk_D534C[12*F + 2*C] >> 16`
	 * reads the same element as formpos{x,z,y}[6*F + C], so the "half
	 * bound" branch doubles as a direct re-read. Resolve to the direct
	 * access here. */
	if (sep_units == 1) {
		off_x += (int32_t)_formposx[form_idx] * (bound_w / 2);
		off_z += (int32_t)_formposz[form_idx] * (bound_h / 2);
		off_y += (int32_t)_formposy[form_idx] * (bound_d / 2);
	}

	pai_calcrotatedpoint(&objects[ai.leader_obj_idx], (int16_t)off_x, (int16_t)off_z, (int16_t)off_y);

	/* Scale the rotated offset for capital-ship leaders (LOD shift).
	 * Binary emits `shl reg, cl` (sign-agnostic); shifting a negative
	 * int32_t in C is UB, so route through uint32_t. */
	if (spec_data[species_idx].model_scale_shift) {
		const int s = spec_data[species_idx].model_scale_shift;
		rotatedx = (int32_t)((uint32_t)rotatedx << s);
		rotatedy = (int32_t)((uint32_t)rotatedy << s);
		rotatedz = (int32_t)((uint32_t)rotatedz << s);
	}

	cd->push_accum_x = rotatedx + objects[ai.leader_obj_idx].world_x - objects[ai.active_obj_idx].world_x;
	cd->push_accum_y = rotatedy + objects[ai.leader_obj_idx].world_y - objects[ai.active_obj_idx].world_y;
	cd->push_accum_z = rotatedz + objects[ai.leader_obj_idx].world_z - objects[ai.active_obj_idx].world_z;
}

/* Core attack-target helper. Picks static-vs-live waypoint resolution,
 * computes angle, requests a 180°-class turn (with inverted-upright
 * special case), drives heading. */
void paiman_attacktarget(int16_t pitch_bias) {
	CraftData* cd = craftptr;
	uint16_t target_ref = (uint16_t)cd->ai_target_ref;
	uint16_t self_idx = ai.active_obj_idx;

	if (cd->mode_byte == MODE_AttackSecondary || target_ref >= 0x3800u)
		pai_settarget();
	else
		paiman_calcplanelead(target_ref);

	trig2_ctop(cd->waypoint_x_cache - objects[self_idx].world_x,
			   cd->waypoint_y_cache - objects[self_idx].world_y,
			   cd->waypoint_z_cache - objects[self_idx].world_z);
	cd->ai_target_pitch = (uint16_t)(trig2_xyangle + pitch_bias);

	{
		bool want_turn = (cd->ai_roll_state != 2);
		if (!want_turn) {
			uint16_t pitch_delta = abs_angle_delta((uint16_t)objects[self_idx].pitch, cd->ai_target_pitch);
			want_turn = (pitch_delta >= 0x2000u || trig2_polardistance < 0x10000);
		}
		if (want_turn)
			paiman_setturn((int16_t)(((int32_t)cd->skill_value >> 1) + 0x8000));
	}

	if (cd->ai_roll_state == 3)
		cd->ai_roll_state = 0;

	if ((uint16_t)trig2_zangle != cd->orient_heading) {
		cd->ai_heading_step = 0xFFFFu;
		cd->throttle_speed = 0xFFFFu;
		cd->ai_climb_state = 0;
		cd->ai_target_heading = (uint16_t)trig2_zangle;
		cd->ai_dive_state = 0;
		cd->ai_heading_force = 0;
		cd->ai_heading_state = pick_heading_state(cd->ai_target_heading, cd->orient_heading);
	}
}

/* ---- MODE_None (slot 0) — nullmaneuver stub ------------------------ */

// FUNCTION: TIE 0x396F8
int16_t paiman_nullmaneuver(void) { return 0; }

/* ---- MODE_TurnInside (1) ------------------------------------------- */

void paiman_initturninsidemaneuver(void) {
	paiman_setnewturninside(ai.active_obj_idx);
	craftptr->maneuver_timer = 3540;
}

// FUNCTION: TIE 0x39718
int16_t paiman_turninsidemaneuver(void) {
	if (!craftptr->maneuver_timer)
		return 1;
	if (!craftptr->ai_plan_state)
		paiman_setnewturninside(ai.active_obj_idx);
	return 0;
}

/* ---- MODE_Splits (2) ----------------------------------------------- */

// FUNCTION: TIE 0x397C0
void paiman_initsplitsmaneuver(void) {
	CraftData* cd = craftptr;
	cd->ai_roll_state = 1;
	cd->ai_roll_step = 0xFFFFu;
	cd->ai_target_roll = 0x8000;
	cd->ai_pitch_state = 0;
	cd->ai_heading_force = 1;
	cd->ai_target_heading = 0x4000;
	cd->ai_heading_state = 2;
	cd->ai_heading_step = 0xFFFFu;
}

// FUNCTION: TIE 0x397F8
int16_t paiman_splitsmaneuver(void) {
	return (craftptr->ai_roll_state == 4 && craftptr->ai_heading_state == 3) ? 1 : 0;
}

/* ---- MODE_Immelmann (3) -------------------------------------------- */

// FUNCTION: TIE 0x39820
void paiman_initimmelmannmaneuver(void) {
	CraftData* cd = craftptr;
	uint16_t cur_heading;

	cd->throttle_speed = 0xFFFFu;
	cd->ai_roll_state = 1;
	cd->ai_roll_step = 0xFFFFu;
	cd->ai_target_roll = 0;
	cd->ai_pitch_state = 0;
	cd->ai_target_heading = 0x4000;
	cd->ai_heading_step = 0xFFFFu;
	cur_heading = cd->orient_heading;
	cd->ai_heading_force = 0;

	if (cur_heading < 0x4000u)
		cd->ai_heading_state = 2;
	else if (cur_heading > 0x4000u)
		cd->ai_heading_state = 1;
	else
		cd->ai_heading_state = 3;
	cd->maneuver_timer = 0;
}

// FUNCTION: TIE 0x39898
int16_t paiman_immelmannmaneuver(void) {
	CraftData* cd = craftptr;
	uint8_t phase = cd->mode_subbyte;

	if (phase == 0) {
		if (cd->ai_heading_state == 3) {
			cd->ai_heading_state = 1;
			cd->ai_heading_step = 0xFFFFu;
			cd->ai_heading_force = 1;
			cd->ai_target_heading = 0x4000;
			cd->mode_subbyte = 1;
		}
		return 0;
	}
	if (phase == 1) {
		if (cd->ai_heading_state == 3) {
			cd->ai_roll_state = 1;
			cd->ai_roll_step = 0xFFFFu;
			cd->ai_target_roll = 0;
			cd->ai_pitch_state = 0;
			cd->mode_subbyte = 2;
		}
		return 0;
	}
	if (phase == 2) {
		if (cd->ai_roll_state == 4 && cd->ai_heading_state == 3)
			return 1;
	}
	return 0;
}

/* ---- MODE_Scissors (4) --------------------------------------------- */

// FUNCTION: TIE 0x39930
void paiman_initscissorsmaneuver(void) {
	CraftData* cd = craftptr;
	uint16_t ref_idx = (cd->attacker_idx == 0xFFu) ? ai.active_obj_idx : cd->attacker_idx;
	uint16_t away_pitch = (uint16_t)(objects[ref_idx].pitch + 0x8000u);
	uint16_t rnd_roll;

	cd->ai_target_pitch = away_pitch;
	paiman_setturn((int16_t)(((int32_t)cd->skill_value >> 1) + 0x8000));

	cd->ai_roll_state = 3;
	cd->ai_roll_step = 0xFFFFu;
	rnd_roll = (uint16_t)math2_getrandom();
	cd->maneuver_timer = 4720;
	cd->ai_plan_state = 472;
	cd->ai_target_roll = rnd_roll;
}

// FUNCTION: TIE 0x399C4
int16_t paiman_scissorsmaneuver(void) {
	CraftData* cd = craftptr;

	if (!cd->maneuver_timer) {
		cd->ai_roll_state = 4;
		return 1;
	}
	if (!cd->ai_plan_state) {
		uint16_t flip_pitch = cd->ai_target_pitch;
		uint16_t tgt_roll_hi;

		cd->ai_pitch_state = 1;
		flip_pitch = (uint16_t)(flip_pitch + 0x8000u);
		cd->ai_target_pitch = flip_pitch;

		tgt_roll_hi = (uint16_t)(((cd->ai_target_roll >> 8) ^ 0x80u) << 8);
		cd->ai_target_roll = (uint16_t)((cd->ai_target_roll & 0x00FFu) | tgt_roll_hi);
		cd->ai_plan_state = 944;
	}
	return 0;
}

/* ---- MODE_Rendezvous (5) ------------------------------------------- */

// FUNCTION: TIE 0x39A14
void paiman_initrendezvousmaneuver(void) {
	paiman_setflighttotarget(0, 1);
	uint16_t t = _throttleconvert[fg_array[ai.fg_idx].ai[ai.ai_entry_count].speed];
	if (!t)
		t = 0xFFFF; /* retail: 0 -> full throttle, never stopped */
	craftptr->throttle_speed = t;
}

// FUNCTION: TIE 0x39A7C
int16_t paiman_rendezvousmaneuver(void) {
	paiman_setflighttotarget(0, 1);
	uint16_t t = _throttleconvert[fg_array[ai.fg_idx].ai[ai.ai_entry_count].speed];
	if (!t)
		t = 0xFFFF;
	craftptr->throttle_speed = t;
	return 0;
}

/* ---- MODE_Cruise (6) ----------------------------------------------- */

// FUNCTION: TIE 0x39AE4
void paiman_initcruisemaneuver(void) {
	CraftData* cd = craftptr;

	paiman_controlplane();
	if ((uint16_t)objects[ai.active_obj_idx].roll < 0x8000u)
		paiman_setflighttotarget(0, 1);

	cd->ai_plan_state = 236;
	cd->throttle_speed = _throttleconvert[fg_array[ai.fg_idx].ai[ai.ai_entry_count].speed];
}

// FUNCTION: TIE 0x39B78
int16_t paiman_cruisemaneuver(void) {
	CraftData* cd = craftptr;
	int32_t way_radius;
	uint8_t speed;

	pai_targetdistance();
	way_radius = (objects[ai.active_obj_idx].genus == GENUS_STARSHIP) ? 0x2000 : 4096;
	if (way_radius > trig2_polardistance)
		paiman_gonextwaypoint();

	if (!cd->ai_plan_state) {
		if (cd->ai_dive_state != 1 && cd->ai_climb_state != 1) {
			int32_t z_delta = cd->waypoint_z_cache - objects[ai.active_obj_idx].world_z;
			if (z_delta < 0)
				z_delta = -z_delta;
			if (z_delta > 512) {
				cd->ai_climb_state = 1;
				cd->ai_target_heading = (uint16_t)trig2_zangle;
				cd->throttle_speed = 0xC000u; /* -16384 as uint */
				cd->ai_heading_state = pick_heading_state(cd->ai_target_heading, cd->orient_heading);
			}
		}
		paiman_setflighttotarget(0, 1);
		cd->ai_plan_state = 236;
		if (cd->ai_pitch_state == 3 && objects[ai.active_obj_idx].roll) {
			cd->ai_roll_state = 1;
			cd->ai_roll_step = 0xFFFFu;
			cd->ai_target_roll = 0;
		}
	}

	speed = fg_array[ai.fg_idx].ai[ai.ai_entry_count].speed;

	/* For starships, damp throttle during an active pitch to tighten
	 * the turn; otherwise pipe speed through throttleconvert. */
	if (objects[ai.active_obj_idx].genus == 4) {
		uint16_t pd = abs_angle_delta((uint16_t)objects[ai.active_obj_idx].pitch, cd->ai_target_pitch);
		if (cd->ai_pitch_state == 2 && pd >= 0x1000u) {
			cd->throttle_speed = 0;
			return 0;
		}
	}
	cd->throttle_speed = _throttleconvert[speed];
	return 0;
}

/* ---- MODE_HeadTowardFull (7) --------------------------------------- */

// FUNCTION: TIE 0x39DFC
void paiman_initheadtowardfullmaneuver(void) {
	paiman_setflighttotarget(0, 0);
	craftptr->throttle_speed = 0xFFFFu;
	craftptr->ai_plan_state = 1180;
}

// FUNCTION: TIE 0x39E1C
int16_t paiman_headtowardfullmaneuver(void) {
	if (!craftptr->ai_plan_state) {
		pai_settarget();
		paiman_setflighttotarget(0, 0);
		craftptr->throttle_speed = 0xFFFFu;
		craftptr->ai_plan_state = 1180;
	}
	return 0;
}

/* ---- MODE_RunAway (8) ---------------------------------------------- */

// FUNCTION: TIE 0x39E50
void paiman_initrunawaymaneuver(void) {
	paiman_controlplane();
	if ((uint16_t)objects[ai.active_obj_idx].roll < 0x8000u)
		paiman_setflighttotarget((int16_t)0x8000, 1);
}

// FUNCTION: TIE 0x39E94
int16_t paiman_runawaymaneuver(void) {
	paiman_setflighttotarget((int16_t)0x8000, 1);
	craftptr->throttle_speed = 0xFFFFu;
	return 0;
}

/* ---- MODE_HeadOnAttack (9) ----------------------------------------- */

// FUNCTION: TIE 0x39EB8
void paiman_initheadonattackmaneuver(void) {
	CraftData* cd = craftptr;
	cd->ai_target_ref = (int16_t)cd->attacker_idx;
	paiman_setflighttotarget(0, 1);
	cd->throttle_speed = 0xFFFFu;
	cd->maneuver_timer = 1888;
}

// FUNCTION: TIE 0x39EEC
int16_t paiman_headonattackmaneuver(void) {
	if (!craftptr->maneuver_timer)
		return 1;
	paiman_setflighttotarget(0, 1);
	craftptr->throttle_speed = 0xFFFFu;
	return 0;
}

/* ---- MODE_FollowLeader (10) ---------------------------------------- */

void paiman_initfollowleadermaneuver(void) { /* No-op: the runtime step re-initialises every frame. */ }

// FUNCTION: TIE 0x39F20
int16_t paiman_followleadermaneuver(void) {
	CraftData* cd = craftptr;
	uint16_t self_idx = ai.active_obj_idx;
	uint8_t leader_idx = cd->leader_obj_idx;

	/* Self-led (no leader) — typically the same-frame aftermath of
	 * paiorder_leaderdeadorder promoting this craft (sets leader_obj_idx
	 * to 0xFF) before PAI_initplan moves mode_byte off FollowLeader. The
	 * binary OOB-reads objects[0xFF] for distance/world/pitch/roll and
	 * almost always lands in the polardistance>0x10000 branch (garbage
	 * coords are far away), which copies garbage into the waypoint cache
	 * and ramps throttle to max before zeroing push. Skip the body and
	 * just zero the push — the corruption is meaningless and the next
	 * tick re-runs PAI_initplan with the correct mode_byte. */
	if (leader_idx == 0xFF) {
		goto zero_push;
	}

	pai_distancebetween(leader_idx, self_idx);

	/* Far from leader: teleport waypoint + full throttle chase. */
	if (trig2_polardistance > 0x10000) {
		cd->waypoint_x_cache = objects[leader_idx].world_x;
		cd->waypoint_y_cache = objects[leader_idx].world_y;
		cd->waypoint_z_cache = objects[leader_idx].world_z;
		paiman_setflighttotarget(0, 1);
		cd->throttle_speed = 0xFFFFu;
		goto zero_push;
	}

	/* Match leader's pitch. Leader actively pitching → use their target;
	 * otherwise match their current pitch. Skip a turn if we're already
	 * aligned. */
	if (ai.leader_craft->ai_pitch_state == 2) {
		cd->ai_target_pitch = ai.leader_craft->ai_target_pitch;
		paiman_setturn((int16_t)(((int32_t)cd->skill_value >> 3) + 0x4000));
	} else if (objects[leader_idx].pitch != objects[self_idx].pitch) {
		cd->ai_target_pitch = (uint16_t)objects[leader_idx].pitch;
		paiman_setturn((int16_t)(((int32_t)cd->skill_value >> 3) + 0x4000));
	}

	/* Throttle: chase player leader by speed delta (50 units/frame slew,
	 * saturating); mirror NPC leader's throttle. */
	if (leader_idx == (uint8_t)pstate.object_idx) {
		uint16_t leader_speed = (uint16_t)objects[leader_idx].current_speed;
		uint16_t our_speed = (uint16_t)objects[self_idx].current_speed;

		if (leader_speed > our_speed) {
			uint32_t bump = 50u * (uint32_t)(leader_speed - our_speed);
			uint32_t nth = (uint32_t)cd->throttle_speed + bump;
			cd->throttle_speed = (nth > 0xFFFFu) ? 0xFFFFu : (uint16_t)nth;
		} else if (leader_speed < our_speed) {
			uint32_t brake = 50u * (uint32_t)(our_speed - leader_speed);
			cd->throttle_speed = (brake >= cd->throttle_speed) ? 0u : (uint16_t)(cd->throttle_speed - brake);
		}
	} else {
		cd->throttle_speed = ai.leader_craft->throttle_speed;
	}

	/* Match heading (snap if close, else short-way). */
	{
		uint16_t hd_delta = abs_angle_delta(cd->orient_heading, ai.leader_craft->orient_heading);
		if (hd_delta >= 0x400u) {
			cd->ai_heading_step = 0xFFFFu;
			cd->ai_target_heading = ai.leader_craft->orient_heading;
			cd->ai_heading_force = 0;
			cd->ai_heading_state = pick_heading_state(cd->ai_target_heading, cd->orient_heading);
		} else {
			cd->ai_heading_state = 0;
			cd->orient_heading = ai.leader_craft->orient_heading;
		}
	}

	/* Match roll — but only once leader's spin has settled. */
	if (ai.leader_craft->spin_done_flag == 0xFFFFu) {
		uint16_t roll_delta =
			abs_angle_delta((uint16_t)objects[self_idx].roll, (uint16_t)objects[ai.leader_obj_idx].roll);
		if (roll_delta >= 0x400u) {
			cd->ai_roll_step = 0xFFFFu;
			cd->ai_roll_state = 1;
			cd->ai_target_roll = (uint16_t)objects[ai.leader_obj_idx].roll;
		} else {
			objects[self_idx].roll = objects[ai.leader_obj_idx].roll;
			objects[self_idx].orient_dirty = 1;
			cd->ai_roll_state = 0;
		}
	}

	/* Formation follow. Skip (hold position) when our leader is the
	 * player and is nearly stationary. */
	if (cd->leader_obj_idx == (uint8_t)pstate.object_idx && (uint16_t)pstate.player->current_speed <= 0xAu) {
		goto zero_push;
	}
	paiman_calcformation();
	return 0;

zero_push:
	cd->push_accum_x = 0;
	cd->push_accum_y = 0;
	cd->push_accum_z = 0;
	return 0;
}

/* ---- MODE_SetupAttack (11), MODE_Attack (12), MODE_AttackSecondary (23) */

// FUNCTION: TIE 0x3A2AC
void paiman_initsetupattackmaneuver(void) {
	craftptr->throttle_speed = 0xFFFFu;
	paiman_attacktarget(0);
}

// FUNCTION: TIE 0x3A2FC
int16_t paiman_setupattackmaneuver(void) {
	uint16_t pitch_delta;

	paiman_attacktarget(0);
	pitch_delta = (uint16_t)(objects[ai.active_obj_idx].pitch - craftptr->ai_target_pitch);
	if (pitch_delta >= 0x3000u && pitch_delta <= 0xD000u)
		craftptr->throttle_speed = 0x8000;
	else
		craftptr->throttle_speed = 0xFFFFu;
	return 0;
}

// FUNCTION: TIE 0x3A364
int16_t paiman_attackmaneuver(void) {
	CraftData* cd = craftptr;
	uint8_t phase = cd->mode_subbyte;
	uint16_t self_idx = ai.active_obj_idx;
	int32_t break_radius;
	uint32_t appr_angle;
	uint16_t target_ref;
	uint8_t target_genus;

	if (phase) {
		return (phase == 1 && !cd->maneuver_timer) ? 1 : 0;
	}

	/* Phase 0 — in approach. Compute "breaking off" threshold by target
	 * class + our approach geometry. */
	pai_distancebetween(self_idx, (uint16_t)cd->ai_target_ref);
	target_ref = (uint16_t)cd->ai_target_ref;

	if (target_ref >= NUM_CRAFTS) {
		/* Static/warhead target — fall back to the fighter base radius
		 * with no alignment doubling. Reading objects[target_ref] would
		 * walk past NUM_CRAFTS into staticobjects/warhead memory. */
		break_radius = 5120;
	} else {
		target_genus = objects[target_ref].genus;
		if (target_genus == 4 || target_genus == 5 || target_genus == 3) {
			/* Capital ship: threshold 0x2000 only when approach angle is
			 * off-axis (0x2800..0x5800). */
			break_radius = 0x2000;
			appr_angle = abs_angle_delta((uint16_t)trig2_xyangle, (uint16_t)objects[target_ref].pitch);
			if (!(appr_angle >= 0x2800u && appr_angle <= 0x5800u))
				break_radius *= 2;
		} else {
			/* Fighter: threshold 5120 only when in pitch & heading alignment. */
			uint32_t pitch_delta =
				abs_angle_delta((uint16_t)objects[self_idx].pitch, (uint16_t)objects[target_ref].pitch);
			uint32_t heading_delta =
				abs_angle_delta((uint16_t)objects[self_idx].heading, (uint16_t)objects[target_ref].heading);
			break_radius = 5120;
			if (!(heading_delta <= 0x4000u && pitch_delta <= 0x4000u))
				break_radius *= 2;
		}
	}

	if (break_radius <= trig2_polardistance &&
		cd->hit_count < spec_data[cd->species_idx].evade_hit_threshold) {
		/* Still approaching: hand off to attacktarget. */
		paiman_attacktarget(0);
		cd->throttle_speed = (cd->mode_byte == MODE_Attack) ? 0xFFFFu : 0xC000u;
		return 0;
	}

	/* Preserve the low random byte while masking and biasing the high byte. */
	{
		uint16_t r1 = (uint16_t)math2_getrandom();
		int32_t rnd_pitch_jink = (int32_t)((((((uint16_t)(r1 >> 8)) & 0x3Fu) + 0x30u) << 8) | (r1 & 0xFFu));
		int32_t signed_jink = ((uint16_t)math2_getrandom() >= 0x8000u) ? -rnd_pitch_jink : rnd_pitch_jink;
		int16_t rnd_heading = (int16_t)(math2_getrandom() & 0x7FFFu);

		cd->ai_target_pitch = (uint16_t)(objects[self_idx].pitch + signed_jink);
		paiman_setturn((int16_t)(((int32_t)cd->skill_value >> 1) + 0x8000));

		cd->ai_climb_state = 0;
		cd->ai_dive_state = 0;
		cd->ai_heading_force = 0;
		cd->ai_heading_step = 0xFFFFu;
		cd->ai_target_heading = (uint16_t)rnd_heading;
		cd->missile_count = 0;
		cd->missile_count_total = 0;
		cd->ai_heading_state = pick_heading_state(cd->ai_target_heading, cd->orient_heading);
		cd->throttle_speed = 0xFFFFu;
	}

	/* Timer: 20..27 s against capitals, 2..5 s against fighters.
	 * Static/warhead targets default to the fighter range. */
	{
		uint16_t tref = (uint16_t)cd->ai_target_ref;
		uint8_t tg = (tref < NUM_CRAFTS) ? objects[tref].genus : 0;
		uint32_t r = (uint32_t)(math2_getrandom() & ((tg == 4 || tg == 5 || tg == 3) ? 7u : 3u));
		uint32_t base = (tg == 4 || tg == 5 || tg == 3) ? 20u : 2u;
		cd->maneuver_timer = 236 * (int32_t)(r + base);
	}
	cd->mode_subbyte = 1;
	return 0;
}

/* ---- MODE_Zoom (13) / MODE_Dive (14) — shared runtime body --------- */

// FUNCTION: TIE 0x3A6C0
void paiman_initzoommaneuver(void) {
	CraftData* cd = craftptr;
	int16_t up_pitch_rnd;
	uint16_t timer_mask;

	cd->throttle_speed = 0xFFFFu;
	cd->ai_roll_state = 3;
	cd->ai_roll_step = 0xFFFFu;
	cd->ai_target_roll = (uint16_t)math2_getrandom();

	up_pitch_rnd = math2_getrandom();
	{
		uint8_t lo = (uint8_t)up_pitch_rnd;
		uint8_t hi = (uint8_t)((((uint16_t)up_pitch_rnd >> 8) & 0xF) + 32);
		uint16_t pitch16 = (uint16_t)((uint16_t)hi << 8) | lo;
		cd->ai_target_heading = (uint16_t)(0x4000 - pitch16);
	}

	timer_mask = (uint16_t)((uint8_t)math2_getrandom()) & 3;
	cd->ai_climb_state = 0;
	cd->ai_dive_state = 0;
	cd->maneuver_timer = 236 * (int32_t)(timer_mask + 3);
	cd->ai_heading_force = 0;
	cd->ai_heading_step = 0xFFFFu;
	cd->ai_heading_state = pick_heading_state(cd->ai_target_heading, cd->orient_heading);
}

// FUNCTION: TIE 0x3A760
int16_t paiman_zoommaneuver(void) { return craftptr->maneuver_timer == 0 ? 1 : 0; }

// FUNCTION: TIE 0x3A770
void paiman_initdivemaneuver(void) {
	CraftData* cd = craftptr;
	int16_t down_pitch_rnd;

	cd->throttle_speed = 0xFFFFu;
	down_pitch_rnd = math2_getrandom();
	{
		uint8_t lo = (uint8_t)down_pitch_rnd;
		uint8_t hi = (uint8_t)((((uint16_t)down_pitch_rnd >> 8) & 0xF) + 88);
		down_pitch_rnd = (int16_t)(((uint16_t)hi << 8) | lo);
	}
	cd->ai_climb_state = 0;
	cd->ai_heading_force = 0;
	cd->ai_target_heading = (uint16_t)down_pitch_rnd;
	cd->ai_heading_step = 0xFFFFu;
	cd->maneuver_timer = 1180;
	cd->ai_heading_state = pick_heading_state(cd->ai_target_heading, cd->orient_heading);
}

/* ---- MODE_SplitsDive (15) / MODE_SplitsDiveAlt (27) ---------------- */

// FUNCTION: TIE 0x3A7C4
void paiman_initsplitsdivemaneuver(void) {
	CraftData* cd = craftptr;
	uint16_t hd_rnd = (uint16_t)math2_getrandom();

	cd->ai_roll_state = 1;
	cd->ai_roll_step = 0xFFFFu;
	cd->ai_target_roll = 0x8000;
	cd->ai_pitch_state = 0;
	cd->ai_heading_force = 1;

	/* Watcom: `and ah, 3Fh; add ah, 40h` — high byte masked/biased into
	 * [0x40..0x7F], low byte stays as raw random. */
	hd_rnd = (uint16_t)(((((hd_rnd >> 8) & 0x3Fu) + 0x40u) << 8) | (hd_rnd & 0xFFu));
	cd->ai_heading_state = 2;
	cd->ai_heading_step = 0xFFFFu;
	cd->ai_target_heading = hd_rnd;
}

// FUNCTION: TIE 0x3A810
int16_t paiman_splitsdivemaneuver(void) {
	return (craftptr->ai_roll_state == 4 && craftptr->ai_heading_state == 3) ? 1 : 0;
}

/* ---- MODE_SpeedAway (16) ------------------------------------------- */

// FUNCTION: TIE 0x3A838
void paiman_initspeedawaymaneuver(void) {
	CraftData* cd = craftptr;
	cd->throttle_speed = 0xFFFFu;
	cd->maneuver_timer = 4720;
	cd->ai_target_pitch = (uint16_t)(objects[ai.active_obj_idx].pitch + (uint8_t)math2_getrandom());
	paiman_setjink(ai.active_obj_idx);
}

// FUNCTION: TIE 0x3A89C
int16_t paiman_speedawaymaneuver(void) {
	CraftData* cd = craftptr;

	if (!cd->ai_plan_state) {
		cd->ai_target_pitch = (uint16_t)-(int16_t)cd->ai_target_pitch;
		paiman_setjink(ai.active_obj_idx);
	}
	return cd->maneuver_timer == 0 ? 1 : 0;
}

/* ---- MODE_IntoHyperspace (21) -------------------------------------- */

// FUNCTION: TIE 0x3A968
void paiman_initintohyperspacemaneuver(void) {
	CraftData* cd = craftptr;
	paiman_setflighttotarget(0, 1);
	cd->throttle_speed = 0xFFFFu;
	cd->mode_subbyte = 0;
}

// FUNCTION: TIE 0x3A98C
int16_t paiman_intohyperspacemaneuver(void) {
	CraftData* cd = craftptr;
	uint8_t phase = cd->mode_subbyte;

	if (phase == 0) {
		paiman_setflighttotarget(0, 1);
		if (trig2_polardistance < 0x4000) {
			cd->flight_flag = 5;
			cd->ai_roll_state = 0;
			cd->ai_heading_state = 0;
			cd->ai_pitch_state = 0;
			cd->mode_subbyte = 1;
			cd->ai_plan_state = 944;
			cd->maneuver_timer = 1652;
		}
		cd->throttle_speed = 0xFFFFu;
		return 0;
	}

	if (phase != 1)
		return 0;

	cd->flight_flag = 5;
	if ((uint16_t)objects[ai.active_obj_idx].current_speed < 0xE10u)
		return 0;

	msg_craftmessage(ai.active_obj_idx, cd, 0x61);
	score_craftexitscoring(ai.active_obj_idx, ai.fg_idx, 3);
	objects[ai.active_obj_idx].ship_idx = 0;

	/* Carry-over passenger (tow_slave_ref) exit bookkeeping. */
	if ((uint16_t)cd->tow_slave_ref != 0xFFFFu && (uint16_t)cd->tow_slave_ref < 14336u) {
		uint16_t pax_obj_idx = (uint16_t)cd->tow_slave_ref;
		uint8_t pax_fg_idx = objects[pax_obj_idx].fg_idx;
		CraftData* pax_cd = objects[pax_obj_idx].craft_ptr;

		score_craftexitscoring((uint16_t)cd->tow_slave_ref, pax_fg_idx, 7);
		++fgstatus[pax_fg_idx].cond[1].detail;
		if (fg_array[pax_fg_idx].special_craft == pax_cd->craft_idx_in_fg)
			fgstatus[pax_fg_idx].cond_id[1].detail = 1;
		objects[pax_obj_idx].ship_idx = 0;
	}
	return 0;
}

/* ---- MODE_OutOfHyperspace (22) ------------------------------------- */

// FUNCTION: TIE 0x3AB48
void paiman_initoutofhyperspacemaneuver(void) {
	CraftData* cd = craftptr;

	cd->flight_flag = 6;
	objects[ai.active_obj_idx].current_speed = 3600;
	cd->mode_subbyte = 0;
	cd->ai_plan_state = 236;
	cd->ai_target_ref = (int16_t)0x8000u;
	pai_settarget();
	cd->maneuver_timer = 2596;
	cd->capture_list[0] = cd->ai_update_rate; /* save for restore */
	cd->ai_update_rate = 59;
}

// FUNCTION: TIE 0x3ABA8
int16_t paiman_outofhyperspacemaneuver(void) {
	CraftData* cd = craftptr;
	bool done = false;
	uint8_t mapped_order;

	if (!cd->ai_plan_state) {
		if (++cd->mode_subbyte > 8)
			cd->mode_subbyte = 8;
		objects[ai.active_obj_idx].current_speed = (int16_t)_stagevel[cd->mode_subbyte];
		cd->ai_plan_state = 236;
	}

	if (cd->leader_obj_idx != 0xFFu) {
		if (ai.leader_craft->current_order != 52)
			done = true;
	} else {
		trig2_ctop(cd->waypoint_x_cache - objects[ai.active_obj_idx].world_x,
				   cd->waypoint_y_cache - objects[ai.active_obj_idx].world_y,
				   cd->waypoint_z_cache - objects[ai.active_obj_idx].world_z);
		if (trig2_polardistance < 0x4000 || !cd->maneuver_timer)
			done = true;
	}

	if (!done)
		return 0;

	mapped_order = (cd->leader_obj_idx == 0xFFu) ? ordersldr[fg_array[ai.fg_idx].ai[0].order]
												 : ordersflw[fg_array[ai.fg_idx].ai[0].order];

	cd->flight_flag = 0;
	cd->ai_target_ref = (int16_t)0xFF; /* clear maneuver target ref */
	cd->ai_update_rate = cd->capture_list[0];

	/* Watcom self-modifying plan: write mapped_order into byte[3] of
	 * outofhyperspaceplan (= byte_C5FEF in the binary). The plan VM reads
	 * this byte as the next_order on transition; without it the maneuver
	 * returns 1 but the VM never transitions out of mode_byte 22 — leader
	 * pinned at 250, followers stuck on the stagevel ladder at 1800. */
	outofhyperspaceplan[3] = mapped_order;

	objects[ai.active_obj_idx].current_speed = (mapped_order > 2) ? 250 : 0;

	return 1;
}

/* ---- MODE_Escort (17) ---------------------------------------------- */

void paiman_initescortmaneuver(void) { /* No-op: the runtime step is fully self-initialising. */ }

// FUNCTION: TIE 0x3AD70
int16_t paiman_escortmaneuver(void) {
	CraftData* cd = craftptr;
	uint16_t self_idx = ai.active_obj_idx;
	uint16_t leader_idx = 0xFFu;
	uint16_t i;
	CraftData* leader_cd;
	uint32_t catchup_dist;

	/* (1) Find the escort target FG's leader. */
	for (i = 0; i < NUM_CRAFTS; ++i) {
		if (!objects[i].ship_idx)
			continue;
		if (objects[i].fg_idx != cd->escortee_fg_idx)
			continue;
		if (objects[i].craft_ptr->leader_obj_idx != 0xFFu)
			continue;
		leader_idx = i;
		break;
	}
	if (leader_idx == 0xFFu) {
		paiman_setflighttotarget(0, 1);
		cd->throttle_speed = 0x8000;
		return 0;
	}

	pai_distancebetween(leader_idx, self_idx);
	leader_cd = objects[leader_idx].craft_ptr;

	/* (2) Far from leader or leader-status-dead: chase mode. */
	catchup_dist = (species_table[objects[leader_idx].ship_idx].bound_hwidth < 0xBB8u) ? 0x8000u : 0x20000u;

	if ((int32_t)catchup_dist < trig2_polardistance || !leader_cd->status_flags) {
		cd->waypoint_x_cache = objects[leader_idx].world_x;
		cd->waypoint_y_cache = objects[leader_idx].world_y;
		cd->waypoint_z_cache = objects[leader_idx].world_z;
		paiman_setflighttotarget(leader_cd->status_flags ? 0 : (int16_t)0x4000, 1);
		cd->throttle_speed = (trig2_polardistance <= 0x10000) ? 0x4000 : 0xFFFFu;
		cd->push_accum_x = 0;
		cd->push_accum_y = 0;
		cd->push_accum_z = 0;
		return 0;
	}

	/* (3) Match leader pitch. */
	if (leader_cd->ai_pitch_state == 2) {
		cd->ai_target_pitch = leader_cd->ai_target_pitch;
		paiman_setturn((int16_t)(((int32_t)cd->skill_value >> 3) + 0x4000));
	} else if (objects[leader_idx].pitch != objects[self_idx].pitch) {
		cd->ai_target_pitch = (uint16_t)objects[leader_idx].pitch;
		paiman_setturn((int16_t)(((int32_t)cd->skill_value >> 3) + 0x4000));
	}

	/* (4) Throttle: chase leader speed unconditionally. Retail does not
	 * gate this on leader-is-player; it adjusts by 50*(speed_delta) for
	 * any leader. */
	{
		uint16_t leader_speed = (uint16_t)objects[leader_idx].current_speed;
		uint16_t our_speed = (uint16_t)objects[self_idx].current_speed;

		if (leader_speed > our_speed) {
			uint32_t bump = 50u * (uint32_t)(leader_speed - our_speed);
			uint32_t nth = (uint32_t)cd->throttle_speed + bump;
			cd->throttle_speed = (nth > 0xFFFFu) ? 0xFFFFu : (uint16_t)nth;
		} else if (leader_speed < our_speed) {
			uint32_t brake = 50u * (uint32_t)(our_speed - leader_speed);
			cd->throttle_speed = (brake >= cd->throttle_speed) ? 0u : (uint16_t)(cd->throttle_speed - brake);
		}
	}

	/* (5) Match heading. */
	{
		uint16_t hd_delta = abs_angle_delta(cd->orient_heading, leader_cd->orient_heading);
		if (hd_delta >= 0x400u) {
			cd->ai_heading_step = 0xFFFFu;
			cd->ai_target_heading = leader_cd->orient_heading;
			cd->ai_heading_force = 0;
			cd->ai_heading_state = pick_heading_state(cd->ai_target_heading, cd->orient_heading);
		} else {
			cd->ai_heading_state = 0;
			cd->orient_heading = leader_cd->orient_heading;
		}
	}

	/* (6) Match roll. */
	{
		uint16_t roll_delta =
			abs_angle_delta((uint16_t)objects[self_idx].roll, (uint16_t)objects[leader_idx].roll);
		if (roll_delta >= 0x400u) {
			cd->ai_roll_step = 0xFFFFu;
			cd->ai_roll_state = 1;
			cd->ai_target_roll = (uint16_t)objects[leader_idx].roll;
		} else {
			objects[self_idx].roll = objects[leader_idx].roll;
			objects[self_idx].orient_dirty = 1;
			cd->ai_roll_state = 0;
		}
	}

	/* (7) Positional offset. Escort slot = ai[ai_count].var[0] (0..26). */
	{
		uint8_t slot_byte = fg_array[ai.fg_idx].ai[ai.ai_entry_count].var[0];
		pai_calcrotatedpoint(&objects[leader_idx], _escortsidepos[slot_byte], _escortuppos[slot_byte],
							 _escortfwdpos[slot_byte]);

		if (species_table[objects[leader_idx].ship_idx].bound_hwidth >= 0xBB8u) {
			rotatedx *= 16;
			rotatedy *= 16;
			rotatedz *= 16;
		}

		if (leader_cd->leader_obj_idx == (uint8_t)pstate.object_idx &&
			(uint16_t)pstate.player->current_speed <= 0xAu) {
			cd->push_accum_x = 0;
			cd->push_accum_y = 0;
			cd->push_accum_z = 0;
			return 0;
		}

		cd->push_accum_x = rotatedx + objects[leader_idx].world_x - objects[self_idx].world_x;
		cd->push_accum_y = rotatedy + objects[leader_idx].world_y - objects[self_idx].world_y;
		cd->push_accum_z = rotatedz + objects[leader_idx].world_z - objects[self_idx].world_z;
	}
	return 0;
}

/* ---- MODE_AwaitBoard (19) / AwaitBoardAlt (25) --------------------- */

// FUNCTION: TIE 0x3C7FC
void paiman_initawaitboardmaneuver(void) {
	CraftData* cd = craftptr;
	cd->ai_roll_state = 0;
	cd->ai_heading_state = 0;
	cd->ai_pitch_state = 0;
	cd->throttle_speed = 0;
}

// FUNCTION: TIE 0x3C81C
int16_t paiman_awaitboardmaneuver(void) {
	CraftData* cd = craftptr;
	cd->ai_roll_state = 0;
	cd->ai_heading_state = 0;
	cd->ai_pitch_state = 0;
	cd->throttle_speed = 0;
	return 0;
}

/* ---- MODE_HeadToward (20) ------------------------------------------ */

// FUNCTION: TIE 0x3C83C
void paiman_initheadtowardmaneuver(void) { paiman_setflighttotarget(0, 1); }

// FUNCTION: TIE 0x3C84C
int16_t paiman_headtowardmaneuver(void) {
	CraftData* cd = craftptr;
	paiman_setflighttotarget(0, 1);
	cd->ai_roll_state = 1;
	cd->ai_roll_step = 0xFFFFu;
	cd->ai_target_roll = 0;
	return 0;
}

/* ---- MODE_TurnAway (24) -------------------------------------------- */

// FUNCTION: TIE 0x3C878
void paiman_initturnawaymaneuver(void) {
	paiman_setnewturnaway(ai.active_obj_idx);
	craftptr->maneuver_timer = 3540;
}

// FUNCTION: TIE 0x3C894
int16_t paiman_turnawaymaneuver(void) {
	if (!craftptr->maneuver_timer)
		return 1;
	if (!craftptr->ai_plan_state)
		paiman_setnewturnaway(ai.active_obj_idx);
	return 0;
}

/* ---- MODE_OutOfHangar (26) ----------------------------------------- */

// FUNCTION: TIE 0x3C94C
void paiman_initoutofhangarmaneuver(void) { craftptr->maneuver_timer = 2360; }

// FUNCTION: TIE 0x3C95C
int16_t paiman_outofhangarmaneuver(void) {
	CraftData* cd = craftptr;
	if (cd->maneuver_timer)
		return 0;

	/* The plan VM reads exithangarplan[3] as the next order. */
	uint8_t mapped_order = (cd->leader_obj_idx == 0xFFu) ? ordersldr[fg_array[ai.fg_idx].ai[0].order]
														 : ordersflw[fg_array[ai.fg_idx].ai[0].order];
	exithangarplan[3] = mapped_order;

	cd->formation_separation = 2;
	return 1;
}

/* ---- MODE_AvoidStarship (28) / MODE_Wait (29) ---------------------- */

// FUNCTION: TIE 0x3C9D0
void paiman_initavoidstarshipmaneuver(void) {
	CraftData* cd = craftptr;
	uint16_t rnd_ticks = (uint16_t)(236u * (uint32_t)((math2_getrandom() & 7) + 15));
	cd->ai_plan_state = rnd_ticks;
	paiman_setturn((int16_t)(((int32_t)cd->skill_value >> 1) + 0x8000));
	cd->ai_heading_step = 0xFFFFu;
	cd->ai_heading_force = 0;
	cd->ai_heading_state = pick_heading_state(cd->ai_target_heading, cd->orient_heading);
}

// FUNCTION: TIE 0x3CA2C
void paiman_initwaitmaneuver(void) {
	CraftData* cd = craftptr;
	uint8_t var0 = fg_array[ai.fg_idx].ai[ai.ai_entry_count].var[0];
	cd->ai_roll_state = 0;
	cd->ai_heading_state = 0;
	cd->ai_pitch_state = 0;
	cd->throttle_speed = 0;
	cd->maneuver_timer = 1180 * (int32_t)var0;
}

int16_t paiman_avoidstarshipmaneuver(void) {
	/* Shared null body; returns 0 so the plan VM stays in its slot. */
	return 0;
}

/* ---- MODE_DropOff (30) --------------------------------------------- */

// FUNCTION: TIE 0x3CA94
int16_t paiman_dropoffmaneuver(void) {
	CraftData* cd = craftptr;
	uint8_t active_waypoint_idx;
	uint16_t anchor_obj = 0xFFu;
	uint8_t tgt_fg_idx = (uint8_t)((int8_t)fg_array[ai.fg_idx].ai[ai.ai_entry_count].var[1] - 1);
	uint8_t craft_index = cd->active_waypoint_idx;
	uint16_t i;
	int32_t dx, dy, dz;

	if (cd->mode_subbyte) {
		if (!cd->maneuver_timer) {
			active_waypoint_idx = cd->active_waypoint_idx;
			cd->mode_subbyte = 0;
			cd->active_waypoint_idx = (uint8_t)(active_waypoint_idx + 1);
		}
		cd->ai_goal_progress[ai.ai_entry_count] = cd->active_waypoint_idx;
		return 0;
	}

	/* Scan for the target-FG's leader. */
	for (i = 0; i < NUM_CRAFTS; ++i) {
		if (objects[i].ship_idx && objects[i].fg_idx == tgt_fg_idx &&
			objects[i].craft_ptr->leader_obj_idx == 0xFFu) {
			anchor_obj = i;
		}
	}

	create_getdropposition(tgt_fg_idx, craft_index, anchor_obj);
	const uint8_t model_type = objects[ai.active_obj_idx].ship_idx;
	if (!TieProfile_UsesTie98Logic())
		draw_lockshipfileptrs(model_type);

	{
		int32_t shield_hi = TieProfile_UsesTie98Logic() ? -modelbounds_getminz(model_type)
														: (objectblockptr->shield_default >> 16);
		dx = worldlocx - objects[ai.active_obj_idx].world_x;
		dy = worldlocy - objects[ai.active_obj_idx].world_y;
		dz = (worldlocz + (-shield_hi >> 1)) - objects[ai.active_obj_idx].world_z;

		cd->push_accum_x = dx;
		cd->push_accum_y = dy;
		cd->push_accum_z = dz;

		trig2_ctop(dx, dy, dz);

		{
			int32_t ax = dx < 0 ? -dx : dx;
			int32_t ay = dy < 0 ? -dy : dy;
			int32_t az = dz < 0 ? -dz : dz;

			if (ay + ax > 256) {
				if ((uint16_t)trig2_xyangle != (uint16_t)objects[ai.active_obj_idx].pitch) {
					cd->ai_pitch_state = 2;
					cd->ai_pitch_step = 0x8000;
					cd->ai_target_pitch = (uint16_t)trig2_xyangle;
				}
			}

			if (ax + ay + az < 32) {
				AiContext saved_ai_ctx;
				fgcnt = tgt_fg_idx;
				saved_ai_ctx = ai;
				leaderflag = (uint8_t)anchor_obj;
				create_startflightgroup((int16_t)craft_index, (int16_t)0);
				ai = saved_ai_ctx;

				cd->maneuver_timer = 1180;
				cd->push_accum_z = 1500;
				cd->mode_subbyte = (uint8_t)(cd->mode_subbyte + 1);
			}
		}
	}

	cd->ai_goal_progress[ai.ai_entry_count] = cd->active_waypoint_idx;
	return 0;
}

/* ---- MODE_Board (18) ----------------------------------------------- */
/*
 * Boarding state machine — the single biggest maneuver in the binary.
 * Four phases tracked in mode_subbyte:
 *   0  approach:  compute dock point, throttle cascade, advance at <2048
 *   1  align:     push_accum toward leader roll/heading/pitch, on <16 merged
 *                 fire MSG_DOCKED_WITH + SFX, advance to phase 2
 *   2  transfer:  after maneuver_timer, dispatch by default_order_ldr:
 *                   0x1C=unload cargo, 0x1D=load cargo, 0x1E=swap
 *                   0x1F=capture-and-steer-target
 *                   0x20=repair, 0x21-0x22=retrieve passenger (tow_slave_ref)
 *                   0x44=refit subsystems (rebuild working_subsystems /
 *                        status_flags from installed_subsystems)
 *   3  departure: wait for maneuver_timer then exit (return 1)
 *
 * Returns 1 only on phase-3 completion; otherwise 0.
 */

// FUNCTION: TIE 0x3B334
void paiman_initboardmaneuver(void) { craftptr->mode_subbyte = 0; }

/* Phase 0 — approach to dock point. */
static int16_t board_phase0(CraftData* cd, uint16_t target_ref, uint8_t tgt_species) {
	int32_t wp_z;

	if (target_ref >= 0x3800u) {
		/* Static target: just use its world pos + 2048 z offset. */
		create_getworldposition(target_ref, 0);
		cd->waypoint_x_cache = worldlocx;
		cd->waypoint_y_cache = worldlocy;
		wp_z = worldlocz + 2048;
	} else {
		/* Live target: species-specific dock approach offset. */
		int16_t dock_fwd = spec_data[tgt_species].dock_fwd;
		int16_t cd_rspd;
		int16_t tgt_heading_sel;
		int16_t dock_active_heavy = spec_data[cd->species_idx].dock_active_heavy;

		if (objects[target_ref].genus && objects[target_ref].genus != GENUS_TRANSPORT) {
			if (objects[ai.active_obj_idx].genus && objects[ai.active_obj_idx].genus != GENUS_TRANSPORT)
				cd_rspd = (int16_t)(spec_data[tgt_species].dock_passive_heavy +
									spec_data[tgt_species].dock_passive_heavy - dock_active_heavy);
			else
				cd_rspd = (int16_t)(spec_data[tgt_species].dock_passive_heavy +
									spec_data[tgt_species].dock_passive_light - dock_active_heavy);
		} else {
			cd_rspd = (int16_t)(spec_data[tgt_species].dock_passive_light -
								spec_data[cd->species_idx].dock_active_light);
		}
		tgt_heading_sel = (int16_t)(spec_data[tgt_species].dock_passive_heavy - dock_active_heavy +
									spec_data[tgt_species].dock_passive_heavy - dock_active_heavy + cd_rspd);
		if (tgt_heading_sel < 0)
			tgt_heading_sel = 28672;

		pai_calcrotatedpoint(&objects[target_ref], 0, tgt_heading_sel, dock_fwd);
		cd->waypoint_x_cache = rotatedx + objects[target_ref].world_x;
		cd->waypoint_y_cache = rotatedy + objects[target_ref].world_y;
		wp_z = rotatedz + objects[target_ref].world_z;
	}
	cd->waypoint_z_cache = wp_z;

	paiman_setflighttotarget(0, 1);
	if (trig2_polardistance > 0x4000)
		cd->throttle_speed = 0xFFFFu;
	if (trig2_polardistance > 0x2000) {
		cd->throttle_speed = 0x8000;
		return 0;
	}
	if (trig2_polardistance <= 2048) {
		cd->throttle_speed = 0;
		cd->mode_subbyte = 1;
	} else {
		cd->throttle_speed = 0x4000;
	}
	return 0;
}

/* Phase 1 — close alignment + dock announcement. */
static int16_t board_phase1(CraftData* cd, uint16_t target_ref, uint8_t tgt_species, uint8_t tgt_fg_idx,
							uint8_t dock_delay_var0) {
	int32_t abs_dx, abs_dy, abs_dz;
	uint16_t target_roll_align;
	uint16_t target_heading_align;
	uint16_t target_pitch_align;
	int32_t push_x, push_y, push_z;

	if (target_ref >= 0x3800u) {
		create_getworldposition(target_ref, 0);
		push_x = worldlocx - objects[ai.active_obj_idx].world_x;
		push_y = worldlocy - objects[ai.active_obj_idx].world_y;
		push_z = (worldlocz + 128) - objects[ai.active_obj_idx].world_z;
		cd->push_accum_x = push_x;
		cd->push_accum_y = push_y;
		cd->push_accum_z = push_z;
		target_roll_align = 0;
		target_pitch_align = 0;
		target_heading_align = 0x4000u;
	} else {
		int16_t offset_up;
		if (objects[target_ref].genus && objects[target_ref].genus != GENUS_TRANSPORT) {
			if (objects[ai.active_obj_idx].genus && objects[ai.active_obj_idx].genus != GENUS_TRANSPORT)
				offset_up = (int16_t)(spec_data[tgt_species].dock_passive_heavy -
									  spec_data[cd->species_idx].dock_active_heavy);
			else
				offset_up = (int16_t)(spec_data[tgt_species].dock_passive_light -
									  spec_data[cd->species_idx].dock_active_heavy);
		} else {
			offset_up = (int16_t)(spec_data[tgt_species].dock_passive_light -
								  spec_data[cd->species_idx].dock_active_light);
		}
		pai_calcrotatedpoint(&objects[target_ref], 0, offset_up, spec_data[tgt_species].dock_fwd);
		push_x = rotatedx + objects[target_ref].world_x - objects[ai.active_obj_idx].world_x;
		push_y = rotatedy + objects[target_ref].world_y - objects[ai.active_obj_idx].world_y;
		push_z = rotatedz + objects[target_ref].world_z - objects[ai.active_obj_idx].world_z;
		cd->push_accum_x = push_x;
		cd->push_accum_y = push_y;
		cd->push_accum_z = push_z;
		target_roll_align = (uint16_t)objects[target_ref].roll;
		target_pitch_align = (uint16_t)objects[target_ref].pitch;
		target_heading_align = (uint16_t)objects[target_ref].heading;
	}

	if ((int16_t)target_roll_align != objects[ai.active_obj_idx].roll) {
		cd->ai_roll_state = 1;
		cd->ai_roll_step = 0x8000u;
		cd->ai_target_roll = target_roll_align;
	}
	if ((int16_t)target_pitch_align != objects[ai.active_obj_idx].pitch) {
		cd->ai_pitch_state = 2;
		cd->ai_pitch_step = 0x8000u;
		cd->ai_target_pitch = target_pitch_align;
	}
	if (objects[ai.active_obj_idx].heading != objects[target_ref].heading) {
		cd->ai_heading_step = 0x8000u;
		cd->ai_target_heading = target_heading_align;
		cd->ai_heading_force = 0;
		cd->ai_heading_state = pick_heading_state(cd->ai_target_heading, cd->orient_heading);
	}

	abs_dx = push_x < 0 ? -push_x : push_x;
	abs_dy = push_y < 0 ? -push_y : push_y;
	abs_dz = push_z < 0 ? -push_z : push_z;
	if (abs_dx + abs_dy + abs_dz >= 16)
		return 0;

	/* Docked. */
	cd->push_accum_x = 0;
	cd->push_accum_y = 0;
	cd->push_accum_z = 0;
	cd->mode_subbyte = 2;
	cd->ai_plan_state = 236;
	cd->maneuver_timer = 1180 * (int32_t)dock_delay_var0;

	msg_createobjectname(ai.active_obj_idx, 1, tempstring);
	msg_addmessageptr(0, tempstring);
	msg_createobjectname(target_ref, 1, temp2string);
	msg_addmessageptr(1, temp2string);
	messageside = objects[ai.active_obj_idx].side;
	msg_messageprintf(MSG_DOCKED_WITH);

	if (pstate.player->side != objects[ai.active_obj_idx].side) {
		fsfx_triggersfx(0x27, 0xFFFF);
		return 0;
	}
	fsfx_speakobjectname(ai.active_obj_idx, 0x33);

	{
		uint8_t order_ldr = cd->default_order_ldr;
		uint8_t voice;

		/* Voice cascade per binary 0x3BC03..0x3BC9C:
		 *   0x1C, 0x1D, 0x20, 0x21, 0x44 -> 0x43/0x42 (with-player vs other)
		 *   0x1E, 0x22                   -> 0x41 (operation-A)
		 *   0x1F                         -> 0x44 (operation-D / capture)
		 *   anything else                -> silent. */
		if (order_ldr < 0x1F) {
			if (order_ldr < 0x1C)
				return 0;
			if (order_ldr <= 0x1D) {
				voice = (target_ref == pstate.object_idx) ? 0x43 : 0x42;
				fsfx_speakoperation(voice, 0x3F);
				return 0;
			}
			/* 0x1E falls through to voice 0x41 below. */
		} else if (order_ldr == 0x1F) {
			fsfx_speakoperation(0x44, 0x3F);
			return 0;
		} else if (order_ldr < 0x22) {
			/* 0x20, 0x21 */
			voice = (target_ref == pstate.object_idx) ? 0x43 : 0x42;
			fsfx_speakoperation(voice, 0x3F);
			return 0;
		} else if (order_ldr == 0x22) {
			/* 0x22 falls through to voice 0x41. */
		} else if (order_ldr == 68) {
			voice = (target_ref == pstate.object_idx) ? 0x43 : 0x42;
			fsfx_speakoperation(voice, 0x3F);
			return 0;
		} else {
			return 0;
		}
		fsfx_speakoperation(0x41, 0x3F);
	}
	return 0;
}

/* Phase 2 — transfer. Returns 1 only if the post-transfer logic advances
 * directly to completion (it always falls through to phase 3 instead). */
static void board_phase2_run_transfer(CraftData* cd, CraftData* tgt_cd, uint16_t target_ref,
									  uint8_t tgt_fg_idx) {
	uint8_t order_ldr = cd->default_order_ldr;
	uint8_t msg_id = 119;
	uint16_t msg_obj = ai.active_obj_idx;
	CraftData* msg_cd = cd;

	switch (order_ldr) {
		case 0x1C: /* Unload to target. */
			if (target_ref < 0x3800u) {
				for (int k = 0; k < 16; ++k)
					tgt_cd->cargo[k] = cd->cargo[k];
				tgt_cd->boarding_state = 2;
			}
			cd->cargo[0] = 0;
			cd->boarding_state = 1;
			break;
		case 0x1D: /* Load from target. */
			if (target_ref < 0x3800u) {
				for (int k = 0; k < 16; ++k)
					cd->cargo[k] = tgt_cd->cargo[k];
				tgt_cd->cargo[0] = 0;
				tgt_cd->boarding_state = 1;
			}
			cd->boarding_state = 2;
			break;
		case 0x1E: /* Swap. */
			if (target_ref < 0x3800u) {
				for (int k = 0; k < 16; ++k) {
					char tmp = cd->cargo[k];
					cd->cargo[k] = tgt_cd->cargo[k];
					tgt_cd->cargo[k] = tmp;
				}
				tgt_cd->boarding_state = 2;
			}
			cd->boarding_state = 2;
			break;
		case 0x1F: /* Capture — set steerage + rebuild target's plan. */
			if (target_ref < 0x3800u) {
				AiContext saved_ai_ctx;
				int16_t max_speed_cache = tgt_cd->max_speed_cache;
				CraftData* cd_prev;

				tgt_cd->dock_state_flags = (uint8_t)(ai.fg_idx | 0x80u);
				++fgstatus[tgt_fg_idx].cond[3].detail;
				if (fg_array[tgt_fg_idx].special_craft == tgt_cd->craft_idx_in_fg)
					fgstatus[tgt_fg_idx].cond_id[3].detail = 1;
				objects[target_ref].side = objects[ai.active_obj_idx].side;
				if (objects[ai.active_obj_idx].side == 1 && objects[target_ref].ship_idx < 0x45u)
					++mission.captures_by_type[tgt_cd->species_idx];

				/* Reset captured craft to cruise mode. Binary 0x3C1E6
				 * writes flight_flag=0 before status_flags/current_order. */
				tgt_cd->flight_flag = 0;
				tgt_cd->status_flags = tgt_cd->subsystem_active;
				tgt_cd->current_order = max_speed_cache ? 47 : 1;

				cd_prev = craftptr;
				saved_ai_ctx = ai;
				craftptr = tgt_cd;
				pai_setupcraftaivars(target_ref);
				pai_initplan();
				craftptr = cd_prev;
				ai = saved_ai_ctx;

				msg_id = 101;
				msg_cd = cd_prev;
				msg_obj = target_ref;
			}
			break;
		case 0x20: /* Repair. */
			if (target_ref < 0x3800u) {
				AiContext saved_ai_ctx;
				tgt_cd->current_order = 67;
				tgt_cd->ai_target_ref = (int16_t)ai.active_obj_idx;
				saved_ai_ctx = ai;
				{
					CraftData* cd_prev = craftptr;
					craftptr = tgt_cd;
					pai_setupcraftaivars(target_ref);
					pai_initplan();
					craftptr = cd_prev;
				}
				ai = saved_ai_ctx;
			}
			break;
		case 0x21: /* Retrieve passenger: stage self as carrier. */
			if (target_ref < 0x3800u) {
				cd->tow_slave_ref = (int16_t)target_ref;
				tgt_cd->dock_state_flags = (uint8_t)(ai.fg_idx | 0xC0u);
				/* Imperial captures of non-Imperial low-id retrievals count
				 * toward the per-species kill ledger. Binary 0x3C305..0x3C33F:
				 * active.side==1 && (original) target.side != 1 && ship_idx<0x45.
				 * The check on target.side runs BEFORE the side overwrite. */
				if (objects[ai.active_obj_idx].side == 1 && objects[target_ref].side != 1 &&
					objects[target_ref].ship_idx < 0x45u)
					++mission.captures_by_type[tgt_cd->species_idx];
				objects[target_ref].side = objects[ai.active_obj_idx].side;
				if (fg_array[tgt_fg_idx].special_craft == tgt_cd->craft_idx_in_fg)
					fgstatus[tgt_fg_idx].cond_id[3].detail = 1;
			} else {
				/* Static anchor: bump status. */
				uint16_t sidx = target_ref - 14336u;
				uint8_t sf = staticobjects[sidx].fg_idx;
				++fgstatus[sf].cond[3].detail;
				/* The binary nulls species to mark the slot retrieved. */
				staticobjects[sidx].species = 0;
			}
			++fgstatus[tgt_fg_idx].cond[3].detail;
			break;
		case 0x22: /* Deliver passenger. */
			if (target_ref < 0x3800u)
				tgt_cd->boarding_state = 2;
			break;
		case 0x44: /* Repair subsystems. */
			if (target_ref < 0x3800u) {
				/* Binary 0x3C441 writes flight_flag=0 between loading
				 * subsystem_active and storing it into status_flags. */
				tgt_cd->flight_flag = 0;
				tgt_cd->status_flags = tgt_cd->subsystem_active;
			}
			break;
		default:
			break;
	}

	msg_craftmessage(msg_obj, msg_cd, msg_id);
}

static int16_t board_phase2(CraftData* cd, uint16_t target_ref, uint8_t tgt_species, uint8_t tgt_fg_idx,
							uint16_t target_idnumber) {
	CraftData* tgt_cd = (target_ref < 0x3800u) ? objects[target_ref].craft_ptr : NULL;
	bool changed_flag = false;

	if (!cd->maneuver_timer) {
		if (tgt_cd)
			board_phase2_run_transfer(cd, tgt_cd, target_ref, tgt_fg_idx);

		/* SFX + capture-list bookkeeping. */
		if (target_ref < 0x3800u && tgt_cd) {
			if (objects[ai.active_obj_idx].side != pstate.player->side) {
				fsfx_triggersfx(0x27, 0xFFFF);
			} else {
				if (!tgt_cd->inspected) {
					tgt_cd->inspected = 1;
					++fgstatus[objects[target_ref].fg_idx].cond[4].detail;
					if (fg_array[objects[target_ref].fg_idx].special_craft == tgt_cd->craft_idx_in_fg)
						fgstatus[objects[target_ref].fg_idx].cond_id[4].detail = 1;
				}
				fsfx_speakobjectname(ai.active_obj_idx, 0x33);
				/* Operation-completed voice cascade per binary 0x3C51A..0x3C58E.
				 * Same shape as phase 1 but uses verb 0x40 (completed) instead
				 * of 0x3F (acknowledged). */
				{
					uint8_t order_ldr = cd->default_order_ldr;
					uint8_t voice;
					if (order_ldr < 0x1F) {
						if (order_ldr >= 0x1C && order_ldr <= 0x1D) {
							voice = (target_ref == pstate.object_idx) ? 0x43 : 0x42;
							fsfx_speakoperation(voice, 0x40);
						} else if (order_ldr == 0x1E) {
							fsfx_speakoperation(0x41, 0x40);
						}
					} else if (order_ldr == 0x1F) {
						fsfx_speakoperation(0x44, 0x40);
					} else if (order_ldr < 0x22) {
						voice = (target_ref == pstate.object_idx) ? 0x43 : 0x42;
						fsfx_speakoperation(voice, 0x40);
					} else if (order_ldr == 0x22) {
						fsfx_speakoperation(0x41, 0x40);
					} else if (order_ldr == 68) {
						voice = (target_ref == pstate.object_idx) ? 0x43 : 0x42;
						fsfx_speakoperation(voice, 0x40);
					}
				}
			}
		}

		++cd->ai_goal_progress[cd->ai_state_1C];
		cd->capture_list[cd->capture_count] = target_idnumber;
		if (++cd->capture_count >= 10)
			cd->capture_count = 9;
		if (cd->capture_count == 1) {
			++fgstatus[ai.fg_idx].cond[6].detail;
			if (fg_array[ai.fg_idx].special_craft == cd->craft_idx_in_fg)
				fgstatus[ai.fg_idx].cond_id[6].detail = 1;
		}
		if (target_ref < 0x3800u && tgt_cd && !tgt_cd->board_count) {
			++fgstatus[tgt_fg_idx].cond[5].detail;
			if (fg_array[tgt_fg_idx].special_craft == tgt_cd->craft_idx_in_fg)
				fgstatus[tgt_fg_idx].cond_id[5].detail = 1;
		}

		cd->mode_subbyte = 3;
		cd->maneuver_timer = 2360;
		if (pstate.target_obj_idx == target_ref)
			lasttargetnum = -3;
		if (target_ref == pstate.object_idx)
			cd->pending_radio_command = 0xFFu;
		return 0;
	}

	/* Pre-timer: re-arm missiles / subsystems for order 28 when linked
	 * to the player craft. */
	if (cd->default_order_ldr != 28)
		return 0;
	if (cd->pending_radio_command != (uint8_t)pstate.object_idx)
		return 0;
	if (cd->ai_plan_state)
		return 0;

	/* Missile restock. */
	{
		uint16_t bank;
		const SpecData* tgt_sd = &spec_data[tgt_species];
		for (bank = 0; bank < tgt_cd->missile_group_cnt; ++bank) {
			if (!tgt_cd->warhead_type[bank])
				continue;
			{
				uint16_t missile_slot = tgt_sd->missile_start[bank];
				uint16_t missile_end = tgt_sd->missile_end[bank];
				for (; missile_slot <= missile_end; ++missile_slot) {
					uint16_t torp_used = (target_ref == pstate.object_idx)
											 ? mission.torp_used
											 : fg_array[objects[target_ref].fg_idx].warhead;
					uint32_t torp_cnt;

					if (bank == 1)
						torp_used = 5;
					/* Binary reads byte_C7AFB[bank + species*236] = +0x47 of the
					 * SpecData entry, which is missile_fire_mode (always BSS-zero
					 * — FEDISKIO_fillinspec never writes it). math2_fraction(0, ...)
					 * returns 0, then the !torp_cnt fallback below forces 1. */
					torp_cnt = math2_fraction(tgt_sd->missile_fire_mode[bank], warheadadjust[torp_used]);
					if (!torp_cnt)
						torp_cnt = 1;
					{
						uint8_t sf = fg_array[objects[target_ref].fg_idx].version;
						if (sf == 1)
							torp_cnt *= 2;
						else if (sf == 2)
							torp_cnt >>= 1;
					}
					if (!torp_cnt)
						torp_cnt = 1;
					if (target_ref == pstate.object_idx && tgt_species == (uint8_t)spec_getspecnum(0xC)) {
						if (torp_cnt > 99)
							torp_cnt = 99;
					} else if (torp_cnt > 9) {
						torp_cnt = 9;
					}
					/* Restore one round at a time and fully charge the launcher. */
					if (torp_cnt > tgt_cd->weapon_slots[missile_slot].ammo) {
						changed_flag = true;
						++tgt_cd->weapon_slots[missile_slot].ammo;
					}
					tgt_cd->weapon_slots[missile_slot].charge = 127;
				}
			}
		}
	}

	/* First missing capability bit. */
	{
		uint16_t bit = 1;
		for (uint16_t m = 0; m < 13; ++m) {
			if ((bit & tgt_cd->installed_subsystems) != 0) {
				if ((bit & tgt_cd->working_subsystems) == 0) {
					changed_flag = true;
					tgt_cd->working_subsystems |= bit;
					break;
				}
			}
			bit <<= 1;
		}
	}
	/* First missing status bit. */
	{
		uint16_t bit = 1;
		for (uint16_t m = 0; m < 10; ++m) {
			if ((bit & tgt_cd->subsystem_active) != 0) {
				if ((bit & tgt_cd->status_flags) == 0) {
					changed_flag = true;
					tgt_cd->status_flags |= bit;
					break;
				}
			}
			bit <<= 1;
		}
	}

	cd->ai_plan_state = 472;
	if (!changed_flag)
		return 0;
	cd->maneuver_timer = 1416;
	if (pstate.object_idx == target_ref && !replayviewmode)
		panel_initpanel();
	return 0;
}

/* Phase 3 — departure push and exit. */
static int16_t board_phase3(CraftData* cd, uint16_t target_ref) {
	if (!cd->maneuver_timer) {
		/* Binary 0x3C6E5 clears ai_target_ref to 0xFFFF on timer expiry so
		 * the next maneuver does not inherit the docking target. */
		cd->ai_target_ref = (int16_t)0xFFFFu;
		return 1;
	}

	if (target_ref >= 0x3800u) {
		cd->push_accum_x = 0;
		cd->push_accum_y = 0;
		cd->push_accum_z = 500;
		return 0;
	}

	pai_calcrotatedpoint(&objects[ai.active_obj_idx], 0, 0x4000, 0);
	cd->push_accum_x = rotatedx + objects[target_ref].world_x - objects[ai.active_obj_idx].world_x;
	cd->push_accum_y = rotatedy + objects[target_ref].world_y - objects[ai.active_obj_idx].world_y;
	cd->push_accum_z = rotatedz + objects[target_ref].world_z - objects[ai.active_obj_idx].world_z;
	return 0;
}

// FUNCTION: TIE 0x3B350
int16_t paiman_boardmaneuver(void) {
	CraftData* cd = craftptr;
	uint16_t target_ref = (uint16_t)cd->ai_target_ref;
	uint8_t dock_delay_var0 = fg_array[ai.fg_idx].ai[ai.ai_entry_count].var[0];
	uint8_t tgt_species = 0xFFu;
	uint8_t tgt_fg_idx;
	uint16_t target_idnumber;

	if (target_ref >= 0x3800u) {
		uint16_t sidx = target_ref - 14336u;
		tgt_fg_idx = staticobjects[sidx].fg_idx;
		target_idnumber = staticobjects[sidx].idnumber;
	} else {
		CraftData* tgt_cd = objects[target_ref].craft_ptr;
		tgt_species = tgt_cd->species_idx;
		tgt_fg_idx = objects[target_ref].fg_idx;
		target_idnumber = objects[target_ref].idnumber;
	}

	switch (cd->mode_subbyte) {
		case 0:
			return board_phase0(cd, target_ref, tgt_species);
		case 1:
			return board_phase1(cd, target_ref, tgt_species, tgt_fg_idx, dock_delay_var0);
		case 2:
			return board_phase2(cd, target_ref, tgt_species, tgt_fg_idx, target_idnumber);
		case 3:
			return board_phase3(cd, target_ref);
		default:
			return 0;
	}
}

/* ---- Dispatch tables ----------------------------------------------- */

const ManeuverFunc _initmanvrfunctionptrs[MODE_COUNT] = {
	(ManeuverFunc)paiorder_nullorder,                 /*  0 None          */
	(ManeuverFunc)paiman_initturninsidemaneuver,      /*  1 TurnInside    */
	(ManeuverFunc)paiman_initsplitsmaneuver,          /*  2 Splits        */
	(ManeuverFunc)paiman_initimmelmannmaneuver,       /*  3 Immelmann     */
	(ManeuverFunc)paiman_initscissorsmaneuver,        /*  4 Scissors      */
	(ManeuverFunc)paiman_initrendezvousmaneuver,      /*  5 Rendezvous    */
	(ManeuverFunc)paiman_initcruisemaneuver,          /*  6 Cruise        */
	(ManeuverFunc)paiman_initheadtowardfullmaneuver,  /* 7 HeadTowardFull */
	(ManeuverFunc)paiman_initrunawaymaneuver,         /*  8 RunAway       */
	(ManeuverFunc)paiman_initheadonattackmaneuver,    /* 9 HeadOnAttack */
	(ManeuverFunc)paiman_initfollowleadermaneuver,    /* 10 FollowLeader */
	(ManeuverFunc)paiman_initsetupattackmaneuver,     /* 11 SetupAttack  */
	(ManeuverFunc)paiman_initsetupattackmaneuver,     /* 12 Attack       */
	(ManeuverFunc)paiman_initzoommaneuver,            /* 13 Zoom          */
	(ManeuverFunc)paiman_initdivemaneuver,            /* 14 Dive          */
	(ManeuverFunc)paiman_initsplitsdivemaneuver,      /* 15 SplitsDive    */
	(ManeuverFunc)paiman_initspeedawaymaneuver,       /* 16 SpeedAway     */
	(ManeuverFunc)paiman_initescortmaneuver,          /* 17 Escort        */
	(ManeuverFunc)paiman_initboardmaneuver,           /* 18 Board         */
	(ManeuverFunc)paiman_initawaitboardmaneuver,      /* 19 AwaitBoard    */
	(ManeuverFunc)paiman_initheadtowardmaneuver,      /* 20 HeadToward    */
	(ManeuverFunc)paiman_initintohyperspacemaneuver,  /* 21 IntoHyperspace */
	(ManeuverFunc)paiman_initoutofhyperspacemaneuver, /* 22 OutOfHyperspace */
	(ManeuverFunc)paiman_initsetupattackmaneuver,     /* 23 AttackSecondary */
	(ManeuverFunc)paiman_initturnawaymaneuver,        /* 24 TurnAway      */
	(ManeuverFunc)paiman_initawaitboardmaneuver,      /* 25 AwaitBoardAlt */
	(ManeuverFunc)paiman_initoutofhangarmaneuver,     /* 26 OutOfHangar   */
	(ManeuverFunc)paiman_initsplitsdivemaneuver,      /* 27 SplitsDiveAlt */
	(ManeuverFunc)paiman_initavoidstarshipmaneuver,   /* 28 AvoidStarship */
	(ManeuverFunc)paiman_initwaitmaneuver,            /* 29 Wait          */
	(ManeuverFunc)paiman_initawaitboardmaneuver,      /* 30 DropOff       */
};

const ManeuverFunc _manvrfunctionptrs[MODE_COUNT] = {
	(ManeuverFunc)paiorder_nullorder,             /*  0 None          */
	(ManeuverFunc)paiman_turninsidemaneuver,      /*  1 */
	(ManeuverFunc)paiman_splitsmaneuver,          /*  2 */
	(ManeuverFunc)paiman_immelmannmaneuver,       /*  3 */
	(ManeuverFunc)paiman_scissorsmaneuver,        /*  4 */
	(ManeuverFunc)paiman_rendezvousmaneuver,      /*  5 */
	(ManeuverFunc)paiman_cruisemaneuver,          /*  6 */
	(ManeuverFunc)paiman_headtowardfullmaneuver,  /*  7 */
	(ManeuverFunc)paiman_runawaymaneuver,         /*  8 */
	(ManeuverFunc)paiman_headonattackmaneuver,    /*  9 */
	(ManeuverFunc)paiman_followleadermaneuver,    /* 10 */
	(ManeuverFunc)paiman_setupattackmaneuver,     /* 11 */
	(ManeuverFunc)paiman_attackmaneuver,          /* 12 */
	(ManeuverFunc)paiman_zoommaneuver,            /* 13 */
	(ManeuverFunc)paiman_zoommaneuver,            /* 14 Dive = Zoom body */
	(ManeuverFunc)paiman_splitsdivemaneuver,      /* 15 */
	(ManeuverFunc)paiman_speedawaymaneuver,       /* 16 */
	(ManeuverFunc)paiman_escortmaneuver,          /* 17 */
	(ManeuverFunc)paiman_boardmaneuver,           /* 18 */
	(ManeuverFunc)paiman_awaitboardmaneuver,      /* 19 */
	(ManeuverFunc)paiman_headtowardmaneuver,      /* 20 */
	(ManeuverFunc)paiman_intohyperspacemaneuver,  /* 21 */
	(ManeuverFunc)paiman_outofhyperspacemaneuver, /* 22 */
	(ManeuverFunc)paiman_attackmaneuver,          /* 23 AttackSecondary */
	(ManeuverFunc)paiman_turnawaymaneuver,        /* 24 */
	(ManeuverFunc)paiman_awaitboardmaneuver,      /* 25 */
	(ManeuverFunc)paiman_outofhangarmaneuver,     /* 26 */
	(ManeuverFunc)paiman_splitsdivemaneuver,      /* 27 */
	(ManeuverFunc)paiman_avoidstarshipmaneuver,   /* 28 */
	(ManeuverFunc)paiman_avoidstarshipmaneuver,   /* 29 Wait = null */
	(ManeuverFunc)paiman_dropoffmaneuver,         /* 30 */
};

/* ---- Public entry points ------------------------------------------ */

// FUNCTION: TIE 0x396B0
void paiman_initmaneuver(void) {
	CraftData* cd = craftptr;

	cd->push_accum_x = 0;
	cd->hit_count = 0;
	cd->mode_subbyte = 0;
	cd->push_accum_y = 0;
	cd->push_accum_z = 0;

	_initmanvrfunctionptr = _initmanvrfunctionptrs[cd->mode_byte];
	(void)_initmanvrfunctionptr();
}

int16_t paiman_updatemaneuver(void) {
	_manvrfunctionptr = _manvrfunctionptrs[craftptr->mode_byte];
	return _manvrfunctionptr();
}
