#include "tie/paiman.h"
#include "tie/create.h"
#include "tie/draw.h"
#include "tie/edition.h"
#include "tie/fsfx.h"
#include "tie/laser.h"
#include "tie/math2.h"
#include "tie/mission.h"
#include "tie/modelbounds.h"
#include "tie/modelmesh.h"
#include "tie/msg.h"
#include "tie/msg_templates.h"
#include "tie/pai.h"
#include "tie/paiorder.h"
#include "tie/panel.h"
#include "tie/score.h"
#include "tie/spec.h"
#include "tie/tie.h"
#include "tie/trig2.h"
#include "tie_runtime/diagnostics/flight_trace.h"
#include "tie_runtime/timing/ai_lead.h"

#include <stdint.h>
#include <stdlib.h>

/* ---- External globals referenced by PAIMAN ------------------------- */

/* ai (AiContext) is declared in pai.h — 52-byte PAI tick-state block.
 * Board/dropoff snapshot-restore uses plain struct-assignment because
 * ai IS the contiguous 52-byte region (matches the binary's qmemcpy). */

/* ---- Maneuver data tables ------------------------------------------ */

/* 13 formations × 6 slots each = 78 unit offsets per axis.
 * formposz has a 79th trailing zero preserved for byte-exact layout. */
// GLOBAL: TIE95 0xC5A86
// GLOBAL: TIE98 0x4E6578
const int16_t _formposx[13][6] = {
	0, 1, -1, 2, -2, 3,  0,  -2, 4,  5,  -4, -5, 0,  0,  0, 0,  0, 0, 0, 1,  -1, 2, -2, 3, 0, 1,
	2, 3, 4,  5, 0,  -1, -2, -3, -4, -5, 0,  -1, 0,  -1, 0, -1, 0, 1, 0, -1, 0,  0, 0,  0, 0, 0,
	0, 0, 0,  1, -1, 1,  -1, 0,  0,  1,  -1, 2,  -2, 3,  0, 0,  0, 0, 0, 0,  0,  0, 0,  0, 0, 0,
};

// GLOBAL: TIE95 0xC5B22
// GLOBAL: TIE98 0x4E6618
const int16_t _formposy[13][6] = {
	0,  -1, -1, -2, -2, -3, 0,  -2, -4, -5, -4, -5, 0,  -1, -2, -3, -4, -5, 0,  0,  0,  0,  0, 0, 0, -1,
	-2, -3, -4, -5, 0,  -1, -2, -3, -4, -5, 0,  0,  -1, -1, -2, -2, 0,  -1, -2, -1, -1, -1, 0, 0, 0, 0,
	0,  0,  0,  0,  0,  0,  0,  -1, 0,  1,  1,  2,  2,  3,  0,  -1, -1, -2, -2, -3, 0,  1,  1, 2, 2, 3,
};

// GLOBAL: TIE95 0xC5BBE
// GLOBAL: TIE98 0x4E66B8
const int16_t _formposz[13][6] = {
	0, 0, 0, 0, 0, 0,  0,  -1, 2, 1, 3, 2, 0, 1, 2, 3, 4,  5, 0,  0, 0, 0,  0,  0, 0,  0,
	0, 0, 0, 0, 0, 0,  0,  0,  0, 0, 0, 0, 0, 0, 0, 0, 0,  0, 0,  0, 1, -1, 0,  1, 2,  3,
	4, 5, 0, 1, 1, -1, -1, 0,  0, 0, 0, 0, 0, 0, 0, 1, -1, 2, -2, 3, 0, 1,  -1, 2, -2, 3,
};

// GLOBAL: TIE95 0xC58BE
// GLOBAL: TIE98 0x4E63A0
const int16_t _escortsidepos[27] = {
	-3072, 0,     3072, -3072, 0,     3072, -3072, 0,     3072, -3072, 0,     3072, -3072, 0,
	3072,  -3072, 0,    3072,  -3072, 0,    3072,  -3072, 0,    3072,  -3072, 0,    3072,
};

// GLOBAL: TIE95 0xC58F4
// GLOBAL: TIE98 0x4E63D8
const int16_t _escortuppos[27] = {
	3072, 3072, 3072, 3072, 3072,  3072,  3072,  3072,  3072,  0,     0,     0,     0,     0,
	0,    0,    0,    0,    -3072, -3072, -3072, -3072, -3072, -3072, -3072, -3072, -3072,
};

// GLOBAL: TIE95 0xC592A
// GLOBAL: TIE98 0x4E6410
const int16_t _escortfwdpos[27] = {
	3072, 3072,  3072,  0,     0,    0,    -3072, -3072, -3072, 3072, 3072,  3072,  0,     0,
	0,    -3072, -3072, -3072, 3072, 3072, 3072,  0,     0,     0,    -3072, -3072, -3072,
};

/* DOS throttle presets indexed by EAIStruct.speed. */
// GLOBAL: TIE95 0xC5960
const uint16_t throttleconvert[12] = {
	0x0000, 0x1999, 0x3334, 0x4CCE, 0x6668, 0x8000, 0x999A, 0xB334, 0xCCCE, 0xE668, 0xFFFF, 0xF400,
};

/* Hyperspace-exit speed ladder; the maneuver clamps its phase to 8. */
// GLOBAL: TIE95 0xC5A70
// GLOBAL: TIE98 0x4E6560
const uint16_t stagevel[11] = {
	0x0E10, 0x0E10, 0x0E10, 0x0E10, 0x0E10, 0x0E10, 0x0708, 0x0708, 0x0708, 0x0384, 0x0384,
};

/* Per-skill hold time (pre-236x scaling) between turn-inside/turn-away
 * re-picks. Skill tiers: 0=novice, 1=veteran, 2=ace. */
// GLOBAL: TIE95 0xC58B8
// GLOBAL: TIE98 0x4E6398
const uint16_t delayturninside[3] = { 9, 6, 3 };

/* Last-selected initializer and runtime maneuver. */
// GLOBAL: TIE95 0xD5068
ManeuverInitFunc initmanvrfunctionptr = 0;
// GLOBAL: TIE95 0xD506C
ManeuverFunc _manvrfunctionptr = 0;

/* ---- Dispatch tables ----------------------------------------------- */

// GLOBAL: TIE95 0xC59F4
const ManeuverInitFunc initmanvrfunctionptrs[MODE_COUNT] = {
	(ManeuverInitFunc)paiorder_nullorder, /*  0 None          */
	paiman_initturninsidemaneuver,        /*  1 TurnInside    */
	paiman_initsplitsmaneuver,            /*  2 Splits        */
	paiman_initimmelmannmaneuver,         /*  3 Immelmann     */
	paiman_initscissorsmaneuver,          /*  4 Scissors      */
	paiman_initrendezvousmaneuver,        /*  5 Rendezvous    */
	paiman_initcruisemaneuver,            /*  6 Cruise        */
	paiman_initheadtowardfullmaneuver,    /* 7 HeadTowardFull */
	paiman_initrunawaymaneuver,           /*  8 RunAway       */
	paiman_initheadonattackmaneuver,      /* 9 HeadOnAttack */
	paiman_initfollowleadermaneuver,      /* 10 FollowLeader */
	paiman_initsetupattackmaneuver,       /* 11 SetupAttack  */
	paiman_initsetupattackmaneuver,       /* 12 Attack       */
	paiman_initzoommaneuver,              /* 13 Zoom          */
	paiman_initdivemaneuver,              /* 14 Dive          */
	paiman_initsplitsdivemaneuver,        /* 15 SplitsDive    */
	paiman_initspeedawaymaneuver,         /* 16 SpeedAway     */
	paiman_initescortmaneuver,            /* 17 Escort        */
	paiman_initboardmaneuver,             /* 18 Board         */
	paiman_initawaitboardmaneuver,        /* 19 AwaitBoard    */
	paiman_initheadtowardmaneuver,        /* 20 HeadToward    */
	paiman_initintohyperspacemaneuver,    /* 21 IntoHyperspace */
	paiman_initoutofhyperspacemaneuver,   /* 22 OutOfHyperspace */
	paiman_initsetupattackmaneuver,       /* 23 AttackSecondary */
	paiman_initturnawaymaneuver,          /* 24 TurnAway      */
	paiman_initawaitboardmaneuver,        /* 25 AwaitBoardAlt */
	paiman_initoutofhangarmaneuver,       /* 26 OutOfHangar   */
	paiman_initsplitsdivemaneuver,        /* 27 SplitsDiveAlt */
	paiman_initavoidstarshipmaneuver,     /* 28 AvoidStarship */
	paiman_initwaitmaneuver,              /* 29 Wait          */
	paiman_initawaitboardmaneuver,        /* 30 DropOff       */
};
// GLOBAL: TIE95 0xC5978
const ManeuverFunc _manvrfunctionptrs[MODE_COUNT] = {
	paiorder_nullorder,             /*  0 None          */
	paiman_turninsidemaneuver,      /*  1 */
	paiman_splitsmaneuver,          /*  2 */
	paiman_immelmannmaneuver,       /*  3 */
	paiman_scissorsmaneuver,        /*  4 */
	paiman_rendezvousmaneuver,      /*  5 */
	paiman_cruisemaneuver,          /*  6 */
	paiman_headtowardfullmaneuver,  /*  7 */
	paiman_runawaymaneuver,         /*  8 */
	paiman_headonattackmaneuver,    /*  9 */
	paiman_followleadermaneuver,    /* 10 */
	paiman_setupattackmaneuver,     /* 11 */
	paiman_attackmaneuver,          /* 12 */
	paiman_zoommaneuver,            /* 13 */
	paiman_zoommaneuver,            /* 14 Dive = Zoom body */
	paiman_splitsdivemaneuver,      /* 15 */
	paiman_speedawaymaneuver,       /* 16 */
	paiman_escortmaneuver,          /* 17 */
	paiman_boardmaneuver,           /* 18 */
	paiman_awaitboardmaneuver,      /* 19 */
	paiman_headtowardmaneuver,      /* 20 */
	paiman_intohyperspacemaneuver,  /* 21 */
	paiman_outofhyperspacemaneuver, /* 22 */
	paiman_attackmaneuver,          /* 23 AttackSecondary */
	paiman_turnawaymaneuver,        /* 24 */
	paiman_awaitboardmaneuver,      /* 25 */
	paiman_outofhangarmaneuver,     /* 26 */
	paiman_splitsdivemaneuver,      /* 27 */
	paiman_avoidstarshipmaneuver,   /* 28 */
	paiman_avoidstarshipmaneuver,   /* 29 Wait = null */
	paiman_dropoffmaneuver,         /* 30 */
};
/* ---- Public entry points ------------------------------------------ */

// FUNCTION: TIE95 0x396B0
void paiman_initmaneuver(void) {
	CraftData* cd = craftptr;

	cd->push_accum_x = 0;
	cd->hit_count = 0;
	cd->mode_subbyte = 0;
	cd->push_accum_y = cd->push_accum_x;
	cd->push_accum_z = cd->push_accum_x;

	initmanvrfunctionptr = initmanvrfunctionptrs[cd->mode_byte];
#ifdef TIE_MODERN
	/* The shared zero-return entry has a different prototype from the initializers. */
	if (initmanvrfunctionptr == (ManeuverInitFunc)paiorder_nullorder) {
		((ManeuverFunc)initmanvrfunctionptr)();
		return;
	}
#endif
	initmanvrfunctionptr();
}

/* ---- MODE_None (slot 0) — nullmaneuver stub ------------------------ */

// FUNCTION: TIE95 0x396F8
int16_t paiman_nullmaneuver(void) { return 0; }

/* ---- MODE_TurnInside (1) ------------------------------------------- */

// FUNCTION: TIE95 0x396FC
void paiman_initturninsidemaneuver(void) {
	paiman_setnewturninside(ai.active_obj_idx);
	craftptr->maneuver_timer = 3540;
}

// FUNCTION: TIE95 0x39718
int16_t paiman_turninsidemaneuver(void) {
	if (!craftptr->maneuver_timer)
		return 1;
	if (!craftptr->ai_plan_state)
		paiman_setnewturninside(ai.active_obj_idx);
	return 0;
}

/* Pick a new turn-inside target orientation and hold-timer. */
// FUNCTION: TIE95 0x39740
void paiman_setnewturninside(uint16_t own_obj_idx) {
	int attacker_idx = craftptr->attacker_idx;

	/* Face away from the attacker (or from our own heading when unthreatened). */
	if (attacker_idx != 0xFF)
		craftptr->ai_target_heading = (uint16_t)(objects[attacker_idx].heading + 0x8000u);
	else
		craftptr->ai_target_heading = (uint16_t)(objects[own_obj_idx].heading + 0x8000u);

	paiman_setturn((craftptr->skill_value >> 1) + 0x8000);

	craftptr->ai_plan_state = (uint16_t)(236u * delayturninside[(uint16_t)ai.skill_tier]);
}

/* ---- MODE_Splits (2) ----------------------------------------------- */

// FUNCTION: TIE95 0x397C0
void paiman_initsplitsmaneuver(void) {
	CraftData* cd = craftptr;
	cd->ai_roll_state = 1;
	cd->ai_roll_step = 0xFFFFu;
	cd->ai_target_roll = 0x8000;
	cd->ai_heading_state = 0;
	cd->ai_pitch_force = 1;
	cd->ai_target_pitch = 0x4000;
	cd->ai_pitch_state = 2;
	cd->ai_pitch_step = 0xFFFFu;
}

// FUNCTION: TIE95 0x397F8
int16_t paiman_splitsmaneuver(void) {
	return (craftptr->ai_roll_state == 4 && craftptr->ai_pitch_state == 3) ? 1 : 0;
}

/* ---- MODE_Immelmann (3) -------------------------------------------- */

// FUNCTION: TIE95 0x39820
void paiman_initimmelmannmaneuver(void) {
	CraftData* cd = craftptr;
	int32_t cur_pitch;

	cd->throttle_speed = 0xFFFFu;
	cd->ai_roll_state = 1;
	cd->ai_roll_step = 0xFFFFu;
	cd->ai_target_roll = 0;
	cd->ai_heading_state = 0;
	cd->ai_target_pitch = 0x4000;
	cd->ai_pitch_step = 0xFFFFu;
	cur_pitch = cd->orient_pitch;
	cd->ai_pitch_force = 0;

	if (cur_pitch < 0x4000)
		cd->ai_pitch_state = 2;
	else if (cur_pitch > 0x4000)
		cd->ai_pitch_state = 1;
	else
		cd->ai_pitch_state = 3;
	craftptr->maneuver_timer = 0;
}

// FUNCTION: TIE95 0x39898
int16_t paiman_immelmannmaneuver(void) {
	CraftData* cd = craftptr;

	switch (cd->mode_subbyte) {
		case 0:
			if (cd->ai_pitch_state == 3) {
				cd->ai_pitch_state = 1;
				cd->ai_pitch_step = 0xFFFFu;
				cd->ai_pitch_force = 1;
				cd->ai_target_pitch = 0x4000;
				cd->mode_subbyte = 1;
			}
			return 0;
		case 1:
			if (cd->ai_pitch_state == 3) {
				cd->ai_roll_state = 1;
				cd->ai_roll_step = 0xFFFFu;
				cd->ai_target_roll = 0;
				cd->ai_heading_state = 0;
				cd->mode_subbyte = 2;
			}
			return 0;
		case 2:
			if (cd->ai_roll_state == 4 && cd->ai_pitch_state == 3)
				return 1;
			return 0;
	}
	return 0;
}

/* ---- MODE_Scissors (4) --------------------------------------------- */

// FUNCTION: TIE95 0x39930
void paiman_initscissorsmaneuver(void) {
	int attacker_idx = craftptr->attacker_idx;

	if (attacker_idx != 0xFF)
		craftptr->ai_target_heading = (uint16_t)(objects[attacker_idx].heading + 0x8000u);
	else
		craftptr->ai_target_heading = (uint16_t)(objects[ai.active_obj_idx].heading + 0x8000u);
	paiman_setturn((craftptr->skill_value >> 1) + 0x8000);

	craftptr->ai_roll_state = 3;
	craftptr->ai_roll_step = 0xFFFFu;
	craftptr->ai_target_roll = (uint16_t)math2_getrandom();
	craftptr->maneuver_timer = 4720;
	craftptr->ai_plan_state = 472;
}

// FUNCTION: TIE95 0x399C4
int16_t paiman_scissorsmaneuver(void) {
	CraftData* cd = craftptr;

	if (!cd->maneuver_timer) {
		cd->ai_roll_state = 4;
		return 1;
	}
	if (!cd->ai_plan_state) {
		uint16_t flip_heading = cd->ai_target_heading;

		cd->ai_heading_state = 1;
		cd->ai_target_heading = (uint16_t)(flip_heading + 0x8000u);
		cd->ai_target_roll ^= 0x8000u;
		cd->ai_plan_state = 944;
	}
	return 0;
}

/* ---- MODE_Rendezvous (5) ------------------------------------------- */

// FUNCTION: TIE95 0x39A14
void paiman_initrendezvousmaneuver(void) {
	uint16_t t;

	paiman_setflighttotarget(0, 1);
	t = throttleconvert[fg_array[ai.fg_idx].ai[ai.ai_entry_count].speed];
	if (!t)
		t = 0xFFFF; /* retail: 0 -> full throttle, never stopped */
	craftptr->throttle_speed = t;
}

// FUNCTION: TIE95 0x39A7C
int16_t paiman_rendezvousmaneuver(void) {
	uint16_t t;

	paiman_setflighttotarget(0, 1);
	t = throttleconvert[fg_array[ai.fg_idx].ai[ai.ai_entry_count].speed];
	if (!t)
		t = 0xFFFF;
	craftptr->throttle_speed = t;
	return 0;
}

/* ---- MODE_Cruise (6) ----------------------------------------------- */

// FUNCTION: TIE95 0x39AE4
void paiman_initcruisemaneuver(void) {
	CraftData* cd = craftptr;

	paiman_controlplane();
	if ((uint16_t)objects[ai.active_obj_idx].roll < 0x8000)
		paiman_setflighttotarget(0, 1);

	cd->ai_plan_state = 236;
	cd->throttle_speed = throttleconvert[fg_array[ai.fg_idx].ai[ai.ai_entry_count].speed];
}

// FUNCTION: TIE95 0x39B78
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
				cd->ai_target_pitch = (uint16_t)trig2_zangle;
				cd->throttle_speed = 0xC000u; /* -16384 as uint */
				cd->ai_pitch_state = (uint8_t)((cd->ai_target_pitch > cd->orient_pitch) + 1);
			}
		}
		paiman_setflighttotarget(0, 1);
		cd->ai_plan_state = 236;
		if (cd->ai_heading_state == 3 && objects[ai.active_obj_idx].roll) {
			cd->ai_roll_state = 1;
			cd->ai_roll_step = 0xFFFFu;
			cd->ai_target_roll = 0;
		}
	}

	speed = fg_array[ai.fg_idx].ai[ai.ai_entry_count].speed;

	/* For starships, damp throttle during an active heading turn to tighten
	 * the turn; otherwise pipe speed through throttleconvert. */
	if (objects[ai.active_obj_idx].genus == 4) {
		uint16_t hd = (uint16_t)(objects[ai.active_obj_idx].heading - cd->ai_target_heading);
		if (hd >= 0x8000u)
			hd = (uint16_t)-hd;
		if (cd->ai_heading_state == 2 && hd >= 0x1000u) {
			cd->throttle_speed = 0;
			return 0;
		}
	}
	cd->throttle_speed = throttleconvert[speed];
	return 0;
}

/* Advance to the next waypoint in a cruise/patrol cycle. */
// FUNCTION: TIE95 0x39D74
void paiman_gonextwaypoint(void) {
	uint8_t order_ldr;
	int next_idx;

	next_idx = (uint8_t)++craftptr->active_waypoint_idx;
	order_ldr = craftptr->default_order_ldr;
	if (next_idx > 11 || !fg_array[ai.fg_idx].way_used[next_idx]) {
		craftptr->active_waypoint_idx = 4;
		if (order_ldr == 3 || order_ldr == 5 || order_ldr == 56)
			++craftptr->ai_goal_progress[ai.ai_entry_count];
	}
	craftptr->ai_target_ref = (uint16_t)((uint16_t)craftptr->active_waypoint_idx + 0x8000);
	pai_settarget();
}

/* ---- MODE_HeadTowardFull (7) --------------------------------------- */

// FUNCTION: TIE95 0x39DFC
void paiman_initheadtowardfullmaneuver(void) {
	paiman_setflighttotarget(0, 0);
	craftptr->throttle_speed = 0xFFFFu;
	craftptr->ai_plan_state = 1180;
}

// FUNCTION: TIE95 0x39E1C
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

// FUNCTION: TIE95 0x39E50
void paiman_initrunawaymaneuver(void) {
	paiman_controlplane();
	if ((uint16_t)objects[ai.active_obj_idx].roll < 0x8000)
		paiman_setflighttotarget(0x8000, 1);
}

// FUNCTION: TIE95 0x39E94
int16_t paiman_runawaymaneuver(void) {
	paiman_setflighttotarget(0x8000, 1);
	craftptr->throttle_speed = 0xFFFFu;
	return 0;
}

/* ---- MODE_HeadOnAttack (9) ----------------------------------------- */

// FUNCTION: TIE95 0x39EB8
void paiman_initheadonattackmaneuver(void) {
	craftptr->ai_target_ref = (int16_t)craftptr->attacker_idx;
	paiman_setflighttotarget(0, 1);
	craftptr->throttle_speed = 0xFFFFu;
	craftptr->maneuver_timer = 1888;
}

// FUNCTION: TIE95 0x39EEC
int16_t paiman_headonattackmaneuver(void) {
	if (!craftptr->maneuver_timer)
		return 1;
	paiman_setflighttotarget(0, 1);
	craftptr->throttle_speed = 0xFFFFu;
	return 0;
}

/* ---- MODE_FollowLeader (10) ---------------------------------------- */

// FUNCTION: TIE95 0x39F1C
void paiman_initfollowleadermaneuver(void) {}

// FUNCTION: TIE95 0x39F20
int16_t paiman_followleadermaneuver(void) {
	uint16_t self_idx = ai.active_obj_idx;
	uint8_t leader_idx = craftptr->leader_obj_idx;

#ifdef TIE_MODERN
	/* A newly promoted craft can retain this maneuver until its next plan update. */
	if (leader_idx != 0xFF)
#endif
	{
		pai_distancebetween(leader_idx, self_idx);

		/* Far from leader: teleport waypoint + full throttle chase. */
		if (trig2_polardistance > 0x10000) {
			craftptr->waypoint_x_cache = objects[leader_idx].world_x;
			craftptr->waypoint_y_cache = objects[leader_idx].world_y;
			craftptr->waypoint_z_cache = objects[leader_idx].world_z;
			paiman_setflighttotarget(0, 1);
			craftptr->throttle_speed = 0xFFFFu;
			craftptr->push_accum_x = 0;
			craftptr->push_accum_y = 0;
			craftptr->push_accum_z = 0;
		} else {
			uint16_t delta;

			/* Match leader's heading. Leader actively turning → use their target;
			 * otherwise match their current heading. Skip a turn if we're already
			 * aligned. */
			if (ai.leader_craft->ai_heading_state == 2) {
				craftptr->ai_target_heading = ai.leader_craft->ai_target_heading;
				paiman_setturn((craftptr->skill_value >> 3) + 0x4000);
			} else if (objects[leader_idx].heading != objects[self_idx].heading) {
				craftptr->ai_target_heading = (uint16_t)objects[leader_idx].heading;
				paiman_setturn((craftptr->skill_value >> 3) + 0x4000);
			}

			/* Adjust player-led throttle from the speed difference; mirror NPC throttle. */
			if (leader_idx == pstate.object_idx) {
				uint16_t leader_speed = (uint16_t)objects[leader_idx].current_speed;
				uint16_t our_speed = (uint16_t)objects[self_idx].current_speed;
				uint16_t previous_throttle;

				/* Retail narrows the scaled delta and result before detecting wrap. */
				if (leader_speed > our_speed) {
					previous_throttle = craftptr->throttle_speed;
					craftptr->throttle_speed =
						(uint16_t)(previous_throttle + (leader_speed - our_speed) * 50);
					if (previous_throttle > craftptr->throttle_speed)
						craftptr->throttle_speed = 0xFFFFu;
				} else if (leader_speed < our_speed) {
					previous_throttle = craftptr->throttle_speed;
					craftptr->throttle_speed =
						(uint16_t)(previous_throttle - (our_speed - leader_speed) * 50);
					if (previous_throttle < craftptr->throttle_speed)
						craftptr->throttle_speed = 0;
				}
			} else {
				craftptr->throttle_speed = ai.leader_craft->throttle_speed;
			}

			/* Match pitch (snap if close, else short-way). */
			delta = craftptr->orient_pitch - ai.leader_craft->orient_pitch;
			if (delta >= 0x8000)
				delta = -delta;
			if (delta < 0x400) {
				craftptr->ai_pitch_state = 0;
				craftptr->orient_pitch = ai.leader_craft->orient_pitch;
			} else {
				craftptr->ai_pitch_step = 0xFFFFu;
				craftptr->ai_target_pitch = ai.leader_craft->orient_pitch;
				craftptr->ai_pitch_force = 0;
				craftptr->ai_pitch_state =
					(uint8_t)((ai.leader_craft->orient_pitch > craftptr->orient_pitch) + 1);
			}

			/* Match roll — but only once leader's spin has settled. */
			if (ai.leader_craft->spin_done_flag == 0xFFFFu) {
				delta = objects[self_idx].roll - objects[ai.leader_obj_idx].roll;
				if (delta >= 0x8000)
					delta = -delta;
				if (delta < 0x400) {
					objects[self_idx].roll = objects[ai.leader_obj_idx].roll;
					objects[self_idx].orient_dirty = 1;
					craftptr->ai_roll_state = 0;
				} else {
					craftptr->ai_roll_step = 0xFFFFu;
					craftptr->ai_roll_state = 1;
					craftptr->ai_target_roll = (uint16_t)objects[ai.leader_obj_idx].roll;
				}
			}

			/* Formation follow. Skip (hold position) when our leader is the
			 * player and is nearly stationary. */
			if (pstate.object_idx != craftptr->leader_obj_idx) {
				paiman_calcformation();
			} else if ((uint16_t)pstate.player->current_speed > 10) {
				paiman_calcformation();
			} else {
				craftptr->push_accum_x = 0;
				craftptr->push_accum_y = 0;
				craftptr->push_accum_z = 0;
			}
		}
	}

	return 0;
}

/* ---- MODE_SetupAttack (11), MODE_Attack (12), MODE_AttackSecondary (23) */

// FUNCTION: TIE95 0x3A2AC
void paiman_initsetupattackmaneuver(void) {
	craftptr->throttle_speed = 0xFFFFu;
	paiman_attacktarget(0);
}

/* Core attack-target helper. Picks static-vs-live waypoint resolution,
 * computes angle, requests a 180°-class turn (with inverted-upright
 * special case), drives pitch. */
// FUNCTION: TIE95 0x3A2BC
void paiman_attacktarget(int16_t heading_bias) {
	uint16_t self_idx = ai.active_obj_idx;
	int target_ref;
	int32_t dx, dy, dz;
	uint16_t heading_delta;
	CraftData* cd;

	if (craftptr->mode_byte == MODE_AttackSecondary ||
		(target_ref = (uint16_t)craftptr->ai_target_ref) >= 0x3800)
		pai_settarget();
	else
		paiman_calcplanelead(target_ref);

	dx = craftptr->waypoint_x_cache - objects[self_idx].world_x;
	dy = craftptr->waypoint_y_cache - objects[self_idx].world_y;
	dz = craftptr->waypoint_z_cache - objects[self_idx].world_z;
	trig2_ctop(dx, dy, dz);
	craftptr->ai_target_heading = trig2_xyangle + heading_bias;

	if (craftptr->ai_roll_state != 2) {
		paiman_setturn((craftptr->skill_value >> 1) + 0x8000);
	} else {
		heading_delta = objects[self_idx].heading - craftptr->ai_target_heading;
		if (heading_delta > 0x8000)
			heading_delta = -heading_delta;
		if (heading_delta >= 0x2000 || trig2_polardistance < 0x10000)
			paiman_setturn((craftptr->skill_value >> 1) + 0x8000);
	}

	if (craftptr->ai_roll_state == 3)
		craftptr->ai_roll_state = 0;

	cd = craftptr;
	if ((uint16_t)trig2_zangle != cd->orient_pitch) {
		cd->ai_pitch_step = 0xFFFFu;
		cd->throttle_speed = 0xFFFFu;
		cd->ai_climb_state = 0;
		cd->ai_target_pitch = trig2_zangle;
		cd->ai_dive_state = 0;
		cd->ai_pitch_state = (cd->ai_target_pitch > cd->orient_pitch) + 1;
		cd->ai_pitch_force = 0;
	}
}

// FUNCTION: TIE95 0x3A2FC
int16_t paiman_setupattackmaneuver(void) {
	uint16_t heading_delta;

	paiman_attacktarget(0);
	heading_delta = (uint16_t)(objects[ai.active_obj_idx].heading - craftptr->ai_target_heading);
	if (heading_delta >= 0x3000u && heading_delta <= 0xD000u)
		craftptr->throttle_speed = 0x8000;
	else
		craftptr->throttle_speed = 0xFFFFu;
	return 0;
}

// FUNCTION: TIE95 0x3A364
int16_t paiman_attackmaneuver(void) {
	uint16_t heading_delta;
	int32_t break_radius;
	uint16_t self_idx = ai.active_obj_idx;
	uint16_t pitch_delta;
	int16_t pitch_rnd;
	int16_t jink;
	int timer_base;
	CraftData* cd;

	switch (craftptr->mode_subbyte) {
		case 0:
			/* In approach. Compute the "breaking off" threshold by target
			 * class and our approach geometry. */
			pai_distancebetween(self_idx, (uint16_t)craftptr->ai_target_ref);
			if ((uint16_t)craftptr->ai_target_ref >= NUM_CRAFTS) {
				/* Static/warhead target: fighter base radius with no
				 * alignment doubling. */
				break_radius = 5120;
			} else if (objects[(uint16_t)craftptr->ai_target_ref].genus == 4 ||
					   objects[(uint16_t)craftptr->ai_target_ref].genus == 5 ||
					   objects[(uint16_t)craftptr->ai_target_ref].genus == 3) {
				/* Capital ship: threshold 0x2000 only when the approach angle
				 * is off-axis (0x2800..0x5800). */
				heading_delta =
					(uint16_t)(trig2_xyangle - objects[(uint16_t)craftptr->ai_target_ref].heading);
				break_radius = 0x2000;
				if (heading_delta >= 0x8000)
					heading_delta = -heading_delta;
				if (heading_delta < 0x2800 || heading_delta > 0x5800)
					break_radius *= 2;
			} else {
				/* Fighter: threshold 5120 only when in heading and pitch
				 * alignment. */
				heading_delta = (uint16_t)(objects[self_idx].heading -
										   objects[(uint16_t)craftptr->ai_target_ref].heading);
				break_radius = 5120;
				if (heading_delta >= 0x8000)
					heading_delta = -heading_delta;
				pitch_delta =
					(uint16_t)(objects[self_idx].pitch - objects[(uint16_t)craftptr->ai_target_ref].pitch);
				if (pitch_delta >= 0x8000)
					pitch_delta = -pitch_delta;
				if (pitch_delta > 0x4000 || heading_delta > 0x4000)
					break_radius *= 2;
			}

			if (break_radius > trig2_polardistance ||
				craftptr->hit_count >= spec_data[craftptr->species_idx].evade_hit_threshold) {
				/* Break off with a random heading jink of 0x3000..0x6FFF. */
				jink = (math2_getrandom() & 0x3FFF) + 0x3000;
				if ((uint16_t)math2_getrandom() >= 0x8000)
					jink = -jink;
				craftptr->ai_target_heading = objects[self_idx].heading + jink;
				paiman_setturn((craftptr->skill_value >> 1) + 0x8000);
				pitch_rnd = math2_getrandom() & 0x7FFF;

				cd = craftptr;
				cd->ai_climb_state = 0;
				cd->ai_dive_state = 0;
				cd->ai_pitch_force = 0;
				cd->ai_pitch_step = 0xFFFFu;
				cd->ai_target_pitch = pitch_rnd;
				cd->missile_count = 0;
				cd->ai_pitch_state = (uint8_t)((cd->ai_target_pitch > cd->orient_pitch) + 1);
				cd->missile_count_total = 0;
				cd->throttle_speed = 0xFFFFu;

				/* Timer: 20..27 s against capitals, 2..5 s against fighters.
				 * Static/warhead targets use the fighter range. */
				if ((uint16_t)cd->ai_target_ref >= NUM_CRAFTS) {
					craftptr->maneuver_timer = 236 * (((uint16_t)(uint8_t)math2_getrandom() & 3) + 2);
				} else {
					if (objects[(uint16_t)cd->ai_target_ref].genus == 4 ||
						objects[(uint16_t)cd->ai_target_ref].genus == 5 ||
						objects[(uint16_t)cd->ai_target_ref].genus == 3)
						timer_base = ((uint16_t)(uint8_t)math2_getrandom() & 7) + 20;
					else
						timer_base = ((uint16_t)(uint8_t)math2_getrandom() & 3) + 2;
					craftptr->maneuver_timer = 236 * timer_base;
				}
				craftptr->mode_subbyte = 1;
				return 0;
			}
			/* Still approaching: hand off to attacktarget. */
			paiman_attacktarget(0);
			if (craftptr->mode_byte == MODE_Attack)
				craftptr->throttle_speed = 0xFFFFu;
			else
				craftptr->throttle_speed = 0xC000u;
			return 0;
		case 1:
			if (craftptr->maneuver_timer == 0)
				return 1;
			return 0;
	}
	return 0;
}

/* ---- MODE_Zoom (13) / MODE_Dive (14) — shared runtime body --------- */

// FUNCTION: TIE95 0x3A6C0
void paiman_initzoommaneuver(void) {
	craftptr->throttle_speed = 0xFFFFu;
	craftptr->ai_roll_state = 3;
	craftptr->ai_roll_step = 0xFFFFu;
	craftptr->ai_target_roll = math2_getrandom();
	craftptr->ai_target_pitch = 0x4000 - ((math2_getrandom() & 0x0FFF) + 0x2000);
	craftptr->maneuver_timer = 236 * (((uint16_t)(uint8_t)math2_getrandom() & 3) + 3);
	craftptr->ai_climb_state = 0;
	craftptr->ai_dive_state = 0;
	craftptr->ai_pitch_force = 0;
	craftptr->ai_pitch_state = (craftptr->ai_target_pitch > craftptr->orient_pitch) + 1;
	craftptr->ai_pitch_step = 0xFFFFu;
}

// FUNCTION: TIE95 0x3A760
int16_t paiman_zoommaneuver(void) { return craftptr->maneuver_timer == 0; }

// FUNCTION: TIE95 0x3A770
void paiman_initdivemaneuver(void) {
	CraftData* craft;
	uint16_t pitch_rnd;

	craftptr->throttle_speed = 0xFFFFu;
	pitch_rnd = (math2_getrandom() & 0x0FFF) + 0x5800;
	craft = craftptr;
	craft->ai_climb_state = 0;
	craft->ai_pitch_force = 0;
	craft->ai_target_pitch = pitch_rnd;
	craft->ai_pitch_step = 0xFFFFu;
	craft->ai_pitch_state = (craft->ai_target_pitch > craft->orient_pitch) + 1;
	craft->maneuver_timer = 1180;
}

/* ---- MODE_SplitsDive (15) / MODE_SplitsDiveAlt (27) ---------------- */

// FUNCTION: TIE95 0x3A7C4
void paiman_initsplitsdivemaneuver(void) {
	uint16_t pitch_rnd;

	craftptr->ai_roll_state = 1;
	craftptr->ai_roll_step = 0xFFFFu;
	craftptr->ai_target_roll = 0x8000;
	craftptr->ai_heading_state = 0;
	craftptr->ai_pitch_force = 1;
	pitch_rnd = (math2_getrandom() & 0x3FFF) + 0x4000;
	craftptr->ai_pitch_state = 2;
	craftptr->ai_pitch_step = 0xFFFFu;
	craftptr->ai_target_pitch = pitch_rnd;
}

// FUNCTION: TIE95 0x3A810
int16_t paiman_splitsdivemaneuver(void) {
	return (craftptr->ai_roll_state == 4 && craftptr->ai_pitch_state == 3) ? 1 : 0;
}

/* ---- MODE_SpeedAway (16) ------------------------------------------- */

// FUNCTION: TIE95 0x3A838
void paiman_initspeedawaymaneuver(void) {
	CraftData* cd = craftptr;
	int self_idx = ai.active_obj_idx;

	cd->throttle_speed = 0xFFFFu;
	cd->maneuver_timer = 4720;
	craftptr->ai_target_heading = objects[self_idx].heading + (uint8_t)math2_getrandom();
	paiman_setjink(self_idx);
}

// FUNCTION: TIE95 0x3A89C
int16_t paiman_speedawaymaneuver(void) {
	if (!craftptr->ai_plan_state) {
		craftptr->ai_target_heading = -craftptr->ai_target_heading;
		paiman_setjink(ai.active_obj_idx);
	}
	return craftptr->maneuver_timer == 0;
}

/* Random z-axis jink used by speedaway. */
// FUNCTION: TIE95 0x3A8D8
void paiman_setjink(uint16_t self_idx) {
	int16_t push_z;
	int16_t heading_off;

	push_z = (math2_getrandom() & 0x1F) + 50;
	heading_off = (math2_getrandom() & 0xFF) + 384;

	/* Flip sign if the previous push was non-negative — alternates dir. */
	if (craftptr->push_accum_z >= 0) {
		push_z = -push_z;
		heading_off = -heading_off;
	}

	/* Retail zero-extends the 16-bit value before storing into the
	 * 32-bit push_accum_z. For negative push_z this produces a positive
	 * int32 around 65455..65486; apply_push_accum reads the field as
	 * int32 so the sign matters. */
	craftptr->push_accum_z = (uint16_t)push_z;
	craftptr->ai_target_heading = objects[self_idx].heading + heading_off;
	paiman_setturn((craftptr->skill_value >> 1) + 0x8000);
	craftptr->ai_plan_state = 118;
}

/* ---- MODE_IntoHyperspace (21) -------------------------------------- */

// FUNCTION: TIE95 0x3A968
void paiman_initintohyperspacemaneuver(void) {
	paiman_setflighttotarget(0, 1);
	craftptr->throttle_speed = 0xFFFFu;
	craftptr->mode_subbyte = 0;
}

// FUNCTION: TIE95 0x3A98C
int16_t paiman_intohyperspacemaneuver(void) {
	uint16_t pax_obj_idx;
	int16_t pax_fg_idx;

	switch (craftptr->mode_subbyte) {
		case 0:
			paiman_setflighttotarget(0, 1);
			if (trig2_polardistance < 0x4000) {
				craftptr->flight_flag = 5;
				craftptr->ai_roll_state = 0;
				craftptr->ai_pitch_state = 0;
				craftptr->ai_heading_state = 0;
				craftptr->mode_subbyte = 1;
				craftptr->ai_plan_state = 944;
				craftptr->maneuver_timer = 1652;
			}
			craftptr->throttle_speed = 0xFFFFu;
			break;
		case 1:
			craftptr->flight_flag = 5;
			if ((uint16_t)objects[ai.active_obj_idx].current_speed < 0xE10)
				break;

			msg_craftmessage(ai.active_obj_idx, craftptr, 0x61);
			score_craftexitscoring(ai.active_obj_idx, ai.fg_idx, 3);
			objects[ai.active_obj_idx].ship_idx = 0;

			/* Carry-over passenger (tow_slave_ref) exit bookkeeping. */
			pax_obj_idx = (uint16_t)craftptr->tow_slave_ref;
			if (pax_obj_idx != 0xFFFF && pax_obj_idx < 14336) {
				pax_fg_idx = objects[pax_obj_idx].fg_idx;
				score_craftexitscoring(pax_obj_idx, pax_fg_idx, 7);
				++fgstatus[pax_fg_idx].counts[FG_COUNT_HYPERSPACED];
				if ((int8_t)fg_array[pax_fg_idx].special_craft ==
					objects[pax_obj_idx].craft_ptr->craft_idx_in_fg)
					fgstatus[pax_fg_idx].special_counts[FG_COUNT_HYPERSPACED] = 1;
				objects[pax_obj_idx].ship_idx = 0;
			}
			break;
	}
	return 0;
}

/* ---- MODE_OutOfHyperspace (22) ------------------------------------- */

// FUNCTION: TIE95 0x3AB48
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

// FUNCTION: TIE95 0x3ABA8
int16_t paiman_outofhyperspacemaneuver(void) {
	bool done;
	uint16_t order;
	uint8_t mapped_order;

	if (!craftptr->ai_plan_state) {
		if (++craftptr->mode_subbyte > 8)
			craftptr->mode_subbyte = 8;
		objects[ai.active_obj_idx].current_speed = (int16_t)stagevel[craftptr->mode_subbyte];
		craftptr->ai_plan_state = 236;
	}

	done = false;
	if (craftptr->leader_obj_idx == 0xFFu) {
		trig2_ctop(craftptr->waypoint_x_cache - objects[ai.active_obj_idx].world_x,
				   craftptr->waypoint_y_cache - objects[ai.active_obj_idx].world_y,
				   craftptr->waypoint_z_cache - objects[ai.active_obj_idx].world_z);
		if (trig2_polardistance < 0x4000 || !craftptr->maneuver_timer)
			done = true;
	} else if (ai.leader_craft->current_order != 52) {
		done = true;
	}

	if (done) {
		order = (int8_t)fg_array[ai.fg_idx].ai[0].order;
#ifdef TIE_MODERN
		// HARDENING: orders past the 33-entry tables (retail HI1W.TIE uses 35) take the null plan.
		if (order >= sizeof(ordersldr))
			mapped_order = 0;
		else
#endif
			if (craftptr->leader_obj_idx == 0xFFu)
			mapped_order = ordersldr[order];
		else
			mapped_order = ordersflw[order];

		craftptr->flight_flag = 0;
		craftptr->ai_update_rate = craftptr->capture_list[0];
		craftptr->ai_target_ref = (int16_t)0xFF; /* clear maneuver target ref */

		/* Watcom self-modifying plan: write mapped_order into byte[3] of
		 * outofhyperspaceplan (= byte_C5FEF in the binary). The plan VM reads
		 * this byte as the next_order on transition; without it the maneuver
		 * returns 1 but the VM never transitions out of mode_byte 22 — leader
		 * pinned at 250, followers stuck on the stagevel ladder at 1800. */
		outofhyperspaceplan[3] = mapped_order;

		if (mapped_order <= 2)
			objects[ai.active_obj_idx].current_speed = 0;
		else
			objects[ai.active_obj_idx].current_speed = 250;

		return 1;
	}
	return 0;
}

/* ---- MODE_Escort (17) ---------------------------------------------- */

// FUNCTION: TIE95 0x3AD6C
void paiman_initescortmaneuver(void) {}

// FUNCTION: TIE95 0x3AD70
int16_t paiman_escortmaneuver(void) {
	uint16_t self_idx = ai.active_obj_idx;
	uint16_t escortee_fg = craftptr->escortee_fg_idx;
	uint16_t leader_idx = 0xFFu;
	uint16_t i;
	CraftData* leader_cd;
	uint32_t catchup_dist;
	uint16_t leader_speed;
	uint16_t our_speed;

	/* (1) Find the escort target FG's leader. */
	for (i = 0; i < NUM_CRAFTS; ++i) {
		if (objects[i].ship_idx) {
			CraftData* cd = objects[i].craft_ptr;
			if (escortee_fg == objects[i].fg_idx && cd->leader_obj_idx == 0xFF) {
				leader_idx = i;
				break;
			}
		}
	}
	if (leader_idx != 0xFF) {
		pai_distancebetween(leader_idx, self_idx);
		leader_cd = objects[leader_idx].craft_ptr;

		/* (2) Far from leader or leader-status-dead: chase mode. */
		if (species_table[objects[leader_idx].ship_idx].bound_hwidth >= 3000)
			catchup_dist = 0x20000;
		else
			catchup_dist = 0x8000;

		if (catchup_dist < trig2_polardistance || !leader_cd->status_flags) {
			craftptr->waypoint_x_cache = objects[leader_idx].world_x;
			craftptr->waypoint_y_cache = objects[leader_idx].world_y;
			craftptr->waypoint_z_cache = objects[leader_idx].world_z;
			if (!leader_cd->status_flags)
				paiman_setflighttotarget(0x4000, 1);
			else
				paiman_setflighttotarget(0, 1);
			if (trig2_polardistance > 0x10000)
				craftptr->throttle_speed = 0xFFFF;
			else
				craftptr->throttle_speed = 0x4000;
		} else {
			/* (3) Match leader heading. */
			if (leader_cd->ai_heading_state == 2) {
				craftptr->ai_target_heading = leader_cd->ai_target_heading;
				paiman_setturn((craftptr->skill_value >> 3) + 0x4000);
			} else if (objects[leader_idx].heading != objects[self_idx].heading) {
				craftptr->ai_target_heading = objects[leader_idx].heading;
				paiman_setturn((craftptr->skill_value >> 3) + 0x4000);
			}

			/* (4) Throttle: chase leader speed. */
			{
				leader_speed = objects[leader_idx].current_speed;
				our_speed = objects[self_idx].current_speed;

				if (leader_speed > our_speed) {
					uint16_t previous_throttle = craftptr->throttle_speed;
					craftptr->throttle_speed += (leader_speed - our_speed) * 50;
					if (previous_throttle > craftptr->throttle_speed)
						craftptr->throttle_speed = 0xFFFF;
				} else if (leader_speed < our_speed) {
					uint16_t previous_throttle = craftptr->throttle_speed;
					craftptr->throttle_speed -= (our_speed - leader_speed) * 50;
					if (previous_throttle < craftptr->throttle_speed)
						craftptr->throttle_speed = 0;
				}
			}

			/* (5) Match pitch. */
			{
				uint16_t pitch_delta = craftptr->orient_pitch - leader_cd->orient_pitch;
				if (pitch_delta >= 0x8000)
					pitch_delta = -pitch_delta;
				if (pitch_delta < 0x400) {
					craftptr->ai_pitch_state = 0;
					craftptr->orient_pitch = leader_cd->orient_pitch;
				} else {
					craftptr->ai_target_pitch = leader_cd->orient_pitch;
					craftptr->ai_pitch_step = 0xFFFF;
					craftptr->ai_pitch_state = (craftptr->orient_pitch < leader_cd->orient_pitch) + 1;
					craftptr->ai_pitch_force = 0;
				}
			}

			/* (6) Match roll. */
			{
				uint16_t roll_delta = objects[self_idx].roll - objects[leader_idx].roll;
				if (roll_delta >= 0x8000)
					roll_delta = -roll_delta;
				if (roll_delta < 0x400) {
					objects[self_idx].roll = objects[leader_idx].roll;
					objects[self_idx].orient_dirty = 1;
					craftptr->ai_roll_state = 0;
				} else {
					craftptr->ai_roll_step = 0xFFFF;
					craftptr->ai_roll_state = 1;
					craftptr->ai_target_roll = objects[leader_idx].roll;
				}
			}

			/* (7) Positional offset. Escort slot = ai[ai_count].var[0] (0..26). */
			{
				int slot = (int8_t)fg_array[ai.fg_idx].ai[ai.ai_entry_count].var[0];
				int16_t side = _escortsidepos[slot];
				int16_t up = _escortuppos[slot];
				int16_t fwd = _escortfwdpos[slot];

				pai_calcrotatedpoint(&objects[leader_idx], side, up, fwd);
			}

			if (species_table[objects[leader_idx].ship_idx].bound_hwidth >= 3000) {
				rotatedx *= 16;
				rotatedy *= 16;
				rotatedz *= 16;
			}

			if (leader_cd->leader_obj_idx != pstate.object_idx ||
				(uint16_t)pstate.player->current_speed > 10) {
				craftptr->push_accum_x =
					rotatedx + objects[leader_idx].world_x - objects[ai.active_obj_idx].world_x;
				craftptr->push_accum_y =
					rotatedy + objects[leader_idx].world_y - objects[ai.active_obj_idx].world_y;
				craftptr->push_accum_z =
					rotatedz + objects[leader_idx].world_z - objects[ai.active_obj_idx].world_z;
				return 0;
			}
		}
		craftptr->push_accum_x = 0;
		craftptr->push_accum_y = 0;
		craftptr->push_accum_z = 0;
		return 0;
	}

	paiman_setflighttotarget(0, 1);
	craftptr->throttle_speed = 0x8000;
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

// FUNCTION: TIE95 0x3B334
void paiman_initboardmaneuver(void) {
	craftptr->mode_subbyte = 0;
	TIE_FLIGHT_TRACE_BOARD(ai.active_obj_idx, (uint16_t)craftptr->ai_target_ref, TIE_TRACE_BOARD_APPROACH,
						   craftptr->default_order_ldr);
}

// FUNCTION: TIE95 0x3B350
int16_t paiman_boardmaneuver(void) {
	uint16_t target_ref = craftptr->ai_target_ref;
	uint16_t dock_delay = fg_array[ai.fg_idx].ai[ai.ai_entry_count].var[0];
	uint16_t tgt_species;
	uint16_t tgt_fg_idx;
	uint16_t target_idnumber;
	CraftData* tgt_cd;

	if (target_ref < 0x3800) {
		tgt_cd = objects[target_ref].craft_ptr;
		tgt_species = tgt_cd->species_idx;
		tgt_fg_idx = objects[target_ref].fg_idx;
		target_idnumber = objects[target_ref].idnumber;
	} else {
		tgt_species = 0xFFu;
		tgt_fg_idx = staticobjects[target_ref - 0x3800].fg_idx;
		target_idnumber = staticobjects[target_ref - 0x3800].idnumber;
	}

	switch (craftptr->mode_subbyte) {
		case 0: /* Approach to dock point. */
			if (target_ref < 0x3800) {
				/* Live target: species-specific dock approach offset. */
				int16_t dock_fwd = spec_data[tgt_species].dock_fwd;
				int16_t offset_up;

				if (!objects[target_ref].genus || objects[target_ref].genus == GENUS_TRANSPORT) {
					offset_up = spec_data[tgt_species].dock_passive_light -
								spec_data[craftptr->species_idx].dock_active_light;
				} else if (!objects[ai.active_obj_idx].genus ||
						   objects[ai.active_obj_idx].genus == GENUS_TRANSPORT) {
					offset_up = spec_data[tgt_species].dock_passive_light -
								spec_data[craftptr->species_idx].dock_active_heavy +
								spec_data[tgt_species].dock_passive_heavy;
				} else {
					offset_up = spec_data[tgt_species].dock_passive_heavy -
								spec_data[craftptr->species_idx].dock_active_heavy +
								spec_data[tgt_species].dock_passive_heavy;
				}
				/* Approach point along the target's up axis, above the docking offset. */
				offset_up += (int16_t)(spec_data[tgt_species].dock_passive_heavy -
									   spec_data[craftptr->species_idx].dock_active_heavy) *
							 2;
				if (offset_up < 0)
					offset_up = 28672;

				pai_calcrotatedpoint(&objects[target_ref], 0, offset_up, dock_fwd);
				craftptr->waypoint_x_cache = objects[target_ref].world_x + rotatedx;
				craftptr->waypoint_y_cache = objects[target_ref].world_y + rotatedy;
				craftptr->waypoint_z_cache = objects[target_ref].world_z + rotatedz;
			} else {
				/* Static target: just use its world pos + 2048 z offset. */
				create_getworldposition(target_ref, 0);
				craftptr->waypoint_x_cache = worldlocx;
				craftptr->waypoint_y_cache = worldlocy;
				craftptr->waypoint_z_cache = worldlocz + 2048;
			}

			paiman_setflighttotarget(0, 1);
			if (trig2_polardistance > 0x4000)
				craftptr->throttle_speed = 0xFFFFu;
			if (trig2_polardistance > 0x2000) {
				craftptr->throttle_speed = 0x8000;
				return 0;
			}
			if (trig2_polardistance > 2048) {
				craftptr->throttle_speed = 0x4000;
				return 0;
			}
			craftptr->throttle_speed = 0;
			craftptr->mode_subbyte = 1;
			TIE_FLIGHT_TRACE_BOARD(ai.active_obj_idx, target_ref, TIE_TRACE_BOARD_ALIGNING,
								   craftptr->default_order_ldr);
			return 0;
		case 1: { /* Close alignment + dock announcement. */
			int32_t push_x, push_y, push_z;
			int16_t target_roll;
			int16_t target_pitch;
			int16_t target_heading;

			if (target_ref < 0x3800) {
				int16_t dock_fwd = spec_data[tgt_species].dock_fwd;
				int16_t offset_up;

				if (!objects[target_ref].genus || objects[target_ref].genus == GENUS_TRANSPORT) {
					offset_up = spec_data[tgt_species].dock_passive_light -
								spec_data[craftptr->species_idx].dock_active_light;
				} else if (!objects[ai.active_obj_idx].genus ||
						   objects[ai.active_obj_idx].genus == GENUS_TRANSPORT) {
					offset_up = spec_data[tgt_species].dock_passive_light -
								spec_data[craftptr->species_idx].dock_active_heavy;
				} else {
					offset_up = spec_data[tgt_species].dock_passive_heavy -
								spec_data[craftptr->species_idx].dock_active_heavy;
				}
				pai_calcrotatedpoint(&objects[target_ref], 0, offset_up, dock_fwd);
				craftptr->push_accum_x = push_x =
					objects[target_ref].world_x + rotatedx - objects[ai.active_obj_idx].world_x;
				craftptr->push_accum_y = push_y =
					objects[target_ref].world_y + rotatedy - objects[ai.active_obj_idx].world_y;
				craftptr->push_accum_z = push_z =
					objects[target_ref].world_z + rotatedz - objects[ai.active_obj_idx].world_z;
				target_roll = objects[target_ref].roll;
				target_pitch = objects[target_ref].pitch;
				target_heading = objects[target_ref].heading;
			} else {
				create_getworldposition(target_ref, 0);
				craftptr->push_accum_x = push_x = worldlocx - objects[ai.active_obj_idx].world_x;
				craftptr->push_accum_y = push_y = worldlocy - objects[ai.active_obj_idx].world_y;
				craftptr->push_accum_z = push_z = worldlocz + 128 - objects[ai.active_obj_idx].world_z;
				target_roll = 0;
				target_pitch = 0x4000;
				target_heading = 0;
			}

			if (target_roll != objects[ai.active_obj_idx].roll) {
				craftptr->ai_roll_state = 1;
				craftptr->ai_roll_step = 0x8000u;
				craftptr->ai_target_roll = target_roll;
			}
			if (target_heading != objects[ai.active_obj_idx].heading) {
				craftptr->ai_heading_state = 2;
				craftptr->ai_heading_step = 0x8000u;
				craftptr->ai_target_heading = target_heading;
			}
			if (objects[ai.active_obj_idx].pitch != objects[target_ref].pitch) {
				craftptr->ai_pitch_step = 0x8000u;
				craftptr->ai_target_pitch = target_pitch;
				craftptr->ai_pitch_force = 0;
				if (craftptr->ai_target_pitch <= craftptr->orient_pitch)
					craftptr->ai_pitch_state = 1;
				else
					craftptr->ai_pitch_state = 2;
			}

			if (push_x < 0)
				push_x = -push_x;
			if (push_y < 0)
				push_y = -push_y;
			if (push_z < 0)
				push_z = -push_z;
			if (push_x + push_y + push_z >= 16)
				return 0;

			/* Docked. */
			craftptr->push_accum_x = 0;
			craftptr->push_accum_y = 0;
			craftptr->push_accum_z = 0;
			craftptr->mode_subbyte = 2;
			craftptr->ai_plan_state = 236;
			craftptr->maneuver_timer = dock_delay * 1180;
			TIE_FLIGHT_TRACE_BOARD(ai.active_obj_idx, target_ref, TIE_TRACE_BOARD_DOCKED,
								   craftptr->default_order_ldr);

			msg_createobjectname(ai.active_obj_idx, 1, tempstring);
			msg_addmessageptr(0, tempstring);
			msg_createobjectname(target_ref, 1, temp2string);
			msg_addmessageptr(1, temp2string);
			messageside = objects[ai.active_obj_idx].side;
			msg_messageprintf(MSG_DOCKED_WITH);

			if (objects[ai.active_obj_idx].side == pstate.player->side) {
				fsfx_speakobjectname(ai.active_obj_idx, 0x33);
				switch (craftptr->default_order_ldr) {
					case 0x1C:
					case 0x1D:
					case 0x20:
					case 0x21:
					case 0x44:
						if (target_ref == pstate.object_idx)
							fsfx_speakoperation(0x43, 0x3F);
						else
							fsfx_speakoperation(0x42, 0x3F);
						break;
					case 0x1E:
					case 0x22:
						fsfx_speakoperation(0x41, 0x3F);
						break;
					case 0x1F:
						fsfx_speakoperation(0x44, 0x3F);
						break;
				}
			} else {
				fsfx_triggersfx(0x27, 0xFFFF);
			}
			return 0;
		}
		case 2: { /* Transfer. */
			AiContext saved_ai_ctx;

			if (craftptr->maneuver_timer) {
				/* Pre-timer: re-arm missiles / subsystems for order 28 when linked
				 * to the player craft. */
				uint16_t bank;
				uint16_t bit;
				uint16_t m;
				int16_t changed_flag;

				if (craftptr->default_order_ldr != 28)
					return 0;
				if ((uint16_t)craftptr->pending_radio_command != pstate.object_idx)
					return 0;
				if (craftptr->ai_plan_state)
					return 0;

				/* Missile restock. */
				changed_flag = 0;
				for (bank = 0; bank < tgt_cd->missile_group_cnt; ++bank) {
					uint16_t missile_slot;
					uint16_t missile_end;

					if (!tgt_cd->warhead_type[bank])
						continue;
					missile_slot = spec_data[tgt_species].missile_start[bank];
					missile_end = spec_data[tgt_species].missile_end[bank];
					for (; missile_slot <= missile_end; ++missile_slot) {
						uint16_t torp_used;
						uint16_t torp_cnt;
						int8_t sf;

						if (!special_features_flag && target_ref == pstate.object_idx)
							torp_used = mission.torp_used;
						else
							torp_used = (int8_t)fg_array[objects[target_ref].fg_idx].warhead;
						if (bank == 1)
							torp_used = 5;
						/* Binary reads byte_C7AFB[bank + species*236] = +0x47 of the
						 * SpecData entry, which is missile_fire_mode (always BSS-zero
						 * — FEDISKIO_fillinspec never writes it). math2_fraction(0, ...)
						 * returns 0, then the !torp_cnt fallback below forces 1. */
						torp_cnt = math2_fraction(spec_data[tgt_species].missile_fire_mode[bank],
												  warheadadjust[torp_used]);
						if (!torp_cnt)
							torp_cnt = 1;
						sf = (int8_t)fg_array[objects[target_ref].fg_idx].version;
						if (sf == 1)
							torp_cnt += torp_cnt;
						else if (sf == 2)
							torp_cnt >>= 1;
						if (!torp_cnt)
							torp_cnt = 1;
						if (target_ref == pstate.object_idx && tgt_species == spec_getspecnum(0xC)) {
							if (torp_cnt > 99)
								torp_cnt = 99;
						} else if (torp_cnt > 9) {
							torp_cnt = 9;
						}
						/* Restore one round at a time and fully charge the launcher. */
						if (torp_cnt > tgt_cd->weapon_slots[missile_slot].ammo) {
							changed_flag = 1;
							++tgt_cd->weapon_slots[missile_slot].ammo;
						}
						tgt_cd->weapon_slots[missile_slot].charge = 127;
					}
				}

				/* First missing capability bit. */
				bit = 1;
				for (m = 0; m < 13; ++m) {
					if ((bit & tgt_cd->installed_subsystems) && !(bit & tgt_cd->working_subsystems)) {
						changed_flag = 1;
						tgt_cd->working_subsystems |= bit;
						break;
					}
					bit <<= 1;
				}
				/* First missing status bit. */
				bit = 1;
				for (m = 0; m < 10; ++m) {
					if ((bit & tgt_cd->subsystem_active) && !(bit & tgt_cd->status_flags)) {
						changed_flag = 1;
						tgt_cd->status_flags |= bit;
						break;
					}
					bit <<= 1;
				}

				craftptr->ai_plan_state = 472;
				if (!changed_flag)
					return 0;
				craftptr->maneuver_timer = 1416;
				if (pstate.object_idx == target_ref && !replayviewmode)
					panel_initpanel();
				return 0;
			}

			/* Transfer: per-order cargo / capture / repair effects. */
			switch (craftptr->default_order_ldr) {
				case 0x1C: /* Unload to target. */
					if (target_ref < 0x3800) {
						uint16_t k;

						for (k = 0; k < 16; ++k)
							tgt_cd->cargo[k] = craftptr->cargo[k];
						tgt_cd->boarding_state = 2;
					}
					craftptr->cargo[0] = 0;
					craftptr->boarding_state = 1;
					msg_craftmessage(ai.active_obj_idx, craftptr, 119);
					break;
				case 0x1D: /* Load from target. */
					if (target_ref < 0x3800) {
						uint16_t k;

						for (k = 0; k < 16; ++k)
							craftptr->cargo[k] = tgt_cd->cargo[k];
						tgt_cd->cargo[0] = 0;
						tgt_cd->boarding_state = 1;
					}
					craftptr->boarding_state = 2;
					msg_craftmessage(ai.active_obj_idx, craftptr, 119);
					break;
				case 0x1E: /* Swap. */
					if (target_ref < 0x3800) {
						uint16_t k;

						for (k = 0; k < 16; ++k) {
							char tmp = craftptr->cargo[k];
							craftptr->cargo[k] = tgt_cd->cargo[k];
							tgt_cd->cargo[k] = tmp;
						}
						tgt_cd->boarding_state = 2;
					}
					craftptr->boarding_state = 2;
					msg_craftmessage(ai.active_obj_idx, craftptr, 119);
					break;
				case 0x1F: /* Capture — set steerage + rebuild target's plan. */
					if (target_ref < 0x3800) {
						CraftData* cd_prev;

						tgt_cd->dock_state_flags = ai.fg_idx | 0x80u;
						++fgstatus[tgt_fg_idx].counts[FG_COUNT_CAPTURED];
						if ((int8_t)fg_array[tgt_fg_idx].special_craft == tgt_cd->craft_idx_in_fg)
							fgstatus[tgt_fg_idx].special_counts[FG_COUNT_CAPTURED] = 1;
						objects[target_ref].side = objects[ai.active_obj_idx].side;
						if (objects[ai.active_obj_idx].side == 1 && objects[target_ref].ship_idx < 0x45u)
							++mission.captures_by_type[tgt_cd->species_idx];

						/* Reset captured craft to cruise mode. */
						tgt_cd->flight_flag = 0;
						tgt_cd->status_flags = tgt_cd->subsystem_active;
						if (tgt_cd->max_speed_cache)
							tgt_cd->current_order = 47;
						else
							tgt_cd->current_order = 1;

						cd_prev = craftptr;
						saved_ai_ctx = ai;
						craftptr = tgt_cd;
						pai_setupcraftaivars(target_ref);
						pai_initplan(target_ref);
						craftptr = cd_prev;
						ai = saved_ai_ctx;
						TIE_FLIGHT_TRACE_BOARD(ai.active_obj_idx, target_ref, TIE_TRACE_BOARD_CAPTURED,
											   craftptr->default_order_ldr);
						msg_craftmessage(target_ref, tgt_cd, 101);
					}
					break;
				case 0x20: /* Repair. */
					if (target_ref < 0x3800) {
						CraftData* cd_prev;

						tgt_cd->current_order = 67;
						tgt_cd->ai_target_ref = ai.active_obj_idx;
						cd_prev = craftptr;
						saved_ai_ctx = ai;
						craftptr = tgt_cd;
						pai_setupcraftaivars(target_ref);
						pai_initplan(target_ref);
						craftptr = cd_prev;
						ai = saved_ai_ctx;
					}
					break;
				case 0x21: /* Retrieve passenger: stage self as carrier. */
					if (target_ref < 0x3800) {
						craftptr->tow_slave_ref = target_ref;
						tgt_cd->dock_state_flags = ai.fg_idx | 0xC0u;
						/* Imperial captures of non-Imperial low-id retrievals count
						 * toward the per-species capture ledger. The check on
						 * target.side runs before the side overwrite. */
						if (objects[ai.active_obj_idx].side == 1 && objects[target_ref].side != 1 &&
							objects[target_ref].ship_idx < 0x45u)
							++mission.captures_by_type[tgt_cd->species_idx];
						objects[target_ref].side = objects[ai.active_obj_idx].side;
						if ((int8_t)fg_array[tgt_fg_idx].special_craft == tgt_cd->craft_idx_in_fg)
							fgstatus[tgt_fg_idx].special_counts[FG_COUNT_CAPTURED] = 1;
					} else {
						/* Static anchor: bump status and null the species to mark
						 * the slot retrieved. */
						++fgstatus[staticobjects[target_ref - 0x3800].fg_idx].counts[FG_COUNT_CAPTURED];
						staticobjects[target_ref - 0x3800].species = 0;
					}
					++fgstatus[tgt_fg_idx].counts[FG_COUNT_CAPTURED];
					break;
				case 0x22: /* Deliver passenger. */
					if (target_ref < 0x3800)
						tgt_cd->boarding_state = 2;
					msg_craftmessage(ai.active_obj_idx, craftptr, 119);
					break;
				case 0x44: /* Repair subsystems. */
					if (target_ref < 0x3800) {
						tgt_cd->flight_flag = 0;
						tgt_cd->status_flags = tgt_cd->subsystem_active;
					}
					msg_craftmessage(ai.active_obj_idx, craftptr, 119);
					break;
			}
			TIE_FLIGHT_TRACE_BOARD(ai.active_obj_idx, target_ref, TIE_TRACE_BOARD_TRANSFER,
								   craftptr->default_order_ldr);

			/* SFX + inspection bookkeeping. */
			if (target_ref < 0x3800) {
				if (objects[ai.active_obj_idx].side == pstate.player->side) {
					if (!tgt_cd->inspected) {
						tgt_cd->inspected = 1;
						++fgstatus[objects[target_ref].fg_idx].counts[FG_COUNT_INSPECTED];
						if ((int8_t)fg_array[objects[target_ref].fg_idx].special_craft ==
							tgt_cd->craft_idx_in_fg)
							fgstatus[objects[target_ref].fg_idx].special_counts[FG_COUNT_INSPECTED] = 1;
					}
					fsfx_speakobjectname(ai.active_obj_idx, 0x33);
					/* Operation-completed voice: same cascade as phase 1 but with
					 * verb 0x40 (completed) instead of 0x3F (acknowledged). */
					switch (craftptr->default_order_ldr) {
						case 0x1C:
						case 0x1D:
						case 0x20:
						case 0x21:
						case 0x44:
							if (target_ref == pstate.object_idx)
								fsfx_speakoperation(0x43, 0x40);
							else
								fsfx_speakoperation(0x42, 0x40);
							break;
						case 0x1E:
						case 0x22:
							fsfx_speakoperation(0x41, 0x40);
							break;
						case 0x1F:
							fsfx_speakoperation(0x44, 0x40);
							break;
					}
				} else {
					fsfx_triggersfx(0x27, 0xFFFF);
				}
			}

			++craftptr->ai_goal_progress[craftptr->ai_state_1C];
			craftptr->capture_list[craftptr->capture_count] = target_idnumber;
			if (++craftptr->capture_count >= 10)
				--craftptr->capture_count;
			if (craftptr->capture_count == 1) {
				++fgstatus[ai.fg_idx].counts[FG_COUNT_DOCKED];
				if ((int8_t)fg_array[ai.fg_idx].special_craft == craftptr->craft_idx_in_fg)
					fgstatus[ai.fg_idx].special_counts[FG_COUNT_DOCKED] = 1;
			}
			if (target_ref < 0x3800 && !tgt_cd->board_count) {
				++fgstatus[tgt_fg_idx].counts[FG_COUNT_BOARDED];
				if ((int8_t)fg_array[tgt_fg_idx].special_craft == tgt_cd->craft_idx_in_fg)
					fgstatus[tgt_fg_idx].special_counts[FG_COUNT_BOARDED] = 1;
			}

			craftptr->mode_subbyte = 3;
			craftptr->maneuver_timer = 2360;
			TIE_FLIGHT_TRACE_BOARD(ai.active_obj_idx, target_ref, TIE_TRACE_BOARD_DEPARTING,
								   craftptr->default_order_ldr);
			if (pstate.target_obj_idx == target_ref)
				lasttargetnum = -3;
			if (target_ref == pstate.object_idx)
				craftptr->pending_radio_command = 0xFF;
			return 0;
		}
		case 3: /* Departure push and exit. */
			if (!craftptr->maneuver_timer) {
				TIE_FLIGHT_TRACE_BOARD(ai.active_obj_idx, target_ref, TIE_TRACE_BOARD_COMPLETE,
									   craftptr->default_order_ldr);
				/* Clear the target so the next maneuver does not inherit it. */
				craftptr->ai_target_ref = -1;
				return 1;
			}
			if (target_ref < 0x3800) {
				pai_calcrotatedpoint(&objects[ai.active_obj_idx], 0, 0x4000, 0);
				craftptr->push_accum_x =
					objects[target_ref].world_x + rotatedx - objects[ai.active_obj_idx].world_x;
				craftptr->push_accum_y =
					objects[target_ref].world_y + rotatedy - objects[ai.active_obj_idx].world_y;
				craftptr->push_accum_z =
					objects[target_ref].world_z + rotatedz - objects[ai.active_obj_idx].world_z;
			} else {
				craftptr->push_accum_x = 0;
				craftptr->push_accum_y = 0;
				craftptr->push_accum_z = 500;
			}
			return 0;
	}
	return 0;
}

/* ---- MODE_AwaitBoard (19) / AwaitBoardAlt (25) --------------------- */

// FUNCTION: TIE95 0x3C7FC
void paiman_initawaitboardmaneuver(void) {
	CraftData* cd = craftptr;
	cd->ai_roll_state = 0;
	cd->ai_pitch_state = 0;
	cd->ai_heading_state = 0;
	cd->throttle_speed = 0;
}

// FUNCTION: TIE95 0x3C81C
int16_t paiman_awaitboardmaneuver(void) {
	CraftData* cd = craftptr;
	cd->ai_roll_state = 0;
	cd->ai_pitch_state = 0;
	cd->ai_heading_state = 0;
	cd->throttle_speed = 0;
	return 0;
}

/* ---- MODE_HeadToward (20) ------------------------------------------ */

// FUNCTION: TIE95 0x3C83C
void paiman_initheadtowardmaneuver(void) { paiman_setflighttotarget(0, 1); }

// FUNCTION: TIE95 0x3C84C
int16_t paiman_headtowardmaneuver(void) {
	paiman_setflighttotarget(0, 1);
	craftptr->ai_roll_state = 1;
	craftptr->ai_roll_step = 0xFFFFu;
	craftptr->ai_target_roll = 0;
	return 0;
}

/* ---- MODE_TurnAway (24) -------------------------------------------- */

// FUNCTION: TIE95 0x3C878
void paiman_initturnawaymaneuver(void) {
	paiman_setnewturnaway(ai.active_obj_idx);
	craftptr->maneuver_timer = 3540;
}

// FUNCTION: TIE95 0x3C894
int16_t paiman_turnawaymaneuver(void) {
	if (!craftptr->maneuver_timer)
		return 1;
	if (!craftptr->ai_plan_state)
		paiman_setnewturnaway(ai.active_obj_idx);
	return paiman_avoidstarshipmaneuver();
}

// FUNCTION: TIE95 0x3C8BC
int16_t paiman_avoidstarshipmaneuver(void) { return 0; }

/* Pick a new turn-away target orientation: face the attacker's own heading
 * (not flipped), or flip our own heading if there is no attacker. */
// FUNCTION: TIE95 0x3C8C0
void paiman_setnewturnaway(uint16_t own_obj_idx) {
	int attacker_idx = craftptr->attacker_idx;

	if (attacker_idx != 0xFF)
		craftptr->ai_target_heading = objects[attacker_idx].heading;
	else
		craftptr->ai_target_heading = (uint16_t)(objects[own_obj_idx].heading + 0x8000u);

	paiman_setturn((craftptr->skill_value >> 1) + 0x8000);

	craftptr->ai_plan_state = (uint16_t)(236u * delayturninside[(uint16_t)ai.skill_tier]);
}

/* ---- MODE_OutOfHangar (26) ----------------------------------------- */

// FUNCTION: TIE95 0x3C94C
void paiman_initoutofhangarmaneuver(void) { craftptr->maneuver_timer = 2360; }

// FUNCTION: TIE95 0x3C95C
int16_t paiman_outofhangarmaneuver(void) {
	if (craftptr->maneuver_timer == 0) {
		uint16_t order = (int8_t)fg_array[ai.fg_idx].ai[0].order;
		uint8_t mapped_order;

#ifdef TIE_MODERN
		// HARDENING: orders past the 33-entry tables (retail HI1W.TIE uses 35) take the null plan.
		if (order >= sizeof(ordersldr))
			mapped_order = 0;
		else
#endif
			if (craftptr->leader_obj_idx == 0xFFu)
			mapped_order = ordersldr[order];
		else
			mapped_order = ordersflw[order];

		craftptr->formation_separation = 2;
		/* The plan VM reads exithangarplan[3] as the next order. */
		exithangarplan[3] = mapped_order;
		return 1;
	}
	return 0;
}

/* ---- MODE_AvoidStarship (28) / MODE_Wait (29) ---------------------- */

// FUNCTION: TIE95 0x3C9D0
void paiman_initavoidstarshipmaneuver(void) {
	CraftData* cd = craftptr;
	uint16_t rnd_ticks = (uint16_t)(236u * (uint32_t)((math2_getrandom() & 7) + 15));
	cd->ai_plan_state = rnd_ticks;
	paiman_setturn((cd->skill_value >> 1) + 0x8000);
	cd->ai_pitch_step = 0xFFFFu;
	cd->ai_pitch_force = 0;
	cd->ai_pitch_state = (uint8_t)((cd->ai_target_pitch > cd->orient_pitch) + 1);
}

// FUNCTION: TIE95 0x3CA2C
void paiman_initwaitmaneuver(void) {
	craftptr->maneuver_timer = fg_array[ai.fg_idx].ai[ai.ai_entry_count].var[0];
	craftptr->maneuver_timer *= 1180;
	craftptr->ai_roll_state = 0;
	craftptr->ai_pitch_state = 0;
	craftptr->ai_heading_state = 0;
	craftptr->throttle_speed = 0;
}

/* ---- MODE_DropOff (30) --------------------------------------------- */

// FUNCTION: TIE95 0x3CA94
int16_t paiman_dropoffmaneuver(void) {
	uint16_t craft_index;
	uint16_t tgt_fg_idx;
	uint16_t anchor_obj;
	uint16_t i;
	int32_t shield_hi;
	int32_t dx, dy, dz;
	int32_t dist_x;
	uint16_t obj_idx;

	if (!craftptr->mode_subbyte) {
		craft_index = craftptr->active_waypoint_idx;
		tgt_fg_idx = (int8_t)fg_array[ai.fg_idx].ai[ai.ai_entry_count].var[1] - 1;
		anchor_obj = 0xFFu;

		/* Scan for the target-FG's leader. */
		for (i = 0; i < NUM_CRAFTS; ++i) {
			if (objects[i].ship_idx && objects[i].fg_idx == tgt_fg_idx &&
				objects[i].craft_ptr->leader_obj_idx == 0xFFu) {
				anchor_obj = i;
			}
		}

		create_getdropposition(tgt_fg_idx, craft_index, anchor_obj);
		if (!TIE_FLIGHT_TIE98)
			draw_Lockshipfileptrs(objects[ai.active_obj_idx].ship_idx);

		obj_idx = ai.active_obj_idx;
		shield_hi = TIE_FLIGHT_EDITION((objectblockptr->shield_default >> 16),
									   -modelbounds_getminz(objects[ai.active_obj_idx].ship_idx));
		dist_x = dx = worldlocx - objects[obj_idx].world_x;
		craftptr->push_accum_x = dx;
		dy = worldlocy - objects[obj_idx].world_y;
		craftptr->push_accum_y = dy;
		dz = (-shield_hi >> 1) + worldlocz - objects[obj_idx].world_z;
		craftptr->push_accum_z = dz;

		trig2_ctop(dx, dy, dz);

		if (dx < 0)
			dist_x = -dx;
		if (dy < 0)
			dy = -dy;
		if (dz < 0)
			dz = -dz;

		if (dist_x + dy > 256) {
			if ((uint16_t)trig2_xyangle != (uint16_t)objects[ai.active_obj_idx].heading) {
				craftptr->ai_heading_state = 2;
				craftptr->ai_heading_step = 0x8000;
				craftptr->ai_target_heading = (uint16_t)trig2_xyangle;
			}
		}

		if (dy + dist_x + dz < 32) {
			AiContext saved_ai_ctx;
			CraftData* cd = craftptr;
			fgcnt = tgt_fg_idx;
			saved_ai_ctx = ai;
			leaderflag = (uint8_t)anchor_obj;
			create_startflightgroup(craft_index);
			ai = saved_ai_ctx;
			craftptr = cd;

			craftptr->maneuver_timer = 1180;
			craftptr->push_accum_z = 1500;
			craftptr->mode_subbyte++;
		}
	} else if (!craftptr->maneuver_timer) {
		craftptr->mode_subbyte = 0;
		craftptr->active_waypoint_idx++;
	}

	craftptr->ai_goal_progress[ai.ai_entry_count] = craftptr->active_waypoint_idx;
	return 0;
}

/* Point the AI craft's flight vector at craftptr->waypoint_*_cache.
 * Uses the current PAI skill_value to pick turn rate. */
// FUNCTION: TIE95 0x3CD04
void paiman_setflighttotarget(uint16_t heading_bias, int16_t drive_pitch) {
	int32_t dx;
	int32_t dy;
	int32_t dz;

	dx = craftptr->waypoint_x_cache - objects[ai.active_obj_idx].world_x;
	dy = craftptr->waypoint_y_cache - objects[ai.active_obj_idx].world_y;
	dz = craftptr->waypoint_z_cache - objects[ai.active_obj_idx].world_z;
	trig2_ctop(dx, dy, dz);

	craftptr->ai_target_heading = (uint16_t)(trig2_xyangle + heading_bias);
	paiman_setturn((craftptr->skill_value >> 1) + 0x4000);

	if (drive_pitch) {
		craftptr->ai_pitch_step = 0xFFFFu;
		craftptr->ai_climb_state = 0;
		craftptr->ai_pitch_force = 0;
		craftptr->ai_dive_state = 0;
		craftptr->ai_target_pitch = (uint16_t)trig2_zangle;
		craftptr->ai_pitch_state = (uint8_t)((craftptr->ai_target_pitch > craftptr->orient_pitch) + 1);
	}
}

/* Level-the-wings helper: roll to 0, freeze heading, clear climb/dive,
 * pitch → 0x4000 (level) with short-way direction. */
// FUNCTION: TIE95 0x3CDC4
void paiman_controlplane(void) {
	craftptr->ai_dive_state = 0;
	craftptr->ai_climb_state = 0;
	craftptr->ai_target_pitch = 0x4000;
	craftptr->ai_pitch_step = 0xFFFFu;
	craftptr->ai_pitch_force = 0;

	if (craftptr->orient_pitch < 0x4000)
		craftptr->ai_pitch_state = 2;
	else if (craftptr->orient_pitch > 0x4000)
		craftptr->ai_pitch_state = 1;
	else
		craftptr->ai_pitch_state = 3;

	craftptr->ai_roll_state = 1;
	craftptr->ai_roll_step = 0xFFFFu;
	craftptr->ai_target_roll = 0;
	craftptr->ai_heading_state = 0;
}

/* Compute aim-lead waypoint for target tgt_obj_idx. */
// FUNCTION: TIE95 0x3CF50
void paiman_calcplanelead(uint16_t tgt_obj_idx) {
	uint16_t lead_ticks = (uint16_t)objects[tgt_obj_idx].current_speed;
	uint16_t self_idx = ai.active_obj_idx;
	int32_t dx, dy, dz;

	if (lead_ticks) {
		uint16_t laser_id;
		uint16_t closing_speed;
		int16_t heading_delta;
		int16_t target_speed;
		uint16_t time_scale;

		pai_distancebetween(self_idx, (uint16_t)craftptr->ai_target_ref);

		/* Pick projectile speed bucket: missile-lock (order 19) always
		 * uses bucket 141; otherwise clamp laser_type[0] to a minimum
		 * of 137 (lightest laser). */
		if (craftptr->default_order_ldr == 19)
			laser_id = 141;
		else if (craftptr->laser_type[0] > 0x89)
			laser_id = craftptr->laser_type[0];
		else
			laser_id = 137;

		closing_speed = projectilevelocity[laser_id - WEAPON_SPECIES_BASE];
		closing_speed += objects[self_idx].current_speed;

		/* Scale by cos(heading_delta) between our heading and target's. */
		heading_delta = objects[tgt_obj_idx].heading - objects[self_idx].heading;
		target_speed = objects[tgt_obj_idx].current_speed;
		if ((uint16_t)target_speed >= 900)
			target_speed = 900;
		if ((uint16_t)heading_delta >= 0x8000)
			heading_delta = -heading_delta;

		if ((uint16_t)heading_delta < 0x4000)
			closing_speed -= trig2_cosinewordmult(target_speed, heading_delta);
		else
			closing_speed += trig2_cosinewordmult(target_speed, heading_delta);

		/* 18 * closing_speed + closing_speed/5 — Watcom idiom for "× 18.2". */
		time_scale = closing_speed * 18;
		time_scale += closing_speed / 5;
		if (!time_scale)
			time_scale = 19;

		time_scale = (uint16_t)(trig2_polardistance / time_scale);
		time_scale *= framerate;
		lead_ticks = math2_fraction(time_scale, craftptr->skill_value);
	}

	dx = objects[tgt_obj_idx].world_x - objects[tgt_obj_idx].world_x_prev;
	dy = objects[tgt_obj_idx].world_y - objects[tgt_obj_idx].world_y_prev;
	dz = objects[tgt_obj_idx].world_z - objects[tgt_obj_idx].world_z_prev;
#ifdef TIE_MODERN
	/* High-rate flight timing scales the per-tick displacement to AI ticks. */
	(void)TieAiLead_GetDisplacement(tgt_obj_idx, &dx, &dy, &dz);
#endif
	craftptr->waypoint_x_cache = dx * lead_ticks + objects[tgt_obj_idx].world_x;
	craftptr->waypoint_y_cache = dy * lead_ticks + objects[tgt_obj_idx].world_y;
	craftptr->waypoint_z_cache = dz * lead_ticks + objects[tgt_obj_idx].world_z;
}

// FUNCTION: TIE95 0x3D164
void paiman_calcformation(void) {
	int sep_units;
	uint16_t species_idx;
	int16_t bound_w;
	int16_t bound_h;
	int16_t bound_d;
	uint16_t formation;
	uint8_t craft_idx;
	int16_t off_x;
	int16_t off_y;
	int16_t off_z;
	uint16_t shift;

	species_idx = craftptr->species_idx;
	bound_w = spec_data[species_idx].bound_width;
	bound_h = spec_data[species_idx].bound_height;
	bound_d = spec_data[species_idx].bound_depth;
	craft_idx = craftptr->craft_idx_in_fg;
	formation = craftptr->formation;
	sep_units = ai.leader_craft->formation_separation + 1;

	off_x = bound_w * (_formposx[formation][craft_idx] * sep_units);
	off_y = bound_d * (_formposy[formation][craft_idx] * sep_units);
	off_z = bound_h * (_formposz[formation][craft_idx] * sep_units);

	/* Tight formations sit half a craft width further apart. */
	if (sep_units == 1) {
		off_x += _formposx[formation][craft_idx] * (bound_w / 2);
		off_z += _formposz[formation][craft_idx] * (bound_h / 2);
		off_y += _formposy[formation][craft_idx] * (bound_d / 2);
	}

	pai_calcrotatedpoint(&objects[ai.leader_obj_idx], off_x, off_z, off_y);

	/* Scale the rotated offset for capital-ship leaders (LOD shift).
	 * Binary emits `shl reg, cl` (sign-agnostic); shifting a negative
	 * int32_t in C is UB, so route through uint32_t. */
	shift = spec_data[species_idx].model_scale_shift;
	if (shift) {
		rotatedx = (int32_t)((uint32_t)rotatedx << shift);
		rotatedy = (int32_t)((uint32_t)rotatedy << shift);
		rotatedz = (int32_t)((uint32_t)rotatedz << shift);
	}

	craftptr->push_accum_x =
		objects[ai.leader_obj_idx].world_x + rotatedx - objects[ai.active_obj_idx].world_x;
	craftptr->push_accum_y =
		objects[ai.leader_obj_idx].world_y + rotatedy - objects[ai.active_obj_idx].world_y;
	craftptr->push_accum_z =
		objects[ai.leader_obj_idx].world_z + rotatedz - objects[ai.active_obj_idx].world_z;
}

/* ---- Helpers ------------------------------------------------------- */

/* Snap or drive the craft heading toward ai_target_heading. Threshold
 * 0x300 (~4.2°) distinguishes "close enough, snap now" from "rotate
 * over time". */
// FUNCTION: TIE95 0x3D3B4
void paiman_setturn(int32_t heading_step) {
	uint16_t delta = (uint16_t)(objects[ai.active_obj_idx].heading - craftptr->ai_target_heading);
	if (delta >= 0x8000)
		delta = (uint16_t)-delta;

	if (delta <= 0x300) {
		objects[ai.active_obj_idx].heading = (int16_t)craftptr->ai_target_heading;
		objects[ai.active_obj_idx].orient_dirty = 1;
		objects[ai.active_obj_idx].move_dirty = 1;
		craftptr->ai_heading_state = 3;
	} else {
		craftptr->ai_heading_state = 2;
		craftptr->ai_heading_step = (uint16_t)heading_step;
	}
}

/* Write the throttle for the active craft; obj_idx is unused by the original. */
// FUNCTION: TIE95 0x3D45C
void paiman_setpower(uint16_t obj_idx, uint16_t throttle) { craftptr->throttle_speed = throttle; }

/* Convert absolute desired_speed to throttle_speed, accounting for the
 * shield/beam/laser power-balance margin and the craft's max_speed. */
// FUNCTION: TIE95 0x3D46C
void paiman_setspeed(uint16_t obj_idx_param, uint16_t desired_speed) {
	CraftData* cd = objects[obj_idx_param].craft_ptr;
	uint16_t margin = 6 - (cd->laser_power + (cd->shield_power + cd->beam_power));
	uint16_t adj_max;

	if (margin >= 0x8000) {
		adj_max = cd->max_speed_cache - math2_fraction((uint16_t)(-margin << 13), cd->max_speed_cache);
	} else {
		adj_max = cd->max_speed_cache + math2_fraction((uint16_t)(margin << 13), cd->max_speed_cache);
	}

	if (desired_speed >= adj_max)
		craftptr->throttle_speed = 0xFFFFu;
	else
		craftptr->throttle_speed = math2_percentage(desired_speed, adj_max);
}
