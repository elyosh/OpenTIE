#ifndef __COLLIDE_H__
#define __COLLIDE_H__

#include "tie/tie.h" /* CraftData, FlightObject, swept-segment globals */
#include <stdint.h>

/* Swept craft, projectile, and static-object collision and damage dispatch. */

/* CraftData is the typedef'd anonymous struct from tie.h (included above). */

/* ---------- Module globals (extern; defined in collide.c) ---------- */

/*
 * systemmask[10] -- 10 cap-ship subsystem bitmasks AND'd against
 * CraftData.status_flags to detect / disable a system. Indexed by
 * subsystem id 0..9 (cmd, sensors, shields, life-support, gravity,
 * communications, beam, weapons, hyperdrive, hangar). Read by
 * collide_damagecraft for missile/warhead overflow cases AND by the
 * DAMAGE module's room-disable handlers.
 */
extern int16_t systemmask[10];

/*
 * damagemsg[10] -- per-subsystem MSG template id for the
 * MSG_SYSTEM_STATUS broadcast.  Indexed in lockstep with systemmask[].
 */
extern uint8_t damagemsg[10];

/*
 * repairtime[10] -- per-subsystem repair countdown duration
 * (ticks). Stored into _player session-stat slot at the moment of
 * disable; counted down by TIE_updatetime each frame.
 */
extern int16_t repairtime[10];

/*
 * instrumentdisable[17] -- bitmasks AND'd against
 * CraftData.working_subsystems when a non-shield-overload random damage
 * roll triggers a cockpit instrument knockout. Indexed by the low
 * nibble of MATH2_getrandom (0..15); index 0 is reserved (typically
 * 0x0001 = 'forward shield' which is gated against
 * mission.train_craft_type to avoid disabling the player's mission-critical
 * shield in briefing/training/combat).
 */
extern int16_t instrumentdisable[17];

/* ---------- Function prototypes ---------- */

/*
 * Per-frame top-level collision dispatcher. Called from MOVE/USER
 * each tick. Skipped during hyperspace unless hyperabortflag is set.
 *
 * Pipeline:
 *   1) For each craft slot 0..0x1B: test player swept-volume vs craft;
 *      apply damage / teleport-respawn / elastic-bounce / friendly-tag /
 *      tractor-prompt as appropriate.
 *   2) Real-combat only: walk staticobjects[0..0x3F] for player vs static.
 *   3) For each object slot 0..0x73, dispatch by genus:
 *        - 3/4/5 (freighter / cruiser / capital): cap-vs-cap swept test
 *        - 6/7   (laser / missile projectile): proj-vs-craft and proj-vs-static
 *        - 0xB   (probe/buoy): just stage swept-segment globals
 */
void collide_collisions(void);

/*
 * Swept-volume collision test between attacker (laser/missile/craft)
 * and target craft, both already loaded into the laser/laserold and
 * craft/craftold swept globals.  Returns 1 on hit (with collide{x,y,z}off
 * filled), 0 on miss.
 *
 * Pipeline:
 *   1) Manhattan reject vs 0x40000.
 *   2) Rough Euclidean reject (writes approxdist).
 *   3) Tighter swept-bound vs combined-displacement test.
 *   4) Small bound (<=0x578): box test.
 *   5) Large bound: STARSHIP_checkstarshiphit (per-mesh hit) unless
 *      targetcomputerflag (one-shot override) AND target speed < 40,
 *      then fall back to box test.
 */
uint16_t collide_lasercraftcollide(uint16_t attacker_obj_idx, uint16_t target_obj_idx);

/*
 * Liang-Barsky-style swept-segment vs swept-AABB-extruded-by-radius
 * clip in normalised t-space. Inputs are the global swept endpoints;
 * radius is the combined laser+target bound.  Returns 0xFFFF on hit
 * (with collide{x,y,z}off = laser_dir * t_enter, 8.7 fixed-point), 0
 * on miss.
 */
int32_t collide_checkboxcollision(int32_t radius);

/*
 * Predict whether shooter_obj_idx firing from hardpoint hp_idx can hit
 * target_obj_idx in 3 frames. Computes 3-frame lookahead positions for
 * both (using projectile speed + own speed for shooter, own speed for
 * target), wires them into the laser/laserold and craft/craftold
 * globals, sets targetcomputerflag=1 (forces lasercraftcollide to use
 * the box path on capital ships at low speed), and delegates to
 * collide_lasercraftcollide. target_obj_idx >= 0x3800 routes to
 * static_laserstaticcollide (static index = target - 0x3800).
 *
 * Projectile speed comes from spec[player_spec_num].laser_type[bank]
 * with bank == player_weapon_group; hardpoint position comes from
 * spec[shooter species].hp[hp_idx]. The caller passes hp_idx = its
 * weapon-group loop index, NOT player_weapon_group, so each cannon in
 * the active bank gets its own lookahead probe. Called only by
 * panel_updatelasers.
 */
uint16_t collide_targetinrange(uint16_t shooter_obj_idx, uint16_t target_obj_idx, uint8_t hp_idx);

/*
 * Predict-ahead collision sweep for AI: extrapolate craft_obj_idx's
 * swept volume `lookahead_frames * framerate` ticks into the future
 * and test against every other craft slot 0..0x1B that is genus 3/4/5
 * (freighter / cruiser / capital). Returns the first colliding
 * obj_idx, or 0xFFFF if none.
 */
uint16_t collide_craftstarshipcollision(uint16_t craft_obj_idx, int16_t lookahead_frames);

/*
 * Process a laser/missile/warhead projectile (projectile_obj_idx)
 * hitting target craft target_obj_idx. Records first-attacker, bumps
 * hit counters / FG status, applies collide_damagecraft (with head-on
 * flag from velocity dot player.fwd), and converts the projectile
 * slot to an explosion (genus=13, ship_idx=127/128 for craft chunk
 * or -125 for missile fizz). Special-case ION CANNON (ship_idx==152)
 * drains target weapon power instead. Returns the FSFX trigger result.
 */
char collide_laserhitcraft(uint16_t projectile_obj_idx, uint16_t target_obj_idx, int16_t hit_offset);

/*
 * Apply damage to target_obj_idx.
 *   component_idx (0xFFFF for non-mesh damage): mesh component index
 *   weapon_group  (0=fwd 1=rear): selects shield slot
 *                  (CraftData.forward_shield/rear_shield)
 *   attacker_obj_idx (0xFFFF=global, 0x3800+=static obj): attacker slot
 *
 * Returns 1 if cosmetic-only (cockpit-update flag), 0 if death scheduled.
 */
char collide_damagecraft(uint16_t target_obj_idx, int16_t component_idx, uint16_t weapon_group,
						 uint16_t attacker_obj_idx);

/*
 * Convert FlightObject obj_idx into a generic explosion: sets
 * ship_idx=variant (127/128 craft chunk, -125 missile fizz), genus=13,
 * category=5, anim_frame=2, clears speed/timers/orient, plays a random
 * sfx in [19..22].
 */
char collide_makeobjectexplosion(uint16_t obj_idx, uint8_t ship_variant);

/* Cheap 3D distance estimate (unsigned). max(|dx|,|dy|,|dz|) +
 * sum(other_two)/4. Inputs already absolute. */
uint32_t collide_roughdistance3du(uint32_t abs_dx, uint32_t abs_dy, uint32_t abs_dz);

/* Same metric, signed inputs (takes absolute value of each axis first). */
int32_t collide_roughdistance3d(int32_t dx, int32_t dy, int32_t dz);

/*
 * Parametric line-segment vs polygon-mesh hit test. mesh_data points
 * at the per-mesh blob (vertex count u8 at +2, face count u8 at +4,
 * AABB +5, vertex array +0x11, face records after).  Each face record
 * is 8 bytes (4 * int16: nx, ny, nz, byte-offset-to-vertex-id-table).
 * Vertex ID table can have 0x7Fxx 'continuation' indirection chain.
 *
 * return_first_hit: if mission.train_craft_type!=0 AND nonzero, returns
 * first hit immediately (with t|1 for marker). Otherwise returns the
 * smallest t over all faces (1..0x7FFF; 0=miss).
 *
 * Callers: starship_checkstarshiphit, static_laserstaticcollide.
 */
/* mesh_data is the polygon-header pointer passed straight through —
 * retail takes it in EAX as an int, but on 64-bit hosts truncating a
 * pointer to int32 and widening back via uintptr_t clears the high 32
 * bits and produces a wild pointer. Keep it as a real pointer. */
uint32_t collide_checkhitpolygons(const uint8_t* mesh_data, int32_t x1, int32_t y1, int32_t z1, int32_t x2,
								  int32_t y2, int32_t z2, int32_t return_first_hit);

/*
 * Credit one kill to shooter_obj_idx. Increments per-craft kill
 * counter (CraftData.kills_by_species). When shooter is the player
 * and the kill counts toward win/loss conditions (filtered against
 * pri/sec/bonus FG win conds + cut[0] / fifth-cut byte at +3), also:
 * bumps player_kills_per_species[spec], plays a congrats voice
 * (random gated by win-cond severity), and triggers MSG_FRIENDLY_KILL
 * if victim is on player's side. victim_obj_idx==0xFFFF means 'kill
 * not attributed to a specific FlightObject' (still bumps total_kills
 * + mission.kills_losses).
 */
void collide_updatekills(uint16_t shooter_obj_idx, uint16_t victim_obj_idx);

/*
 * Increment the shooter's per-craft hit counter
 * (laser_hit / missile_hit / warhead_hit on CraftData) for the
 * projectile in projectile_obj_idx, classified by ship_idx range.
 * When shooter is the player, also bumps the matching session-stat
 * global (player_laser_hit / player_missile_hit / player_warhead_hit).
 * Returns the shooter's CraftData* (or self's if shooter not a craft).
 */
CraftData* collide_updatehits(uint16_t projectile_obj_idx);

#endif /* __COLLIDE_H__ */
