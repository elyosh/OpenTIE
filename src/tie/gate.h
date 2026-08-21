#ifndef __GATE_H__
#define __GATE_H__

#include <stdint.h>

/* Training-course gates, crossing detection, animation, scoring, and CRT output. */

/*
 * Build the 12-gate training course. Called once by tie_simulator at
 * training-mission start-up. Resets mission.mission_score and mission.
 * train_targets, lays out 12 FlightObjects / CraftData pairs at world
 * positions cumulatively stepped along a per-gate forward vector, and
 * prepares the per-mesh initial damage/rotation/init_state fields so that
 * gate_settraininglevel only has to update the animation rates.
 */
void gate_createtraininggates(void);

/*
 * Apply a training difficulty level (0..N) to the already-built course.
 * Resets currentgate to 1, mission.train_gates_passed to 0 and
 * mission.train_gates_remaining to 12, recomputes mtimer_min / mtimer_sec,
 * and re-runs the per-mesh state pass that configures which meshes rotate
 * and at what rate. Calls panel_initpanel() unless in replayviewmode.
 */
void gate_settraininglevel(uint16_t level);

/*
 * Advance the 4-slot player-pose history ring by one frame and store the
 * new slot 0 from player->world_{x,y,z}_prev / {roll,pitch,heading}.
 * Called once per physics tick by move_moveobjects while a training
 * mission is active; provides the swept-volume information that
 * gate_checkgateedge needs to detect plane crossings.
 */
void gate_savegatelastpos(void);

/*
 * Per-frame animation tick. Advances the three rotation timers
 * (cargopod / wing / antenna), walks every gate's meshes to apply the
 * appropriate delta, then probes the next gate's plane with
 * gate_checkgateedge. Handles the level-complete reward loop (timer
 * count-down, +10 per second to mission.mission_score / mission.train_bonus,
 * SFX 0x21 every 100 points, MSG_LEVEL_COMPLETED / MSG_BONUS_AWARDED,
 * and a recursive call to gate_settraininglevel(++mission.train_level)).
 *
 * The binary returns the "crossed" flag in AL; no caller reads it, so the
 * C signature is void to match anim_updateanimation's existing extern.
 */
void gate_updategateanimations(void);

/*
 * Draw one training gate into the 3D scene. obj_idx is the FlightObject slot
 * (1..12). The current gate and the one after it render the full complex
 * object (DRAW_drawcomplexobject); passed gates render only the MESH_MainHull
 * and have currenttarget / highlightcolor set so the draw pipeline tints
 * them. parentobject = obj_idx | 0x7000 tags the draw as a training gate
 * rather than a ship. Called per visible gate by tie_updatescreen.
 */
void gate_drawtraininggate(uint16_t obj_idx);
void gate_drawtraininggate_tie98(uint16_t obj_idx);

/*
 * Test whether the player's swept segment (world_xyz_prev -> world_xyz)
 * crossed the plane of gate `obj_idx`. Plane center is the gate's world
 * position plus a forward offset that depends on the gate's ship_idx
 * (98 = -speed_default, 99 = 0) and whether this is the current gate
 * (-1024 look-behind) or a look-ahead gate (+32). Returns 1 on a crossing,
 * 0 otherwise.
 */
int gate_checkgateedge(uint16_t obj_idx);

/*
 * Fires lasers from armed training-gun turrets in staticobjects[] at the
 * player. Iterates static objects with ship_class == 14 and the 0x80 bit
 * of anim_frame set, projects each turret's local muzzle position through
 * its model hierarchy, leads the shot by polardistance, adds level-scaled
 * jitter, and spawns a GENUS_PROJECTILE_NPC slot via create_findslot(7).
 *
 * The current training loop does not call this function.
 */
void gate_updategateguns(void);

void gate_setrenderreferenceobject(uint16_t object_index);

/*
 * Redraw the training CRT on the cockpit panel at the anchor (x_origin,
 * y_origin) supplied by panel_updatecmd. The labels (LEVEL/REMAIN/PASSED/
 * TARGETS/SCORE) are only drawn when initpanelflag is set; the numeric
 * values (train_level, train_gates_remaining, train_gates_passed,
 * train_targets, mission_score) are refreshed every call. Column offsets
 * scale with flightResolution (19 = 320x200 layout, 257 = 640x480 layout);
 * the CRT swaps sides of the crosshair for different player_spec_num
 * values.
 */
void gate_trainingupdatecrt(int16_t x_origin, int16_t y_origin);

/*
 * Redraw the HUD overlay (MM:SS timer and 5-digit train_bonus). Position
 * switches between a 320x200 layout and a 640x480 layout on flightResolution.
 * Calls panel_updatepanel unless in replayviewmode. Invoked once per second
 * of the level-complete reward count-down from gate_updategateanimations.
 */
void gate_updatebonuspoints(void);

/*
 * Push the per-second bonus-countdown task. Called from
 * gate_updategateanimations when the player completes the final gate
 * of a training level. The task decrements mtimer_min/mtimer_sec one
 * step every 4 PIT ticks (gated via xtimer_time_elapsed reading from
 * sim_clock); when the timer reaches zero it posts MSG_BONUS_AWARDED,
 * bumps train_level, and pops. Replaces the original synchronous
 * `while (mtimer_min || mtimer_sec) { ... while (tickcounter<4); }`
 * spin which would deadlock under HOST_DRIVEN sim_clock.
 */
void gate_Push_Bonus_Countdown_Task(void);

/*
 * Print `value` to the current FESTRING cursor as a decimal integer,
 * right-justified in a field of `num_digits`, with at least `min_digits`
 * printed (leading positions above min are padded with spaces rather than
 * zeros, except digits above 9 which clamp to '9'). Internal helper used
 * only by gate_trainingupdatecrt to render mission_score.
 */
void gate_outdnum(int32_t value, uint16_t num_digits, uint16_t min_digits);

/* --- Globals owned by gate.c --- */

/* Per-level rotation periods indexed by mission.train_level. Lower period
 * = more periods elapse per frame = faster spin. Demo table:
 *   {24,24,24,24,20,16,14,14,14,12,12,12,10,8,6,6,6,6,6,6}.
 * The antenna-spin timer reads gatespeed[train_level - 1] via the Watcom
 * unaligned-dword pattern (see gate_updategateanimations). */
extern uint16_t gatespeed[20];

/* Digit-position divisors for gate_outdnum. The slot 0 entry is a duplicate
 * '1' so that gate_outdnum's loop reaches the ones place at pos = 1. */
extern uint32_t powersof10[9];

/* String pointers into the strings.dat buffer. Populated by
 * fediskio_loadstringdata -- the relocations are done there, not here.
 * Rendered by gate_trainingupdatecrt. */
extern void* gatelevelstr;
extern void* gateremainstr;
extern void* gatepassedstr;
extern void* targetshitstr;
extern void* scorestr;

/* Three animation timers (cargopod, wing, antenna). Decremented by
 * frameticks each gate_updategateanimations call; when one goes negative
 * the "delta" applied to mesh_rotation this frame is the number of periods
 * that fit in the negative span, and the timer is pushed back positive by
 * that many periods. */
extern int16_t gatetimer[3];

/* Laser-turret fire interval used only by gate_updategateguns. */
extern int16_t gateguntimer;

/* Index of the next gate the player must cross (1..12). Set to 1 by
 * gate_settraininglevel at level start; advanced by
 * gate_updategateanimations on each crossing. After gate 12 the logic
 * wraps to checking gate 1 as the "level complete" sentinel. */
extern uint16_t currentgate;
extern uint16_t gate_render_reference_object;

/* Set while the per-section bonus countdown task is on the task stack
 * (gate_Push_Bonus_Countdown_Task → 1, bonus_countdown_step on DONE
 * → 0). The classic cockpit bonus bar is only visible during this
 * window: `panel_updatepanel` redraws the cockpit bitmap every tick
 * and only the per-step `gate_updatebonuspoints` paints the timer +
 * bonus text on top — outside the countdown the region is bare
 * cockpit. HD reads this flag to reproduce the same gating. */
extern uint8_t bonus_countdown_active;

/* 4-slot player-pose history ring. Written by gate_savegatelastpos each
 * physics tick; slot 0 is the newest pose, slot 3 is the pose used by
 * collide_collisions as the respawn snapshot on a training collision.
 * NOTE: the binary swaps pitch/heading on write -- gatepreviouspitch[0] is
 * set from player->heading and gatepreviousheading[0] from player->pitch;
 * readers compensate. */
extern int16_t gatepreviousroll[4];
extern int32_t gatepreviousx[4];
extern int32_t gatepreviousy[4];
extern int32_t gatepreviousz[4];
extern int16_t gatepreviousheading[4];
extern int16_t gatepreviouspitch[4];

#endif
