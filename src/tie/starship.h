#ifndef __STARSHIP_H__
#define __STARSHIP_H__

#include "tie/tie.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * STARSHIP -- capital-ship combat layer (6 public functions).
 *
 * Provides:
 *   - Per-mesh laser hit test (called from COLLIDE when target bound_hwidth > 0x578)
 *   - Per-component damage application + radar retarget + training-score bonus
 *   - Probabilistic per-frame explosion sparking (ANIM) and full-ship detonation (MOVE)
 *   - Per-component explosion spawning with BSP-vertex randomization
 *   - Capital-ship turret fire (one weapon slot per call) with charge model, lead
 *     computation, and LOS test
 *
 * No internal helper; starship_getcoordvalue is exposed because the binary
 * emitted it as a standalone symbol even though no caller survived in the
 * shipped build (every call-site inlines the same back-ref walk).
 */

/*
 * Walk the BSP coord back-reference chain for a single 16-bit coordinate.
 * Each record in the BSP vertex stream is 3 int16 (side, fwd, up). When a
 * record's high byte equals 0x7F, its low byte N encodes a back-reference:
 * skip N*3 bytes backwards and retry. Returns the first direct int16 reached.
 *
 * No direct callers in the shipped binary (demo); the same loop is inlined
 * in starship_makestarshipcompexplo and starship_firelasergunner.
 */
int16_t starship_getcoordvalue(const uint8_t* bsp_coord);

/*
 * Capital-ship per-mesh laser hit test. Dispatched from collide_lasercraftcollide
 * when the target's bound_hwidth > 0x578 (big-ship class).
 *
 * Transforms the laser's current+previous world-space endpoints into the
 * craft's local orientation frame, scales by the model's model_scale_shift, optionally
 * rotates around each mesh's per-object mesh_rotation angle (training mode only),
 * AABB-rejects, then walks the per-mesh LOD chain (ShipMeshLOD records, 6-byte
 * stride) until the per-LOD "budget" byte drops below 0x10 (0x18 for
 * ship_idx==28 = Super Star Destroyer). Calls collide_checkhitpolygons per mesh.
 *
 * Returns 0 if no mesh hit, else (hit_mesh_index + 1). Updates tie.c's
 * collidexoff / collideyoff / collidezoff with the closest impact offset.
 */
uint16_t starship_checkstarshiphit(uint16_t shooter_obj_idx, uint16_t target_obj_idx);

/*
 * Apply damage to a single capital-ship component (mesh). Called from
 * collide_damagecraft when the laser landed on mesh (component_plus1 - 1).
 *
 *   damage is the raw amount; it is scaled down by >>4 to "units", rounded
 *   up to a minimum of 1. craftptr->mesh_component_hp[component_idx] is the
 *   per-mesh HP counter (0 = dead, 255 = indestructible, 1..254 = HP in
 *   damage-unit ticks). If units absorb the HP, the mesh is flagged
 *   MESH_STATE_HIDDEN and an ember/debris FlightObject is
 *   spawned at its center (offset by the mesh's center_side/up/fwd,
 *   rotated into world space by pai_calcrotatedpoint, scaled by model_scale_shift).
 *
 *   Side effects when killing a component:
 *     * bumps radar_target1 past dead meshes if it was locked on this one
 *     * in training mode (mission.train_craft_type != 0): mission.train_targets++,
 *       +50 mission_score (+100 if the mesh has a non-zero rotation byte, i.e.
 *       it's a rotating turret), mtimer_sec += 2 with minute carry
 *     * FSFX_triggersfx(19..22) random explosion sound
 *
 *   The mesh's flags bit 1 (0x0002) gates whether the explosion FlightObject
 *   is spawned; if clear, damage is absorbed silently.
 *
 * Returns the overflow damage (= 16 * (units - hp_remaining)) to propagate
 * to the caller when the component died, 0 when the hit was fully absorbed.
 */
uint16_t starship_damagecomponent(uint16_t obj_idx, int16_t component_plus1, uint16_t damage);

/*
 * Spawn explosion effects on a capital ship. Two invocation paths:
 *
 *   full_ship == 1 (move_moveobjects when a big ship's death_timer expires):
 *     - detonate every MESH_MainHull mesh in sequence (up to 16 entries),
 *       passing the per-mesh explosion_scale value (mesh+0x0A, u16)
 *     - clear ship_idx on the 5 slots starting at bigexplo_obj_first so
 *       the big-bitmap pool is free for the new explosion frames
 *     - trigger the whole-ship SFX (0x12)
 *
 *   full_ship == 0 (anim_updateanimation per-frame for alive capital ships):
 *     - probabilistic sparking: quit unless math2_getrandom() < starshipexplodetail
 *     - pick one alive MESH_MainHull mesh at random (BSP-vertex centered)
 *     - spawn one component explosion with SFX 19..22 (random)
 */
void starship_createstarshipexplo(uint16_t obj_idx, int16_t full_ship);

/*
 * Spawn a single component-explosion FlightObject. Called by
 * starship_createstarshipexplo and also on the component-kill path of
 * starship_damagecomponent (via its inline duplicate of this logic).
 *
 *   use_bsp_random == 0 -- center the explosion on mesh->center_{side,fwd,up}
 *   use_bsp_random == 1 -- walk the mesh BSP vertex list (first LOD) and pick
 *                          a random vertex; resolve each of its 3 int16 coords
 *                          through the 0x7F back-reference chain
 *
 * The center is rotated into world space by pai_calcrotatedpoint, scaled by
 * 2^(model_scale_shift-1) (left-shifted when model_scale_shift >= 2, halved when model_scale_shift ==
 * 0, untouched when model_scale_shift == 1), and added to craft->world_{x,y,z}.
 *
 * The spawned FlightObject is genus=13 (Ember/Debris), category=5,
 * ship_idx = 127 + (rand & 1) (picks one of two explosion bitmap variants),
 * anim_frame=2, no parent craft_ptr. damage_state = size >> (6 - model_scale_shift).
 *
 * Returns the new object's FlightObject slot (0xFFFF on allocation failure).
 */
uint16_t starship_makestarshipcompexplo(FlightObject* craft, uint16_t component_idx, uint16_t size,
										int16_t use_bsp_random);

/* Advance one capital-ship turret slot. Skill controls its fractional
 * cooldown rate; hardpoint position, range, self-occlusion, and target lead
 * determine whether a projectile is spawned. */
void starship_firelasergunner(uint16_t craft_obj_idx, uint16_t weapon_slot_idx, uint16_t target_ref);

/*
 * STARSHIP run-time scalar detail control.
 *
 *   bigexplo_obj_first -- first of 5 reserved FlightObject slots used for
 *                         big-ship bitmap explosion sprites. In the binary
 *                         this is 92, so slots 92..96 are reused.
 *
 * starshipdetail / starshipexplodetail are tie.c-owned scalars declared in
 * tie.h. The LOD source tables (starshipdtl / starshipexplodtl) are user.c-
 * owned and declared in user.h.
 */
extern const uint16_t bigexplo_obj_first;

/*
 * Mesh-flag bit consumed by starship_damagecomponent: when the mesh's flags
 * field ANDs zero with this bit, damage is absorbed silently (no explosion
 * spawn), e.g. shield emitters on large ships before they are rendered vulnerable.
 */
#define STARSHIP_MESH_FLAG_EXPLODABLE 0x0002u

/*
 * ship_idx used internally by starship_firelasergunner for capital-ship
 * laser/ion projectiles. 138..146 range picks the projectile FlightObject
 * class; indexing the same-named tables in laser.c gives speed/radius/life.
 */
#define PROJ_SHIP_REBEL_LASER 138u
#define PROJ_SHIP_EMPIRE_LASER 140u
#define PROJ_SHIP_AMMO_LASER 141u /* +0 standard, +1 turbo */
#define PROJ_SHIP_REBEL_TURBO 145u
#define PROJ_SHIP_EMPIRE_TURBO 146u

#endif
