/*
 * GOALS -- mission-objectives screen and goal-text formatter.
 *
 * Five functions ported 1:1 from the Watcom binary (goals.c):
 *   goals_missiongoalsroom  -- the scrollable goals display room
 *   goals_outputgoal        -- format one goal line with word-wrap
 *   goals_outputspeciesname -- species name + trailing separator
 *   goals_checkidflag       -- 'specific craft ID' flag accessor (orphan in demo)
 *   goals_checkwrap         -- right-margin word-wrap helper (orphan in demo)
 *
 * Data flow: the mission objectives are cached in the global cut[] array as
 * four EMissionGoal records (cut[0]=primary, [1]=secondary, [2]=bonus; see
 * tie.h for the struct layout). goals_missiongoalsroom renders category
 * titles and each qualifying subcondition via goals_outputgoal.
 */

#ifndef __GOALS_H__
#define __GOALS_H__

#include <stdint.h>

#include "tie/score.h" /* GoalTargetType enum (shared with SCORE) */

/* Push the scrollable mission-goals display as a tie_core task. The
 * task parks the navigation hint in `user_submodal_result` before pop:
 *   -1  previous mission
 *    0  stay / plain exit
 *   +1  next mission */
void goals_Push_MissionGoalsRoom_Task(void);

/* Render one goal line with right-margin word-wrap. Accumulates line-height
 * including any added wrap lines and returns that total so callers can
 * advance cursor_y. See the implementation comment for `op` / `cond` /
 * `status` semantics. */
int32_t goals_outputgoal(uint16_t target, uint16_t cond, int16_t target_type, uint16_t status, uint16_t op);

/* Print the display name of species `species_idx`. Falls back to
 * buoystr[species_idx - 70] for species 70..84. Emits 's' (plural) or ' '
 * (singular) as separator. Returns fontheight if a right-margin wrap
 * occurred, else 0. */
uint8_t goals_outputspeciesname(uint16_t species_idx, int16_t plural_flag);

/* Returns fgstatus[fg_index].cond_id[4].detail -- the 'specific craft id'
 * flag that drives craft-number vs '?' in outputgoal. Orphan (inlined) in
 * the demo build; body preserved for source-level callers. */
uint8_t goals_checkidflag(uint16_t fg_index);

/* Right-margin wrap test for string `s`. Emits newline + resets cursor to
 * x=6 when needed, returning fontheight; otherwise returns 0. Orphan
 * (inlined) in the demo build. */
uint8_t goals_checkwrap(const uint8_t* s);

/* --- Module-owned globals (watdbg: goals.c) --------------------------- */

/* Goals-window Y bounds. Populated from flightResolution at room entry. */
extern int32_t goalsTop;
extern int32_t goalsBottom;

/* Total and completed goal counts for primary, secondary, and bonus goals.
 * Rebuilt by every goals-room render and consumed by the debrief screens. */
extern int32_t goalsCount[3];
extern int32_t goalsCompletedCount[3];

/* Zero suppresses the bonus-category section unless an FG has its bonus
 * already marked complete. Written per-room-entry from mission.mission_mode
 * (non-training). */
extern uint8_t showbonusgoals;

/* String tables resolved by fediskio_loadstringdata from strings.dat.
 * The bare *str / *string singletons each point at one C string; the *strings
 * variants point at a char* array inside stringdata_buf. */
extern void* condstrings;         /* const char *[21]                     */
extern void* condverbstrings;     /* const char *[20]                     */
extern void* percentstrings;      /* const char *[16]                     */
extern void* goaloperatorstrings; /* const char *[2]  (GOP_AND, GOP_OR)   */
extern void* goaltitlestrings;    /* const char *[9]  (3 cats x 3 status) */
extern void* goalescapestr;       /* const char *     "[ESC]" banner      */
extern void* goal_of_string;      /* const char *     " of "              */
extern void* goal_ofall_string;   /* const char *     " of all "          */
extern void* goalskillstrings;    /* const char *[6]  skill-level names   */
extern void* goal_group_string;   /* const char *     " group "           */
extern void* goalaistrings;       /* const char *[30] AI-order names      */
extern void* goal_allbut_string;  /* const char *     "all but "          */
extern void* goalsidestrings;     /* const char *[3]  rebel/imperial/craft*/
extern void* goal_and_string;     /* const char *     " and "             */
extern void* goalfamilystrings;   /* const char *[7]  family-category     */
extern void* goal_comma_string;   /* const char *     ", "                */
extern void* goalgenusstrings;    /* const char *[16] genus-category      */
extern void* goalallfgstring;     /* const char *     "all FG"            */

#endif /* __GOALS_H__ */
