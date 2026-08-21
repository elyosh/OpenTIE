#ifndef __LASER_H__
#define __LASER_H__

#include "tie_runtime/species_id.h"

#include <stdint.h>

/* Projectile physics and weapon-fire dispatch. Parameter tables use
 * `(weapon_species - WEAPON_SPECIES_BASE)` and retain six zero padding slots. */
#define NUM_PROJECTILE_TYPES TIE_SPECIES_PROJECTILE_STORAGE_COUNT
#define WEAPON_SPECIES_BASE TIE_SPECIES_PROJECTILE_FIRST
/* laser_species_poly[] and projectiledataptrs[] cover species 137..154. */
#define WEAPON_SPECIES_COUNT TIE_SPECIES_PROJECTILE_COUNT
/* The recovered parameter storage retains six unused slots through 160. */
#define WARHEAD_TYPE_COUNT TIE_SPECIES_PROJECTILE_STORAGE_COUNT

extern const uint16_t projectileweight[NUM_PROJECTILE_TYPES];
extern const uint16_t projectilevelocity[NUM_PROJECTILE_TYPES];
extern const uint16_t projectilelife[NUM_PROJECTILE_TYPES];
extern const uint16_t projectilewarhead[12];

/* Forward push applied at spawn, indexed by (species - 137). The selected
 * flight version determines which original game's model dimensions apply. */
uint16_t TieProjectileLaunchOffset_Get(unsigned int projectile_type_idx);

/* Per-weapon-species 'explodes on death' flag, indexed by
 * (species - WEAPON_SPECIES_BASE). 0 = silent removal on death timer,
 * 1/2 = full explosion (variant picks chunk-type). Replaces the
 * `((char*)&projectilevelocity[3] + 1)[species]` pointer trick.
 * Values at [0..17] match retail byte_C5463[137..154]; entries [18..23]
 * (species 155..160) are zero in both demo and retail builds and are
 * included to match the demo's 24-entry _projectilewarhead so callers
 * iterating up to species 160 stay in-bounds. */
extern const uint8_t projectile_is_warhead_type[WARHEAD_TYPE_COUNT];

/* Species -> 0-based table index. Retail has no bounds check anywhere;
 * callers guarantee species in [137, 154] by only sourcing values from
 * spec_data[].laser_type / warhead_type / warheadconvert[]. We match
 * retail semantics: a stray out-of-range species is a caller bug and
 * should surface as a crash / UB, not be silently papered over. */
static inline unsigned int laser_species_idx(unsigned int species) { return species - WEAPON_SPECIES_BASE; }

/*
 * WarheadRecord -- 8-byte per-projectile runtime slot.
 *
 * When LASER spawns a projectile/missile, it builds a FlightObject at
 * some slot i (where i >= NUM_CRAFTS) and sets obj->craft_ptr =
 * &warheads[i-NUM_CRAFTS]. FlightObject types craft_ptr as CraftData*, but
 * for projectile slots the pointer really addresses an 8-byte
 * WarheadRecord, NOT a 538-byte CraftData. Only the first 8 bytes are
 * valid -- accessing further is UB.
 *
 * Two creation paths initialise this record differently:
 *   laser_createprojectile          (craft-fired): homing_tier=0,
 *                                    target_obj=0xFFFF => MOVE skips
 *                                    homing, projectile flies straight.
 *   laser_createprojectilefromstatic (static-fired turret/mine gun):
 *                                    homing_tier = (random & 3) + 3
 *                                    (i.e. 3..6), target_obj = the
 *                                    actual target obj idx. MOVE's
 *                                    homing runs using homing_tier as
 *                                    a tier offset within a group.
 *
 * sub_obj_idx and min_speed are not always initialised by the two
 * creators -- leftover values from the slot's previous use may be
 * read. In the demo they zero-init on first use.
 */
#pragma pack(push, 1)
typedef struct {
	uint8_t homing_tier;  /* +0: 0 skips homing; 3..6 selects a tier */
	uint8_t reserved_1;   /* +1: not read by any ported code */
	uint16_t sub_obj_idx; /* +2: target mesh index (0xFFFF = whole target) */
	uint16_t target_obj;  /* +4: target FlightObject idx (0xFFFF = none) */
	uint16_t min_speed;   /* +6: homing min-speed floor */
} WarheadRecord;          /* 8 bytes */
#pragma pack(pop)

#define NUM_WARHEAD_SLOTS 49

extern WarheadRecord warheads[NUM_WARHEAD_SLOTS];

/*
 * laser_weaponsfire pacing slots in tie.c's timers[] bank:
 *   timers[TIMER_LASER_STATUS]     -- 236 ticks (~1 second). Gates the
 *     per-second shield/laser/SLAM bookkeeping pass and the player
 *     beam-charge refill.
 *   timers[TIMER_LASER_BEAM_DRAIN] -- 59 ticks between beam-weapon drain
 *     steps while the beam is firing.
 * Both decrement via TIE_updatetime's per-frame timers[] sweep.
 */

/*
 * laser_warnplayer -- issue the 'INCOMING MISSILE!' HUD banner + voice
 * if the just-created warhead_slot is aimed at the player. Called at
 * the tail of laser_firemissile and laser_createprojectilefromstatic
 * after the warhead record's target_obj is populated.
 *
 * warhead_slot indexes warheads[0..48]; the paired FlightObject idx is
 * warhead_slot + 28.
 */
void laser_warnplayer(uint16_t warhead_slot);

/*
 * laser_chargeshields -- add `delta` to craftptr->shield[shield_side]
 * and clamp to [0..shield_cap]. _craftptr is the SHOOTER (not the
 * defender); this models the shooter's own shield regen as a side
 * effect of firing. shield_side 0 = forward_shield, 1 = rear_shield.
 * shield_cap scales with mission.difficulty and the shooter's side
 * (see implementation comment).
 */
void laser_chargeshields(uint16_t shooter_obj_idx, uint16_t shield_side, int16_t delta);

/*
 * laser_createprojectile -- spawn a craft-fired projectile (laser bolt
 * or unguided missile/torp). Returns the allocated slot (28..76) or
 * 0xFFFF on failure.
 *
 *   shooter_obj_idx - firing craft's FlightObject index.
 *   hp_idx          - hardpoint 0..15 into spec[species].hp[].
 *   projectile_type - 0..23; indexes the projectile_* tables.
 */
uint16_t laser_createprojectile(uint16_t shooter_obj_idx, uint16_t hp_idx, uint16_t projectile_type);

/*
 * laser_createprojectilefromstatic -- retaliation fire from a static
 * object (rebel mine / turret) whose flight group has a warhead
 * configured. Returns the allocated slot (44..75) or 0xFFFF on
 * failure. Arms homing (random tier 3..6) against shooter_obj_idx.
 */
uint16_t laser_createprojectilefromstatic(uint16_t static_obj_idx, uint16_t shooter_obj_idx);

/*
 * laser_firemissile -- fire one warhead from
 * craftptr->weapon_slots[weapon_slot_idx]. Returns the warhead slot
 * (minus 28) or 0xFFFF on any failure.
 *
 *   group_idx - 0 or 1 (warhead group; controls missile_armed[]/0x80
 *               toggle and which warhead_type is used).
 */
uint16_t laser_firemissile(uint16_t shooter_obj_idx, uint16_t weapon_slot_idx, uint16_t projectile_type,
						   uint16_t group_idx);

/*
 * laser_firelasersystem -- fire one laser group (cannon or ion). Loops
 * over the selected hardpoints in the configured fire-mode (single /
 * alternating / burst), calling laser_createprojectile once per slot
 * that has ammo/charge. group_idx 0 or 1.
 */
void laser_firelasersystem(uint16_t shooter_obj_idx, uint16_t group_idx);

/*
 * laser_firerocketsystem -- fire the warhead group for the given
 * shooter. Dispatches to laser_firemissile for one or two shots based
 * on missile_armed[group_idx]'s low-7 bits (3 = dual, else 0x80
 * toggles). Prints out-of-ammo message for the player.
 */
void laser_firerocketsystem(uint16_t shooter_obj_idx, uint16_t group_idx);

/*
 * laser_fireplayerweapon -- per-frame fire-button handler for the
 * player. Picks laser vs missile based on player_weapon_mode, drives
 * the cooldown + subsystem-health check, delegates to firelasersystem
 * or firerocketsystem.
 */
void laser_fireplayerweapon(void);

/*
 * laser_weaponsfire -- per-frame entry called by tie_doframe. Drives
 * missile lock gauge, beam weapon drain/targeting, per-second status
 * update (shield_power / laser_power / SLAM), AI fire loop, turret
 * dispatch, and mine-turret update.
 */
void laser_weaponsfire(void);

#endif /* __LASER_H__ */
