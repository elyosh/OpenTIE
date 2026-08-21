#ifndef __PAIMAN_H__
#define __PAIMAN_H__

#include <stdint.h>

#include "tie/tie.h"

/* Maneuver-function signature: no args, returns non-zero to indicate the
 * maneuver has completed (plan VM should advance to the next order). */
typedef int16_t (*ManeuverFunc)(void);

/* CraftData.mode_byte values — index both dispatch tables. 31 total. */
typedef enum {
	MODE_None = 0,
	MODE_TurnInside = 1,
	MODE_Splits = 2,
	MODE_Immelmann = 3,
	MODE_Scissors = 4,
	MODE_Rendezvous = 5,
	MODE_Cruise = 6,
	MODE_HeadTowardFull = 7,
	MODE_RunAway = 8,
	MODE_HeadOnAttack = 9,
	MODE_FollowLeader = 10,
	MODE_SetupAttack = 11,
	MODE_Attack = 12,
	MODE_Zoom = 13,
	MODE_Dive = 14,
	MODE_SplitsDive = 15,
	MODE_SpeedAway = 16,
	MODE_Escort = 17,
	MODE_Board = 18,
	MODE_AwaitBoard = 19,
	MODE_HeadToward = 20,
	MODE_IntoHyperspace = 21,
	MODE_OutOfHyperspace = 22,
	MODE_AttackSecondary = 23,
	MODE_TurnAway = 24,
	MODE_AwaitBoardAlt = 25,
	MODE_OutOfHangar = 26,
	MODE_SplitsDiveAlt = 27,
	MODE_AvoidStarship = 28,
	MODE_Wait = 29,
	MODE_DropOff = 30,

	MODE_COUNT = 31,
} Maneuver;

/* ---- Public entry points ------------------------------------------ */

/* Kick off the maneuver matching craftptr->mode_byte. Zeros push_accum,
 * hit_count, mode_subbyte, then dispatches through _initmanvrfunctionptrs.
 * Called by PAI_initplan and every PAIORDER_* that forces a new maneuver. */
void paiman_initmaneuver(void);

/* Runtime dispatch — called every tick by PAIORDER_updatecourseorder.
 * Reads _manvrfunctionptrs[craftptr->mode_byte] and returns its verdict
 * (1 = complete, 0 = keep going). */
int16_t paiman_updatemaneuver(void);

/* ---- Individual init handlers (slot comment = mode_byte index) ---- */

void paiman_initturninsidemaneuver(void);      /*  1 */
void paiman_initsplitsmaneuver(void);          /*  2 */
void paiman_initimmelmannmaneuver(void);       /*  3 */
void paiman_initscissorsmaneuver(void);        /*  4 */
void paiman_initrendezvousmaneuver(void);      /*  5 */
void paiman_initcruisemaneuver(void);          /*  6 */
void paiman_initheadtowardfullmaneuver(void);  /*  7 */
void paiman_initrunawaymaneuver(void);         /*  8 */
void paiman_initheadonattackmaneuver(void);    /*  9 */
void paiman_initfollowleadermaneuver(void);    /* 10 - no-op */
void paiman_initsetupattackmaneuver(void);     /* 11,12,23 */
void paiman_initzoommaneuver(void);            /* 13 */
void paiman_initdivemaneuver(void);            /* 14 */
void paiman_initsplitsdivemaneuver(void);      /* 15,27 */
void paiman_initspeedawaymaneuver(void);       /* 16 */
void paiman_initescortmaneuver(void);          /* 17 - no-op */
void paiman_initboardmaneuver(void);           /* 18 */
void paiman_initawaitboardmaneuver(void);      /* 19,25,30 */
void paiman_initheadtowardmaneuver(void);      /* 20 */
void paiman_initintohyperspacemaneuver(void);  /* 21 */
void paiman_initoutofhyperspacemaneuver(void); /* 22 */
void paiman_initturnawaymaneuver(void);        /* 24 */
void paiman_initoutofhangarmaneuver(void);     /* 26 */
void paiman_initavoidstarshipmaneuver(void);   /* 28 */
void paiman_initwaitmaneuver(void);            /* 29 */

/* ---- Individual runtime handlers (match ManeuverFunc) ------------- */

int16_t paiman_nullmaneuver(void);            /*  0 */
int16_t paiman_turninsidemaneuver(void);      /*  1 */
int16_t paiman_splitsmaneuver(void);          /*  2 */
int16_t paiman_immelmannmaneuver(void);       /*  3 */
int16_t paiman_scissorsmaneuver(void);        /*  4 */
int16_t paiman_rendezvousmaneuver(void);      /*  5 */
int16_t paiman_cruisemaneuver(void);          /*  6 */
int16_t paiman_headtowardfullmaneuver(void);  /*  7 */
int16_t paiman_runawaymaneuver(void);         /*  8 */
int16_t paiman_headonattackmaneuver(void);    /*  9 */
int16_t paiman_followleadermaneuver(void);    /* 10 */
int16_t paiman_setupattackmaneuver(void);     /* 11 */
int16_t paiman_attackmaneuver(void);          /* 12,23 */
int16_t paiman_zoommaneuver(void);            /* 13,14 */
int16_t paiman_splitsdivemaneuver(void);      /* 15,27 */
int16_t paiman_speedawaymaneuver(void);       /* 16 */
int16_t paiman_escortmaneuver(void);          /* 17 */
int16_t paiman_boardmaneuver(void);           /* 18 */
int16_t paiman_awaitboardmaneuver(void);      /* 19,25 */
int16_t paiman_headtowardmaneuver(void);      /* 20 */
int16_t paiman_intohyperspacemaneuver(void);  /* 21 */
int16_t paiman_outofhyperspacemaneuver(void); /* 22 */
int16_t paiman_turnawaymaneuver(void);        /* 24 */
int16_t paiman_outofhangarmaneuver(void);     /* 26 */
int16_t paiman_avoidstarshipmaneuver(void);   /* 28,29 - stub, returns 0 */
int16_t paiman_dropoffmaneuver(void);         /* 30 */

/* ---- Internal helpers (called by the maneuver handlers) ----------- */

/* Point the AI craft's flight vector at craftptr->waypoint_*_cache.
 *   pitch_bias: added to xyangle before writing ai_target_pitch.
 *   drive_heading: non-zero = also update target_heading / ai_heading_state. */
void paiman_setflighttotarget(int16_t pitch_bias, int16_t drive_heading);

/* Drive or snap objects[ai.active_obj_idx].pitch toward ai_target_pitch.
 * Small residual (|delta| <= 0x300): snap immediately (pitch_state=3).
 * Large residual: pitch_state=2 + pitch_step=arg. */
void paiman_setturn(int16_t pitch_step);

/* Set throttle_speed directly. */
void paiman_setpower(uint16_t throttle);

/* Convert an absolute desired speed to throttle_speed, accounting for
 * the shield/beam/laser power-balance margin and the craft's max_speed_cache. */
void paiman_setspeed(uint16_t obj_idx, uint16_t desired_speed);

/* Level-the-wings helper: roll to 0, freeze pitch, clear climb/dive,
 * target_heading = 0x4000 with short-way heading_state. */
void paiman_controlplane(void);

/* Per-frame formation-follow offset: rotate formposx/y/z[6*formation +
 * craft_idx_in_fg] by leader's orientation (scaled by sep_units *
 * bound_{w,h,d}) and write push_accum_{x,y,z}. */
void paiman_calcformation(void);

/* Lead target location for an attacker's projectile solution; writes
 * waypoint_*_cache = target + target_velocity * ticks-to-impact. */
void paiman_calcplanelead(uint16_t tgt_obj_idx);

/* Core target-turn helper used by initsetupattackmaneuver /
 * setupattackmaneuver. Resolves waypoint (static -> settarget, live ->
 * calcplanelead), sets ai_target_pitch = xyangle + pitch_bias, requests
 * a turn, drives heading to zangle. */
void paiman_attacktarget(int16_t pitch_bias);

/* Turn-inside / turn-away sub-helpers: reload ai_plan_state countdown
 * with a per-skill-tier delay and aim 180° from the attacker. */
uint16_t paiman_setnewturninside(uint16_t own_obj_idx);
uint16_t paiman_setnewturnaway(uint16_t own_obj_idx);

/* Random ±Z jink for speed-away — sets push_accum_z, rotates target
 * pitch, and kicks a 180° turn. */
void paiman_setjink(uint16_t self_idx);

/* Cycle to the next waypoint on a cruise/patrol loop. */
void paiman_gonextwaypoint(void);

/* ---- Data tables exported (for introspection / cross-module use) --- */

/* 31-entry dispatch tables. Exposed so PAIORDER_updatecourseorder can
 * reach them without importing the whole maneuver API. */
extern const ManeuverFunc _initmanvrfunctionptrs[MODE_COUNT];
extern const ManeuverFunc _manvrfunctionptrs[MODE_COUNT];

/* Cached "currently-selected" dispatch slot. Mirrors the binary's 4-byte
 * function-pointer globals at 0xE3B4C / 0xE3B50; lets a debugger identify
 * the last-invoked maneuver. */
extern ManeuverFunc _initmanvrfunctionptr;
extern ManeuverFunc _manvrfunctionptr;

/* Formation position tables. 13 formations × 6 slots each, indexed as
 * formpos*[6 * craftptr->formation + craftptr->craft_idx_in_fg]. Values
 * are unit offsets scaled by (separation_units × bound_{w,h,d}) by
 * paiman_calcformation and create_createcraft. */
extern const int16_t _formposx[78];
extern const int16_t _formposy[78];
extern const int16_t _formposz[79];

/* Escort position grid. 27 entries = 3×3×3 cells (side, up, fwd) around
 * the escorted leader; indexed by fg[fg].ai[ai_count].var[0] via
 * paiman_escortmaneuver. */
extern const int16_t _escortsidepos[27];
extern const int16_t _escortuppos[27];
extern const int16_t _escortfwdpos[27];

/* 12-entry uint16 throttle LUT; maps EAIStruct.speed (0..11) to a
 * 16-bit throttle_speed value (~0..1.0 fixed-point). Used by most
 * inits and by rendezvous/cruise to reload throttle each tick. */
extern const uint16_t _throttleconvert[12];

/* 11-entry speed profile for outofhyperspacemaneuver's deceleration
 * ladder (3600 -> 900 -> 132 over 11 phases). */
extern const uint16_t _stagevel[11];

/* Per-skill-tier hold time between turn-inside / turn-away re-orients
 * (9/6/3 units, scaled ×236 PIT ticks by the callers). */
extern const uint16_t _delayturninside[3];

#endif /* __PAIMAN_H__ */
