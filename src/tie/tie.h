#ifndef __TIE_H__
#define __TIE_H__

#include "tie/mission.h"
#include "tie/shipext.h"
#include "tie_runtime/species_id.h"
#include <stdbool.h>
#include <stdint.h>

/* Forward typedefs so module externs (below) can name pointers to these
 * structs before the full definitions appear later in this header. */
typedef struct ShipModelData ShipModelData;
typedef struct ShipModelMesh ShipModelMesh;

/* Per-species rendering and classification data. Host pointers require
 * natural alignment; field comments retain the binary offsets. */
typedef struct {
	uint8_t flags;         /* +0x00: master flags (bit 1 = has model data) */
	uint8_t load_flags;    /* +0x01: loading flags (bits 3-4 = quality, bit 6 = training-only, bit 0 = has
							  orientation) */
	uint8_t category;      /* +0x02: IFF/damage category */
	uint8_t ship_class;    /* +0x03: ship classification (weapon slot typing) */
	uint16_t bound_hwidth; /* +0x04: bounding half-width (LOD-shifted) */
	uint16_t bound_qdepth; /* +0x06: bounding quarter-depth */
	void* model_handle;    /* binary +0x08 (u16 HANDLE); host ship-model pointer,
							shared by entries for the same LFD resource */
	/* binary +0x0A (int). Pointer to the species' animation pattern
	 * (AnimOp array: bigexplo / sparks / ember / lightning / ...). NULL
	 * means 'no animation' -- static objects with NULL draw_data render
	 * as a BSP mesh via draw_drawcomplexobject. */
	void* draw_data;
	/* binary +0x0E (int). Pointer to the 16-entry palette-remap LUT
	 * used by rotscale_prepare_color for this species' bitmap frames.
	 * For planets, set from planetpalptrs[special_flag] at mission load. */
	void* bitmap_data;
	uint8_t side;      /* binary +0x12: IFF side (0=hostile, 1=imperial, 2=neutral) */
	uint8_t spec_num;  /* binary +0x13: species/spec number */
	uint8_t lfd_file;  /* binary +0x14: which LFD file (0-2) */
	uint8_t lfd_entry; /* binary +0x15: entry index within LFD */
} SpeciesEntry;

/*
 * WeaponSlot — per-laser/missile slot in CraftData.weapon_slots[16].
 * 6 bytes per entry. Initialized by create_createcraft for both laser
 * banks (slots [laser_start..laser_end] of each bank) and missile banks
 * (slots [missile_start..missile_end]). Consumed by USER_inputforplane,
 * STARSHIP_firelasergunner, ANIM_updateanimation, COLLIDE.
 */
#pragma pack(push, 1)
typedef struct {
	uint8_t type;        /* +0x00: laser/warhead type byte */
	uint8_t charge;      /* +0x01: charge level; init = 127 (full) */
	uint8_t ammo;        /* +0x02: missile count; 0 for lasers */
	uint8_t _pad_03;     /* +0x03 */
	uint16_t target_obj; /* +0x04: target FlightObject slot; 0xFFFF = none */
} WeaponSlot;
#pragma pack(pop)

/*
 * Per-mesh state byte (CraftData.mesh_state[mesh_idx]).
 * draw_drawcraft hides the mesh on any nonzero value; the 2/4 distinction
 * is bookkeeping for AI / spawn / damage code (decides whether a debris
 * FlightObject was spawned alongside the kill).
 */
enum MeshState {
	MESH_STATE_VISIBLE = 0,   /* rendered; valid component sub-target */
	MESH_STATE_HIDDEN = 2,    /* killed-in-place (ember spawned, no debris)
							   * OR selectively hidden by mission scripting
							   * (gate_settraininglevel uses this to thin
							   * the training course at low difficulty). */
	MESH_STATE_BLOWN_OFF = 4, /* replaced by a debris FlightObject; also
							   * the spawn-time pre-destroyed state for
							   * capital-ship turrets in componentsgone[]. */
};

/*
 * CraftData — per-craft runtime state.
 * 538 bytes (0x21A), 28 entries (_crafts array). Allocated by create_createcraft.
 * Field names from IDA RE of CREATE_createcraft + PAI_setupcraftaivars +
 * PAI_initplan + ANIM_updateanimation + PANEL_update* + FEDISKIO_updatepilotrecord.
 */
#pragma pack(push, 1)
typedef struct {
	uint8_t species_idx;    /* +0x000: species/craft type index */
	uint8_t leader_obj_idx; /* +0x001: FG-leader's FlightObject slot; 255 = self is leader */
	uint16_t
		skill_value; /* +0x002: per-craft skill roll; CREATE_createcraft stores skilltranslate[fgskill].
					  *         Readers in PAI/PAIMAN/STARSHIP threshold at 0x5555/0x8000/0xAAAA/0xC000
					  *         for skill-tiered behaviour (AI skill tier, turret cooldown, lead jitter).
					  *         Was misidentified as 'missile_sub_obj'; no reader treats it as an obj idx. */
	uint16_t missile_target; /* +0x004: missile's target obj idx (0xFFFF = no target) */
	uint16_t orient_heading; /* +0x006: yaw mirror of FlightObject.heading; was misnamed missile_min_speed */
	uint16_t orient_pitch;   /* +0x008: pitch mirror of FlightObject.pitch; first u16 of original field_008 */
	uint8_t pad_00A[4];      /* +0x00A..+0x00D: unused padding; never read or written */
	/* Per-frame eye-space (camera-space) position cache (3 × int32).
	 * Written every render frame by TIE_getobjecteyexyz (called from
	 * TIE_updatescreen) as (world_x - camera, world_y - camera_y,
	 * world_z - camera_z) transformed through TRANSFM2_geteyex/y/z.
	 * Readers:
	 *   PANEL_addbliptoradar    - radar blip projection (all three)
	 *   DRAW_polydepthsort      - z-sort tie-breaker (via getcompdetailptr on eye_z)
	 *   FSFX_checktieflyby      - proximity flyby trigger
	 *   GATE_savegatelastpos / checkgateedge / updategateguns - training gate tests
	 *   STARSHIP_checkstarshiphit - cap-ship collision test
	 *   USER_ejectcamera        - eject-camera animation base pose */
	int32_t eye_x_cache;   /* +0x00E */
	int32_t eye_y_cache;   /* +0x012 */
	int32_t eye_z_cache;   /* +0x016 (formerly unknown_016) */
	uint8_t beam_state;    /* +0x01A: bit 7 = beam weapon active */
	uint8_t ai_anim_flags; /* +0x01B: bit 0/1 used by ANIM for wing animation triggers */
	uint8_t ai_state_1C;   /* +0x01C: read by PAI_setupcraftaivars; mirrored to word_F8F56 */
	/* Per-AI-entry completion state (one byte per fg.ai[] slot, 3 slots).
	 * Written to 2 by PAIORDER_completegohomeorder when the current entry
	 * is finished; read by completegootherorder / orderswitchorder to
	 * decide whether that slot is consumed. Cleared to 0 in createcraft. */
	uint8_t ai_complete_state[3]; /* +0x01D..+0x01F */
	/* Per-AI-entry goal progress counter (one byte per fg.ai[] slot, 3 slots).
	 * Every writer advances by ONE progress unit, never per-frame:
	 *   - PAIMAN_gonextwaypoint:    ++ per completed patrol loop (wrap 11->4, orders 3/5/56)
	 *   - PAIMAN_boardmaneuver:     ++ per completed board/transfer cycle
	 *   - PAIORDER_awaitboardorder: ++ per dock handshake tick (gated by boarding_state==2)
	 *   - PAIMAN_dropoffmaneuver:   = active_waypoint_idx (mirrors the +1-per-drop counter)
	 * Read by PAI_aicompletioncheck for orders 3/5/39/42/43/56, which all
	 * return (progress >= ai.var[0]) to trigger plan completion. Cleared
	 * to 0 in createcraft. */
	uint8_t ai_goal_progress[3]; /* +0x020..+0x022 */
	uint8_t current_order;     /* +0x023: AI plan opcode (50=hangar, 52=hyperspace, else order_ldr/order_flw);
								  indexes planptrs[] */
	uint8_t default_order_ldr; /* +0x024: leader's default order code */
	uint8_t active_waypoint_idx;   /* +0x025: PAI_initplan way_used[] index; init=4 */
	uint8_t saved_current_order;   /* +0x026: USER_inputforplane stashes current_order
									*         here when issuing a radio command, then
									*         restores it on the next radio toggle. */
	uint8_t pad_027;               /* +0x027: unused (no readers/writers in binary) */
	uint16_t ai_update_rate;       /* +0x028: aiupdatetranslate[skill] */
	uint16_t ai_update_rate_copy;  /* +0x02A: PAI_initplan copies update_rate here */
	int16_t ai_target_ref;         /* +0x02C: active AI pursuit target as an
									* OBJ_REF_*: live craft idx, static-obj
									* ref, waypoint ref (OBJ_REF_WAYPOINT_BASE
									* + N), or sentinel (0x800C mother-fighter,
									* 0x800D mother-capship, 0x8000 unresolved,
									* 0xFF unset). Set by PAI_initplan from the
									* plan stream (waypoint), pai_searchformother,
									* and order handlers. Read by every PAI order,
									* pai_settarget/targetdistance, panel.c
									* incoming-fire LED, maproom.c target marker,
									* and DRAW_polydepthsort as a tie-breaker
									* (gated on mode_byte==18 && mode_subbyte==2). */
	int16_t link_target_2E;        /* +0x02E: init -1 */
	int32_t waypoint_x_cache;      /* +0x030: PAI_initplan: getworldposition.worldlocx */
	int32_t waypoint_y_cache;      /* +0x034: worldlocy */
	int32_t waypoint_z_cache;      /* +0x038: worldlocz */
	int16_t pending_radio_command; /* +0x03C: wingman command queue. Player
									* radio handlers (USER_inputforplane,
									* user_assigntarget, KEY_A/B/E) write here;
									* paiorder_evasiveorder / lookfordisable-
									* order / newtargetorder consume on the
									* next plan tick. Values: 0xFF=no command,
									* 0xFB=evasive/break-off, else obj idx of
									* a target to switch to. */
	int16_t tow_slave_ref;         /* +0x03E: OBJ_REF of a slave craft this
									* one is towing. MOVE_dispatchcraft pins
									* the slave's world position to leader_pos
									* + leader-rotated fixed offset (from
									* spec_data.engine_*). DRAW_polydepthsort
									* also reads to bind the pair in z-order. */
	/* Escortee FG index — which flight group this craft is currently
	 * escorting (for ESCORT default_order_ldr == 20). 0xFF = no escortee.
	 *   CREATE_createcraft:           init to 0xFF.
	 *   PAIFIGHT_checkescortorder:    reset to 0xFF at entry; overwrites
	 *                                 with _escortfg when searchforclosest-
	 *                                 ingroup finds a matching escort target.
	 *   PAIFIGHT_findescorterofgroup: reads to filter "who is escorting FG X?".
	 *   PAIFIGHT_escorttargetorder:   reads to pick attackers of this FG.
	 *   PAIMAN_escortmaneuver:        reads to find the escortee leader to follow. */
	uint8_t escortee_fg_idx;    /* +0x040 */
	uint8_t pad_041;            /* +0x041: struct padding; never accessed */
	uint16_t attacker_idx;      /* +0x042: obj_idx of last/active attacker;
								 * 0x00FF = none (sentinel set by PAI_initplan).
								 * Set by COLLIDE_laserhitcraft to shooter's
								 * self_idx; consumed by PAIFIGHT_*defense /
								 * coverleader for AI threat tracking. */
	uint16_t spin_done_flag;    /* +0x044: 0xFFFF once FlightObject.spin_rate has decayed to 0 */
	uint8_t special_order_flag; /* +0x046: set by PAIORDER_abortatkorder / USER_inputforplane;
								 * gate tested by PAIORDER_hyperspaceorder (!flag -> may hyperspace). */
	uint8_t missile_count;      /* +0x047: missiles fired in current missile lock.
								 * PAIFIGHT_fightershootorder ++ after firing; compared to
								 * per-genus limit (1 fighter/transport, 2 capship);
								 * PAIMAN_attackmaneuver resets to 0 on break-off. */
	uint8_t hit_count;          /* +0x048: laser hits taken during current maneuver.
								 * COLLIDE_laserhitcraft ++ per impact; compared to
								 * spec.evade_hit_threshold in PAIMAN_attackmaneuver (break-off trigger);
								 * PAIMAN_initmaneuver resets to 0. */
	uint8_t board_count;        /* +0x049: times this craft has been boarded.
								 * PAIORDER_awaitboardorder ++ each tick of boarding;
								 * compared to fg.capture_fg_used threshold.
								 * PAIMAN_boardmaneuver reads on target for first-board credit. */
	uint8_t capture_count;      /* +0x04A: # entries in capture_list (0..10).
								 * PAIMAN_boardmaneuver appends and increments, clamps at 10;
								 * PAI_finddisabledingroup uses as loop bound. */
	uint8_t pad_04B;            /* +0x04B: padding for u16 alignment of capture_list */
	uint16_t capture_list[10];  /* +0x04C..+0x05F: idnumbers of already-boarded objects,
								 * used by PAI_finddisabledingroup to skip re-targeting.
								 * Slot [0] is transiently reused as scratch for saved
								 * ai_update_rate during PAIMAN_initoutofhyperspacemaneuver /
								 * outofhyperspacemaneuver (restored on hyperspace exit). */
	uint8_t flight_flag;        /* +0x060: = fgflightflag */
	uint8_t mode_byte;    /* +0x061: PAI maneuver opcode (set by PAI_initplan from plan stream); polydepthsort
							 gates ai_target_ref check on ==18 */
	uint8_t mode_subbyte; /* +0x062: polydepthsort gates same check on ==2 */
	uint8_t pad_063;      /* +0x063: unused (no readers in any craft module) */
	int32_t maneuver_timer;  /* +0x064: dword tick-count set by PAIMAN_init*
							  *         (durations 0x0DD4, 0x1270, 0x49C, …);
							  *         PAI_aicompletioncheck mode 0x42 and
							  *         DYNAMIX_planedynamics flight_flag==5
							  *         test for ==0 to detect "stage done". */
	uint16_t ai_plan_state;  /* +0x068: PAI_initplan clears */
	int16_t max_speed_cache; /* +0x06A: cached spec.max_speed (max throttle base) */
	int16_t ai_target_a;     /* +0x06C: init 0xFFFF (no target) */
	/* Climb/dive autopilot latches (owned by PAIMAN + DYNAMIX). */
	uint8_t ai_climb_state; /* +0x06E: 1 = climbing toward waypoint_z_cache;
							 *         DYNAMIX_planedynamics clears to 0 on reach */
	uint8_t ai_dive_state;  /* +0x06F: 1 = diving, triggers DYNAMIX_pulloutdive;
							 *         pulloutdive sets to 2 on recovery */
	/* Heading (yaw) autopilot: pointed toward ai_target_heading at a rate
	 * derived from heading_rate_cache / framerate * ai_target_b * ai_heading_step. */
	int16_t heading_rate_cache; /* +0x070: cached spec.heading_rate (max yaw rate) */
	int16_t ai_target_b;        /* +0x072: yaw pacing scale (init 0xFFFF) */
	uint8_t ai_heading_state;   /* +0x074: 0=idle, 1=dec orient_heading, 2=inc, 3=reached */
	uint8_t ai_heading_force;   /* +0x075: bypass the "delta<step" exit test */
	uint16_t ai_target_heading; /* +0x076: target yaw angle (orient_heading goal) */
	uint16_t ai_heading_step;   /* +0x078: 0..0xFFFF scale into per-tick step */
	/* Roll autopilot. */
	int16_t roll_rate_cache; /* +0x07A: cached spec.roll_rate */
	int16_t ai_target_c;     /* +0x07C: roll pacing scale (init 0xFFFF) */
	uint8_t ai_roll_state;   /* +0x07E: 0=idle, 1..3=active, 4=settled */
	uint8_t pad_07F;         /* +0x07F: single pad byte */
	uint16_t ai_target_roll; /* +0x080: target roll angle (objects[i].roll goal) */
	uint16_t ai_roll_step;   /* +0x082: 0..0xFFFF scale into per-tick step */
	/* Pitch autopilot. */
	int16_t pitch_rate_cache;     /* +0x084: cached spec.pitch_rate */
	int16_t ai_target_d;          /* +0x086: pitch pacing scale (init 0xFFFF) */
	uint8_t ai_pitch_state;       /* +0x088: 0=idle, nonzero=active, 3=reached */
	uint8_t pad_089;              /* +0x089: single pad byte */
	uint16_t ai_target_pitch;     /* +0x08A: target pitch angle */
	uint16_t ai_pitch_step;       /* +0x08C: 0..0xFFFF scale into per-tick step */
	uint8_t formation;            /* +0x08E: = fgformation */
	uint8_t formation_separation; /* +0x08F: = fgseparation (cleared if hangar-spawn) */
	uint8_t craft_idx_in_fg;      /* +0x090: = craftcnt (this craft's index 0..count-1 within FG) */
	uint8_t pad_091;              /* +0x091: dead byte; no readers or writers in binary */
	int32_t push_accum_x; /* +0x092: external-push velocity accumulator (X). Decays toward 0 each tick, adds
							 to xmovedist. */
	int32_t push_accum_y; /* +0x096: same for Y */
	int32_t push_accum_z; /* +0x09A: same for Z */
	uint16_t throttle_speed; /* +0x09E: current speed, /655 for percent */
	uint16_t slam_active;    /* +0x0A0: if 0, speed display is doubled */
	uint16_t hull_damage;    /* +0x0A2: cumulative damage past shields; 0 = intact */
	uint16_t hull_strength;  /* +0x0A4: subsystem-failure + destroyed-status threshold */
	uint16_t hull_max;       /* +0x0A6: kill threshold (collide step 5) + hull-lever denominator */
	/* Subsystem loadout (immutable after createcraft). Bit i set =
	 * this craft has subsystem i installed. Bits map 1:1 to instruments
	 * 45..57 in panel_updatecockpitdamage. Same bit layout as
	 * `working_subsystems`; working_subsystems ⊆ installed_subsystems always. */
	uint16_t installed_subsystems; /* +0x0A8 */
	/* Subsystem runtime status (mutable). Bit i set = subsystem i is
	 * currently functional. Cleared in COLLIDE_damagecraft when a
	 * subsystem is knocked out; re-set on repair (PAIMAN). Widget code
	 * early-returns when its capability bit is clear. */
	uint16_t working_subsystems; /* +0x0AA */
	uint16_t subsystem_active;   /* +0x0AC: bitmask of active subsystems */
	uint16_t status_flags;       /* +0x0AE: runtime status flags */
	uint16_t dead_0B0;           /* +0x0B0: 2 bytes; cleared to 0 by createcraft and
								  *         GATE_createtraininggates, never read anywhere
								  *         in the binary. Initialized but unused. */
	uint16_t ion_drain_timer;    /* +0x0B2: ticks until weapons re-enabled
								  * after an ion-cannon hit. Bumped by
								  * COLLIDE_laserhitcraft on ion impact;
								  * counted down by TIE_updatetime; checked
								  * by LASER_weaponsfire / PAIFIGHT_*shoot /
								  * PANEL display. */
	uint8_t pad_0B4;             /* +0x0B4: cleared individually; unused */
	uint8_t was_hit_flag;        /* +0x0B5: bit 0 = hit by anyone, bit 7 =
								  * hit by player. Set by COLLIDE_laserhitcraft;
								  * read by PAIFIGHT_findattackedtargetingroup,
								  * PAIORDER_abortatk, SCORE_*. */
	uint8_t pad_0B6;             /* +0x0B6: cleared individually; unused */
	uint8_t dock_state_flags;    /* +0x0B7: bit 0x40 = collisions disabled
								  * (during dock approach), bit 0x80 = boarding/
								  * attached. Set by PAIMAN_boardmaneuver; read
								  * by COLLIDE_collisions / PAIORDER_{flyhome,
								  * enterhangar,hyperspace} / PANEL_getcraftstatus
								  * / SCORE_*. */
	uint8_t inspected;           /* +0x0B8: identification flag — 1 once
								  * the craft is known to the player.
								  * Initialised to 1 at spawn for own-side
								  * craft and for any GENUS_FIGHTER (auto-
								  * IDed by silhouette regardless of side);
								  * flipped 0->1 by COLLIDE_collisions on
								  * proximity scan and by PAIMAN board
								  * phase 2. Drives the "unknown" vs cargo
								  * string in the HUD readout (PANEL_*),
								  * the cond[4].detail "inspected" bucket
								  * (score-cond=5), and the GTT_CRAFT_ATTR
								  * sub-case 1 predicate. The previous
								  * name `friendly_to_player` came from
								  * the spawn clause and was misleading —
								  * enemy fighters get inspected=1 too. */
	/* Boarding/transfer handshake between boarder and target during
	 * PAIMAN_boardmaneuver. Values:
	 *   0 = idle / no pending tick (cleared by PAIORDER_awaitboardorder
	 *       after consuming a tick, and by CREATE_createcraft).
	 *   1 = "gave out" side (craft just unloaded cargo or was drained in
	 *       a swap). Set by PAIMAN_boardmaneuver on whichever of boarder
	 *       or target just released its contents; no direct consumer.
	 *   2 = "received" / capture-progress tick. Set by PAIMAN_boardmaneuver
	 *       each frame the dock handshake advances. PAIORDER_awaitboardorder
	 *       (running on the target craft's board plan) gates on == 2 to
	 *       increment board_count toward the capture_goal.
	 * Behavioural summary: PAIMAN re-raises 2 per dock tick, awaitboard
	 * consumes it and drops back to 0 until the next tick. */
	uint8_t boarding_state;        /* +0x0B9 */
	char cargo[16];                /* +0x0BA..+0x0C9: 16-byte cargo string from EFGStruct.contents[0/1] */
	int16_t forward_shield;        /* +0x0CA: forward shield HP (signed) */
	int16_t rear_shield;           /* +0x0CC: rear shield HP (signed) */
	uint8_t shield_power;          /* +0x0CE: shield power setting (0-2) */
	uint8_t is_player_craft;       /* +0x0CF: 1 if this is the player's craft */
	uint8_t laser_group_cnt;       /* +0x0D0: number of laser banks armed */
	uint8_t laser_power;           /* +0x0D1: laser power setting (0-2) */
	uint8_t weapon_group_cnt;      /* +0x0D2: number of weapon groups */
	uint8_t laser_type[2];         /* +0x0D3: per-bank laser type from spec.laser_type[] */
	uint8_t laser_owner_player[2]; /* +0x0D5: =1 if owner is the player craft */
	/* Per-laser-group burst countdown (AI shots remaining in current burst).
	 * PAIFIGHT_fightershootorder seeds with frwdgunnerbursts[skill_tier]
	 * whenever the AI decides to fire the group. LASER_weaponsfire
	 * decrements it for each shot and, on reaching 0, clears the
	 * corresponding laser_owner_player[g] to stop AI firing that group
	 * until the next fightershootorder decision. CREATE_createcraft
	 * only zeroes [0] at init; [1] is relied-upon to be zero from the
	 * bulk crafts[] clear. */
	uint8_t laser_burst_remaining[2]; /* +0x0D7..+0x0D8 */
	uint8_t laser_first_slot[2];      /* +0x0D9: = spec.laser_start[bank] (offset into weapon_slots) */
	uint8_t pad_0DB;                  /* +0x0DB: padding (never accessed in binary) */
	uint16_t laser_cooldown[2];       /* +0x0DC..+0x0DF: per-bank firing cooldown in ticks */
	uint8_t missile_group_cnt;        /* +0x0E0: number of missile banks armed */
	uint8_t warhead_type[2];          /* +0x0E1: per-bank warhead type (warheadconvert lookup) */
	uint8_t missile_armed[2];         /* +0x0E3: =1 init */
	uint8_t pad_0E5;                  /* +0x0E5: dead byte; no readers or writers in binary */
	uint16_t missile_state[2];        /* +0x0E6..+0x0E9: per-bank u16 cleared */
	uint16_t missile_count_total;     /* +0x0EA: cleared */
	uint8_t beam_type;                /* +0x0EC: = mission.beam_used or fg.beam */
	uint8_t beam_power;               /* +0x0ED: beam power setting (0-2) */
	int16_t beam_charge;              /* +0x0EE: beam charge level (0-9999) */
	uint16_t laser_fired;             /* +0x0F0: lasers fired this mission */
	uint16_t laser_hit;               /* +0x0F2: laser hits this mission */
	uint16_t missile_fired;           /* +0x0F4: missiles fired this mission */
	uint16_t missile_hit;             /* +0x0F6: missile hits this mission */
	uint8_t warhead_fired;            /* +0x0F8: warheads fired this mission */
	uint8_t warhead_hit;              /* +0x0F9: warhead hits this mission */
	uint8_t kills_by_species[69];     /* +0x0FA: per-species kill count */
	uint8_t pad_13F;                  /* +0x13F: dead byte; no readers or writers in binary */
	uint16_t total_kills;             /* +0x140: total kills this mission */
	WeaponSlot weapon_slots[16];      /* +0x142..+0x1A1: per-slot weapon state */
	/* +0x1A2: per-mesh state enum, indices [0..num_meshes-1]:
	 *   0 = visible / valid component target
	 *   2 = hidden (component killed-in-place, OR selectively hidden for
	 *       gate-course difficulty in gate_settraininglevel)
	 *   4 = blown off and replaced by a debris FlightObject (also used
	 *       for pre-destroyed capital-ship turrets at spawn).
	 * draw_drawcraft only tests `!= 0`; the 2/4 distinction is
	 * bookkeeping for AI / spawn / damage code.
	 *
	 * Index [num_meshes] is overlaid as a craft-level lightning-anim
	 * frame counter (0..24 indexing lightning[25]); set to 2 by
	 * blow-off / damage to kick the bolt script, advanced each tick by
	 * anim_updateanimstate for Fuselage meshes. */
	uint8_t mesh_state[40];        /* +0x1A2 */
	uint8_t mesh_rotation[40];     /* +0x1CA: per-mesh rotation angle; read by draw_drawcraft */
	uint8_t mesh_component_hp[40]; /* +0x1F2: per-mesh component HP, 0..255.
									* Preloaded by create_createcraft from
									* initialdamagestate[mesh_type]; decremented by
									* starship_damagecomponent on each hit. 0 = destroyed
									* (also flips mesh_state to MESH_STATE_HIDDEN);
									* 0xFF = indestructible (damage passes through).
									* Readers gate on 0 at starship.c:191/507/646 and
									* anim.c:621.
									*
									* Gate-craft overload: gate.c stores per-mesh rotation
									* speed in this array instead (MainHull = 0xFF,
									* others = 0). */
} CraftData;                       /* 538 bytes (0x21A) */
#pragma pack(pop)

/*
 * Genus -- runtime FlightObject.genus values. Used by COLLIDE for
 * dispatch, by CREATE_createcraft / CREATE_createcomponent /
 * CREATE_createstaticobject as the slot-class selector, by ANIM /
 * DRAW for per-genus draw routing, and by PAI for AI behavior gating.
 *
 * Values 0..5 match the GoalGenusId string-table indices in
 * string_table_ids.h ("starfighters", "transport craft", etc.).
 *
 * Genus 14 marks training-course gates (the 12 FlightObjects built by
 * gate_createtraininggates). gate_drawtraininggate consumes them via a
 * dedicated render path; COLLIDE_damagecraft's damage-skip on genus 14
 * is a consequence (gates aren't shootable), not the defining purpose.
 * Prior RE named this GENUS_INVULNERABLE before the setter was found.
 */
enum Genus {
	GENUS_FIGHTER = 0,   /* X-wing, TIE, etc. (elastic-bounce path) */
	GENUS_TRANSPORT = 1, /* shuttle / transport */
	GENUS_UTILITY = 2,   /* tug / utility craft */
	GENUS_FREIGHTER = 3, /* freighter / corvette (cap-vs-cap, dmg/=4) */
	GENUS_STARSHIP = 4,  /* cruiser / star destroyer (cap-vs-cap, dmg/=16) */
	GENUS_PLATFORM = 5,  /* platform / station (cap-vs-cap, dmg/=16) */
	/* Genera 6/7 are shooter-discriminators, NOT weapon-type tags.
	 * laser_createprojectile (laser.c:212-215) sets genus = 6 when
	 * the shooter is the player (pstate.object_idx) and genus = 7
	 * for every NPC shot. Used by the engine for scoring + collision
	 * routing. Every projectile species (fighter laser, ion cannon,
	 * proton torpedo, concussion missile) flows through both genera
	 * depending on who fires it. Weapon-type discrimination is on
	 * `ship_idx`. */
	GENUS_PROJECTILE_PLAYER = 6, /* player-fired projectile (any type) */
	GENUS_PROJECTILE_NPC = 7,    /* NPC-fired projectile (any type)    */
	GENUS_MINE = 8,              /* static mine */
	GENUS_DEBRIS = 11,           /* probe / buoy / debris (COLLIDE case 0xB) */
	GENUS_EXPLOSION = 13,        /* ember / component-debris explosion */
	GENUS_GATE = 14              /* training-course gate (gate_drawtraininggate) */
};

/*
 * Object-reference encoding (16-bit).
 * --------------------------------------------------------------------
 * The engine uses a single 16-bit namespace for every reference to a
 * "thing in the world" — stored in FlightObject.self_idx, the
 * drawpol.c global `parentobject`, the `ai_target_ref` / `tow_slave_ref`
 * fields of CraftData, the AI target-id stream, and the `obj_or_kind`
 * argument to create_getworldposition (which is the authoritative
 * decoder; see create.c).
 *
 * |-|-|
 * | range           | meaning                                                       |
 * | `0x0000..0x37FF`| FlightObject slot → objects[i].world_*                         |
 * | `0x3800..0x7FFF`| StaticObject slot = (ref - 0x3800) → staticobjects[..].world_* * 256 |
 * | `0x8000`        | sentinel "current waypoint" — indirects via fgstatus[fg].world_position |
 * | `0x8001..0xFFFF`| specific waypoint index = (ref & 0x7FFF) in fg_array[fg].way_*[..]     |
 *
 * Example: a craft in slot 5 has self_idx = 5; a static in slot 3 has
 * self_idx = 0x3803. STATIC_laserstaticcollide's ordering gate
 * compares two refs drawn from this same space.
 *
 * The "static" range is 18432 codes wide (0x4800) even though only 64
 * slots are ever allocated; the excess codes are used to tag transient
 * sprites (the hyperspace starburst in draw_drawhyperstar sits here).
 */
#define OBJ_REF_STATIC_BASE 0x3800u   /* first static-slot ref     */
#define OBJ_REF_WAYPOINT_BASE 0x8000u /* first waypoint ref; also the "current waypoint" sentinel */
#define OBJ_REF_WAYPOINT_MASK 0x7FFFu /* (wp_ref & mask) = waypoint index (0..14) */

/*
 * FlightObject — per-object runtime state in the 3D flight engine.
 * 88 bytes, 116 entries (_objects array). Contains angles, movement
 * direction, and cached orientation matrix (Side/Forward/Up).
 */
#pragma pack(push, 2)
typedef struct FlightObject {
	uint16_t idnumber;     /* +0x00: per-craft monotonic id */
	uint8_t category;      /* +0x02: species.category */
	uint8_t genus;         /* +0x03: fggenus */
	uint8_t ship_idx;      /* +0x04: species index (ship class) */
	uint8_t damage_state;  /* +0x05: damage anim byte read by ANIM, written by COLLIDE/STARSHIP/STATIC after
							  hits */
	int32_t world_x;       /* +0x06: current world position X (Q16.16) */
	int32_t world_y;       /* +0x0A: current world position Y */
	int32_t world_z;       /* +0x0E: current world position Z */
	int32_t world_x_prev;  /* +0x12: previous-frame X (for interpolation) */
	int32_t world_y_prev;  /* +0x16: previous-frame Y */
	int32_t world_z_prev;  /* +0x1A: previous-frame Z */
	int16_t pitch;         /* +0x1E */
	int16_t heading;       /* +0x20 */
	int16_t roll;          /* +0x22 */
	int16_t spin_rate;     /* +0x24: death-spin angular rate; decays at 4096/framerate per tick, adds
							  (rate/framerate)*4 to roll */
	int16_t current_speed; /* +0x26: throttle scalar (0..3600); DYNAMIX_add/sub/adjust_velocity */
	uint16_t speed_remainder; /* +0x28: per-frame MATH2_divide remainder accumulator */
	int16_t collision_radius; /* +0x2a: 4 * spec.bound_hwidth (capped 0x7FFF); used by collide */
	int16_t death_timer;      /* +0x2c: destruction countdown (COLLIDE_damagecraft writes 60); genus-specific
								 explosion fires at 0 */
	int16_t age_ticks;        /* +0x2e: age-in-ticks counter.
							   * Incremented each frame by TIE_updatetime.
							   * Initialized to 0 by CREATE_createhyperin
							   * and to 1 by LASER_createprojectile /
							   * STATIC_updatemineguns on the newly-
							   * spawned projectile object (NOT a
							   * shooter-side cooldown). USER key 'U'
							   * targets the craft with the lowest
							   * age_ticks (newest spawn) per QRC manual. */
	int16_t self_idx;         /* +0x30: own reference in the 16-bit object-ref
							   * namespace (see OBJ_REF_* above). For a craft
							   * this is the slot index; for a static sprite
							   * (set by STATIC / draw_drawhyperstar) it's
							   * slot + OBJ_REF_STATIC_BASE. */
	uint8_t ship_type_override; /* +0x32: substitute ship index when ship_idx == 89 */
	uint8_t side;               /* +0x33: IFF/team (= fgside); checkdebris sets -1 (none) */
	uint8_t decal_color;        /* +0x34: marking/decal color (drawpol_setmarkingcolors) */
	uint8_t fg_idx;             /* +0x35: owning FlightGroup index */
	uint8_t anim_frame; /* +0x36: sprite anim frame index used by ANIM_updateanimstate; createcomponent sets
						   to 2*mesh_idx */
	uint8_t anim_frame_alt; /* +0x37: alt anim slot used by ANIM_updateanimation for ship_idx==89
							   (sparks2/component-debris) */
	char move_dirty;        /* +0x38: nonzero = recalculate move vector */
	int16_t moveX;          /* +0x3A: movement direction X (Q15) */
	int16_t moveY;          /* +0x3C */
	int16_t moveZ;          /* +0x3E */
	char orient_dirty;      /* +0x40: nonzero = recalculate orientation */
	int16_t fwd_x;          /* +0x42: forward vector X (negated from calcf) */
	int16_t fwd_y;          /* +0x44 */
	int16_t fwd_z;          /* +0x46 */
	int16_t side_x;         /* +0x48: side vector X */
	int16_t side_y;         /* +0x4A */
	int16_t side_z;         /* +0x4C */
	int16_t up_x;           /* +0x4E: up vector X */
	int16_t up_y;           /* +0x50 */
	int16_t up_z;           /* +0x52 */
	CraftData* craft_ptr;   /* +0x54: per-ship runtime data pointer */
} FlightObject;             /* 88 bytes (0x58) */
#pragma pack(pop)

/*
 * StaticObject — environment object slot (mines, planets, asteroid fields).
 * 18 bytes, 64 entries (_staticobjects array). Allocated by create_createstaticobject
 * for FG species whose ship_class is 8/9/10. Animated by anim_updateanimation.
 * The byte at +0x03 stores the species index; 0 means the slot is free.
 * Angle bytes at +0x0A..+0x0C are unsigned 0..255 mapping to 0..2π (engine loads
 * them with movzx and shifts <<8 for the 16-bit angle).
 */
#pragma pack(push, 1)
typedef struct {
	uint16_t idnumber;     /* +0x00: per-object monotonic id */
	uint8_t ship_class;    /* +0x02: copy of species[].ship_class at creation time. 8=mine, 9=planet,
							  10=asteroid, 11/12=backdrop, 13=explosion, 14=gate. */
	uint8_t species;       /* +0x03: species index; 0 = slot free/destroyed */
	int16_t world_x;       /* +0x04: world coord / 256 (real pos = world_x << 8) */
	int16_t world_y;       /* +0x06 */
	int16_t world_z;       /* +0x08 */
	uint8_t pitch_byte;    /* +0x0A: unsigned 8-bit angle (<<8 for 16-bit rotation) */
	uint8_t yaw_byte;      /* +0x0B */
	uint8_t roll_byte;     /* +0x0C */
	uint8_t fg_idx;        /* +0x0D: owning FlightGroup index */
	uint16_t status_flags; /* +0x0E: 10-bit systems-online bitfield. Init = 0x3FF (all online); disruptor
							  zeroes it; only tested as `!= 0`. */
	uint8_t anim_frame;    /* +0x10: polymorphic per-tick byte updated by ANIM_updateanimation.
							*   - animated sprites (species.draw_data != NULL): frame index into draw_data[].
							*   - complex meshes (draw_data == NULL): non-zero hides the mesh from
							* STATIC_drawstaticobject.
							*   - gates (ship_class==14): reinterpreted by GATE_updategateguns as a bitfield --
							*     bit 7 = "gate has turrets alive", bits 0..5 = per-turret active mask. */
	uint8_t mine_cooldown; /* +0x11: mine-turret fire cooldown (only used when ship_class==8). Decremented by
							  frameticks/2; reset to 236 after firing. */
} StaticObject;            /* 18 bytes */
#pragma pack(pop)

#define NUM_SPECIES TIE_SPECIES_ID_COUNT
/* NUM_CRAFTS = 32 (retail).
 * Demo build used 28; retail bumped both this and NUM_OBJECTS by 4 (extra
 * space for additional craft slots). The 4-slot delta also shifts the
 * AI-fighter scan range in TIE_updatemusic from [44,76) to [48,80). */
#define NUM_CRAFTS 32
#define NUM_SPEC 69
/* NUM_WARHEADS = 48 (same in demo and retail). Warhead object slots sit
 * immediately after the craft slots, so the retail warhead range is
 * [NUM_CRAFTS, NUM_CRAFTS + NUM_WARHEADS) = [32, 80). Demo: [28, 76).
 * Verified against retail PAIFIGHT_fightershootorder / missiledefenseorder
 * (cmp eax, 50h; base reload `mov ecx, 20h`). */
#define NUM_WARHEADS 48
#define WARHEAD_SLOT_END (NUM_CRAFTS + NUM_WARHEADS) /* 80 retail, 76 demo */
/* NUM_OBJECTS = 120 (retail). Demo: 116. Last 8 slots are debris cycle
 * (currentdebrisslot starts at 112 = NUM_OBJECTS - 8). */
#define NUM_OBJECTS 120
#define NUM_STATIC_OBJECTS 64
/* Index of the special "skip-unless-debris" slot in the per-frame render
 * loop. = NUM_OBJECTS - 8 (the first debris slot). Was 108 in demo. */
#define DEBRIS_FIRST_SLOT 112

extern SpeciesEntry species_table[NUM_SPECIES];
extern CraftData crafts[NUM_CRAFTS];

/* Flight object array. Owned by tie.c. Player pointer + craft live in
 * PlayerInFlightState pstate (defined below). */
extern FlightObject objects[NUM_OBJECTS];

/* Static-object table (mines/planets/asteroids). Owned by create.c. */
extern StaticObject staticobjects[NUM_STATIC_OBJECTS];

/* Per-frame rendering cache: Last point rotated by pai_calcrotatedpoint;
 * move vector from fview_calcrotatemove. Owned by tie.c. */
extern int32_t rotatedx, rotatedy, rotatedz;
extern int32_t craftmoveX, craftmoveY, craftmoveZ;

/* Frame pacing. Owned by tie.c. */
extern uint16_t framerate;  /* ticks per second */
extern uint16_t frameticks; /* ticks elapsed this frame */

/* --- tie.c globals (flight engine main module) --- */

extern int16_t fileerror;
extern RUNTIME_MissionState mission;

extern uint8_t replayviewmode;
extern uint16_t maingameflag;
extern uint8_t cheatingflag;
extern uint16_t fullupdateflag;
extern uint16_t targetblinkstate;
extern uint8_t hyperspaceflag;
extern uint8_t hyperabortflag; /* 1 = hyperspace entry aborted mid-warp */
/*
 * FGCondPair — condition counter pair (count + sub-count per condition type).
 * The 9 condition types map to: total exits, hypered, docked, disabled,
 * captured, boarded, and various destruction/damage categories.
 */
typedef struct {
	uint8_t count;  /* total count for this condition */
	uint8_t detail; /* sub-count (cause-specific: hypered, disabled, boarded, etc.) */
} FGCondPair;

/*
 * FGStatus — per-flight-group runtime status.
 * 48 bytes, 48 entries. Tracks FG lifecycle, per-condition exit counts,
 * ID-match flags, and objective completion.
 * Field names from IDA RE of CREATE_updatefgstatus, SCORE_craftexitscoring,
 * SCORE_checkcondition, and GOALS_missiongoalsroom.
 */
#pragma pack(push, 1)
typedef struct {
	uint8_t active;            /* +0x00: FG spawned/alive (0=not yet, 1=active) */
	uint8_t waves_remaining;   /* +0x01: reinforcement waves left */
	uint16_t arrival_delay;    /* +0x02: countdown ticks to next spawn */
	uint8_t arrival_triggered; /* +0x04: arrival condition met */
	uint8_t _pad_05;           /* +0x05: unused */
	uint16_t world_position;   /* +0x06: used by CREATE_getworldposition */
	FGCondPair cond[9];        /* +0x08: per-condition exit counters (18 bytes) */
	FGCondPair cond_id[9];     /* +0x1A: per-condition ID-match flags (18 bytes) */
	uint8_t primary_status;    /* +0x2C: primary goal status */
	uint8_t secondary_status;  /* +0x2D: secondary goal status */
	uint8_t fg_complete;       /* +0x2E: bonus/completion status (1=complete) */
	uint8_t _pad_2F;           /* +0x2F: unused */
} FGStatus;                    /* 48 bytes (0x30) */
#pragma pack(pop)

extern FGStatus fgstatus[48];
extern uint8_t* farbufferptr;
extern uint8_t* farbufferptrs[265];
extern int32_t screenXRes;
extern int32_t screenYRes;
extern int32_t bytesPerPixel;
extern uint16_t yAspect; /* watdbg-owned by tie.c; 0 = square pixels */
extern int32_t screenMemWidth;
extern uint32_t vesa_page_size;
extern uint8_t vesa_window;
/* Active flight-scene display mode. */
#define TIE_FLIGHT_RES_VGA 0x13       /* 320x200x8 */
#define TIE_FLIGHT_RES_SVGA 0x101     /* 640x480x8 */
#define TIE_FLIGHT_RES_SVGA_16 0x111  /* TIE98 640x480x16 software */
#define TIE_FLIGHT_RES_SVGA_D3D 0x1FF /* TIE98 640x480x16 hardware */
extern int16_t flightResolution;

/* RECOVERY HELPER: shares the repeated TIE98 640x480 mode predicate. */
static inline int tie_is_high_resolution_flight(void) {
	return flightResolution == TIE_FLIGHT_RES_SVGA || flightResolution == TIE_FLIGHT_RES_SVGA_16 ||
		   flightResolution == TIE_FLIGHT_RES_SVGA_D3D;
}
extern uint8_t musicenabled;
extern uint8_t voiceenabled;
extern uint8_t sfxenabled;
extern void* fontptrtiny;
extern void* fontptrmicro;
extern void* newbuf;
extern void* xtransdataptr;
extern void* loadbuffer;
extern void* replaybufferstart;

/* Ship-render context shared by DRAW, FVIEW, COLLIDE, and BPFLIGHT. Both
 * model pointers address the ShipModelData after its two-byte file prefix. */
extern ShipModelData* shipimageptr;
extern ShipModelData* objectblockptr;
extern ShipModelMesh* componentblockptr;
extern CraftData* craftptr; /* current ship's CraftData (binary stores as raw int) */
/* User detail-tier knob (set from user.c::polydtl[level] or
 * option.c). The mapping is INVERTED relative to the UI: smaller
 * (negative) values correspond to HIGHER detail. Verified via
 * polydtl[4] = { 1, 1, 0, -1 } where level 3 (highest UI detail)
 * stores -1 and level 0..1 (lowest) store +1.
 *
 *   shipdetailvalue == -1  HIGH detail (user's "max" setting).
 *                          draw_getdetailptr halves z_threshold so
 *                          each mesh picks a CLOSER-range (finer)
 *                          LOD record. No mesh pruning.
 *   shipdetailvalue ==  0  Normal detail. Plain LOD walk: first
 *                          record whose distance >= z_threshold.
 *                          No prune, no fallback.
 *   shipdetailvalue == +1  LOW detail (cycle-saving). Two effects:
 *                          1) draw_gettreeorder prunes
 *                             `has_position && first_LOD.distance ==
 *                             INT_MAX` static cosmetic meshes;
 *                          2) draw_getdetailptr falls back to the
 *                             NEXT (coarser) LOD when the chosen
 *                             record's polygon header is a line
 *                             object or its face count exceeds
 *                             shipdetailpolycnt. */
extern int16_t shipdetailvalue;
/* Polygon-count cap used by draw_getdetailptr's `shipdetailvalue > 0`
 * (low-detail) fallback path: if a picked LOD record's poly_header[4]
 * (= numfaces) exceeds this cap, the engine falls back to the next
 * (coarser) LOD record. Capped progressively higher per
 * numpolydtl[4] = { 8, 12, 16, 16 } as the user's detail level rises. */
extern uint16_t shipdetailpolycnt;

/* --- World-to-eye 3x3 rotation matrix used during transforms. tie.c. --- */
extern int32_t rotworldeyeA1, rotworldeyeA2, rotworldeyeA3;
extern int32_t rotworldeyeB1, rotworldeyeB2, rotworldeyeB3;
extern int32_t rotworldeyeC1, rotworldeyeC2, rotworldeyeC3;

/* --- Perspective-projection parameters. tie.c. ---
 * screen = ((|eye_v| << perspShift) + halfPerspFactor) / eye_z   (unsigned)
 * perspShift is a small bit count (typically 16..24). perspFactor =
 * 1 << perspShift; halfPerspFactor = perspFactor >> 1 (rounding bias). */
extern uint8_t perspShift;
extern int32_t perspFactor;
extern int32_t halfPerspFactor;

/* Enable flag for the 3D skybox renderer (BACKDRP2_backdrop). */
extern uint8_t drawbackdropflag;

/* Enable flag for the parallax-debris layer (set / cleared by anim during
 * the hyperspace warp). Owned by tie.c per watdbg. */
extern uint8_t drawdebrisflag;

/* Hyperspace warp state (anim_dohyperspace state machine). Owned by tie.c
 * per watdbg. */
extern uint16_t hyperticks;
extern uint16_t hyperstarlength;
extern uint16_t hypertemp1;
extern uint16_t hypertemp2;

/* The 20 word-sized cooldown slots all live in this array; TIE_updatetime
 * decrements each non-zero entry by frameticks every frame and clamps to
 * zero. Any "timer" wired as a standalone global will not be decremented
 * and its !timer gate will only ever fire on frame 1, so every cooldown
 * goes through this enum-indexed array (0xE3894 in the binary). */
typedef enum TimerSlot {
	TIMER_MSG = 0,                /* MSG_messageprintf gate */
	TIMER_MUSIC_CHANGE = 1,       /* TIE_updatemusic throttle */
	TIMER_PRIMARY_CHECK = 2,      /* SCORE_checkobjective heavy pass */
	TIMER_ANIM_UPDATE = 3,        /* ANIM queue frame advance */
	TIMER_SHIELD_FLASH = 4,       /* PANEL_updateshields damage-flash override */
	TIMER_SHIELD_OVERLOAD = 5,    /* PANEL_updateshields balance-lever overload flash */
	TIMER_LASER_STATUS = 6,       /* LASER_weaponsfire per-second pass */
	TIMER_FG_ARRIVAL = 7,         /* CREATE_updatefgstatus arrival-cond poll */
	TIMER_FG_SPAWN = 8,           /* CREATE_updatefgstatus spawn driver */
	TIMER_SPACE_CONFIRM = 9,      /* MSG "press space" auto-cancel */
	TIMER_RADIOMSG_POLL = 10,     /* SCORE_checkobjective radio-msg poll */
	TIMER_PRI_COMPLETE = 11,      /* "primary complete" message cooldown */
	TIMER_SEC_COMPLETE = 12,      /* "secondary complete" cooldown */
	TIMER_BONUS_COMPLETE = 13,    /* "bonus complete" cooldown */
	TIMER_OBJECTIVES_FAILED = 14, /* "objectives failed" cooldown */
	TIMER_LASER_BEAM_DRAIN = 15,  /* beam_charge drain interval */
} TimerSlot;
extern int16_t timers[20];

/* --- Calc-frame rotation rows (calc{S,f,U}{1,2,3}). tie.c. --- */
extern int32_t calcS1, calcS2, calcS3;
extern int32_t calcf1, calcf2, calcf3;
extern int32_t calcU1, calcU2, calcU3;

/* --- Current craft's local frame rows (craft{S,f,U}{1,2,3}). tie.c. --- */
extern int32_t craftS1, craftS2, craftS3;
extern int32_t craftf1, craftf2, craftf3;
extern int32_t craftU1, craftU2, craftU3;

/*
 * Camera — 408-byte camera-state block matching the binary's _camera @
 * 0xED5D8 (demo) / 0xE2D8C (retail). Holds world position, view
 * orientation, and chase-cam history. Two instances are exported below:
 *   camera   — the live render camera (read by tie_updatescreen, FVIEW,
 *              draw, etc.). Owned by tie.c.
 *   replaycam — the replay viewer's user-positioned camera; in chase or
 *               lookat mode its values are copied into camera every
 *               frame. Owned by replay.c.
 *
 * Field offsets verified against TIE.EXE's per-field globals around
 * 0xED5D8 / 0xED770. replaycam re-uses some fields with different
 * semantics:
 *   replaycam.view_pitch_offset = lookat-mode flag
 *   replaycam.view_zoom_rate    = current zoom-step rate
 *   replaycam.view_zoom_flag    = zoom seed pad (init 1)
 */
typedef struct {
	int32_t x;                          /* +0x00 world X */
	int32_t y;                          /* +0x04 world Y */
	int32_t z;                          /* +0x08 world Z */
	uint16_t view_target_obj;           /* +0x0C 0xFFFF = free; else object slot */
	uint16_t cam_heading;               /* +0x0E view heading angle (16-bit BAM) */
	uint16_t cam_pitch;                 /* +0x10 view pitch angle */
	int16_t roll;                       /* +0x12 view roll */
	int16_t bank;                       /* +0x14 stick-bank input (live cam only) */
	int16_t side_angle;                 /* +0x16 view-yaw offset */
	int16_t up_angle;                   /* +0x18 view-pitch offset */
	uint8_t pilotview;                  /* +0x1A current pilot view (0..21) */
	uint8_t pilotview_save;             /* +0x1B saved view across forced switches */
	uint8_t view_dir_dirty;             /* +0x1C 1 = view rebuild needed */
	uint8_t view_saved_idx;             /* +0x1D saved pilotview to restore */
	int16_t reserved_1E;                /* +0x1E padding */
	int16_t view_saved_side_angle;      /* +0x20 saved side-angle */
	int16_t view_saved_up_angle;        /* +0x22 saved up-angle */
	int16_t view_pitch_offset;          /* +0x24 (replaycam: lookat-mode flag) */
	int16_t view_zoom_rate;             /* +0x26 (replaycam: zoom step rate) */
	int16_t view_zoom_flag;             /* +0x28 0/1 zoomed-flag (low half of dword) */
	int16_t view_zoom;                  /* +0x2A zoom factor 48..5120 (high half) */
	int16_t view_heading_offset;        /* +0x2C target-pointed view heading offset */
	int16_t cam_chase_roll_hist[60];    /* +0x2E rolling 60-tick chase-cam history */
	int16_t cam_chase_heading_hist[60]; /* +0xA6 */
	int16_t cam_chase_pitch_hist[60];   /* +0x11E */
	int16_t cam_chase_slot;             /* +0x196 ring-buffer write index */
} Camera;                               /* sizeof = 0x198 (408) */

extern Camera camera;
extern Camera replaycam;
extern int32_t worldlocx, worldlocy, worldlocz;
extern int32_t worldx, worldy, worldz; /* watdbg-owned by tie.c; per-object world-XYZ scratch */
extern int16_t objectsize;             /* watdbg-owned by tie.c; current-object size threshold */
extern uint8_t gouraudflag;            /* tie.c-owned input flag gating DRAWPOL per-vertex lighting
										* (distinct from DRAWPOL's output flag 'gauraudflag') */
extern int32_t objecteyex, objecteyey, objecteyez;

/* --- Swept-segment globals shared by the collision pipeline (tie.c). ---
 * Per-object laser muzzle sweep in world frame (current + previous tick):
 *     (laserxold, laseryold, laserzold) -> (laserx, lasery, laserz)
 * Per-target craft world position for craft-vs-laser check:
 *     (craftxold, craftyold, craftzold) -> (craftx, crafty, craftz)
 * Transformed endpoints for mesh-level collision (static/gate slot local frame):
 *     (gatex1, gatey1, gatez1) / (gatex2, gatey2, gatez2)
 * Output: scaled delta back into world frame when checkhitpolygons reports a hit:
 *     (collidexoff, collideyoff, collidezoff) */
extern int32_t laserx, lasery, laserz;
extern int32_t laserxold, laseryold, laserzold;
extern int32_t craftx, crafty, craftz;
extern int32_t craftxold, craftyold, craftzold;
extern int32_t gatex1, gatey1, gatez1;
extern int32_t gatex2, gatey2, gatez2;
extern int32_t gatenx, gateny, gatenz;
extern int32_t collidexoff, collideyoff, collidezoff;

/* --- Targeting state read by DRAW_drawcraft. tie.c. --- */
extern uint16_t bluetarget;
extern uint16_t currenttarget;
extern uint16_t currenttargetcomp;
extern uint8_t drawmarkingsflag;

/* --- FESTRING text output state (defined in tie.c) --- */

extern int16_t cursorx;
extern int16_t cursory;
extern int16_t topmargin;
extern int16_t bottommargin;
extern int16_t leftmargin;
extern int16_t rightmargin;
extern uint8_t textcolor;
extern uint8_t backcolor;
extern uint8_t dropcolor;
extern uint8_t dropflag;

/* Per-subsystem state for the in-flight damage room (DAMAGE module).
 * Indexed by SystemStringId (0..9). Live inside pstate (below); written
 * by CREATE_createmission (init), TIE_updatetime (decrement/destroy),
 * and DAMAGE_damageroom (priority edits). See damage.h. */

/* MSG module support (defined in tie.c, watdbg owner tie.c). */
extern uint16_t messageside;
extern uint16_t argtable[4];
extern uint16_t messageloghandle;
extern int16_t lwrapflag;
extern int16_t autofillflag;
extern int16_t flight_text_reserved_flag;
extern uint8_t fontflag;
extern uint8_t fontheight;
extern int16_t fontcharsize;
extern uint8_t fontlowercase;
extern void* curfontptr;
extern char tempstring[40];
extern char temp2string[40];

/* Graphics function pointers (assigned by FEINPUT_SetGraphicsPtrs) */
typedef void (*OutCharFunc)(int ch);
typedef void (*ClearWindowFunc)(void);
typedef void (*ScreenFunc)(void);

extern OutCharFunc outchar;
extern ClearWindowFunc clearwindow;
extern ScreenFunc blank;
extern ScreenFunc unblank;

extern uint8_t color_remap_table[256];

/* Flight group array and mission file header.
 * missionheader_buf layout: [WORD num_fg, WORD num_msg, WORD num_goals, EMissionStruct mission_data] = 456
 * bytes */
/* --- Input state --- */

extern int16_t inputbuttons;
extern int16_t inputkey;
extern int16_t inputdeltax;
extern int16_t inputdeltay;
extern int16_t inputdeltaroll;
extern int16_t mouseflag;
extern int16_t joystickflag;
/* Logical joystick channels — already mapped by feinput_getrawinput
 * from raw HID axes through TieInputMapping. joystickx = yaw input,
 * joysticky = pitch input, joystickroll = roll input,
 * joystickthrottle = throttle-rate input (consumed by user.c's
 * ui_apply_throttle_axis as an incremental nudge). */
extern int16_t joystickx;
extern int16_t joysticky;
extern int16_t joystickroll;
extern int16_t joystickthrottle;
extern int16_t joybuttons;
extern int16_t mousebuttons;
extern int16_t keypress;
extern int16_t deltamx;
extern int16_t deltamy;
extern int16_t mousex;
extern int16_t mousey;
extern int16_t joystickcount;
extern uint8_t graphicsmode;
extern int16_t detaillevel;

extern EFGStruct fg_array[48];

/* Authoritative player flight state and replay save block. */
#pragma pack(push, 1)
typedef struct PlayerInFlightState {
	/* +0x000 */ FlightObject* player;           /* 4 */
	/* +0x004 */ CraftData* player_craft;        /* 4 */
	/* +0x008 */ uint16_t object_idx;            /* 2 */
	/* +0x00A */ uint8_t player_fg_idx;          /* 1 */
	/* +0x00B */ uint8_t hyperin_state;          /* 1 */
	/* +0x00C */ uint8_t post_mission_shield_q4; /* 1 */
	/* +0x00D */ uint8_t _pad_0D;                /* 1 */
	/* +0x00E */ uint8_t player_spec_num;        /* 1 */
	/* +0x00F */ uint8_t _pad_0F;                /* 1 */
	/* +0x010 */ uint8_t radar_enable;           /* 1 */
	/* +0x011 */ uint8_t _pad_11;                /* 1 */
	/* +0x012 */ uint16_t target_obj_idx;        /* 2 */
	/* +0x014 */ int16_t radar_target0;          /* 2 */
	/* +0x016 */ int16_t radar_subtargets[4];    /* 8 */
	/* +0x01E */ uint8_t radar_subtarget_state;  /* 1 */
	/* +0x01F */ uint8_t player_weapon_group;    /* 1 */
	/* +0x020 */ uint8_t player_weapon_mode;     /* 1 */
	/* +0x021 */ uint8_t _pad_21;                /* 1 */
	/* +0x022 */ int16_t radar_target1;          /* 2 */
	/* +0x024 */ int16_t radar_target2;          /* 2 */
	/* +0x026 */ uint8_t space_confirm_action;   /* 1 */
	/* +0x027 */ uint8_t _pad_27;                /* 1 */
	/* +0x028: argtable[0] slot used by the messaging system. Holds an
	 * object index (objects[] subscript) consumed by msg-text expansion
	 * and by the SPACE-confirm handler in user_space_confirm. Writers:
	 * laser_warhead_lock, msg_messageupdate-driven prompts. Reads:
	 * msg.c handler, user.c SPACE handler. */
	/* +0x028 */ int16_t msg_arg_obj_idx;  /* 2 */
										   /* +0x02A: previous-frame snapshot of x_roll_mode, kept here so
											* user_update can detect the frame the player toggles roll-axis
											* mapping and reset the input slew accumulators. The binary packed
											* this into the high 16 of msg_dword purely for memory compactness;
											* splitting clarifies that the two halves are unrelated state. */
	/* +0x02A */ int16_t prev_x_roll_mode; /* 2 */
	/* +0x02C */ int16_t axis_x_accum;     /* 2 */
	/* +0x02E */ int16_t axis_y_accum;     /* 2 */
	/* Analog roll-axis slew accumulator, independent of x_roll_mode. */
	int16_t axis_roll_accum;
	/* +0x030 */ int16_t prev_inputbuttons;             /* 2 */
	/* +0x032 */ uint16_t double_tap_timer;             /* 2 */
	/* +0x034 */ int16_t player_laser_fired;            /* 2 */
	/* +0x036 */ int16_t player_laser_hit;              /* 2 */
	/* +0x038 */ int16_t player_missile_fired;          /* 2 */
	/* +0x03A */ int16_t player_missile_hit;            /* 2 */
	/* +0x03C */ uint8_t player_warhead_fired;          /* 1 */
	/* +0x03D */ uint8_t player_warhead_hit;            /* 1 */
	/* +0x03E */ uint16_t player_kills_per_species[69]; /* 138 */
	/* +0x0C8 */ uint16_t player_total_kills;           /* 2 */
	/* +0x0CA */ uint8_t rank_pilot_idx[12];            /* 12; indices 10..11 unused */
	/* +0x0D6 */ uint16_t rank_pilot_score[11];         /* 22 */
	/* +0x0EC */ uint16_t rank_pilot_kills[11];         /* 22 */
	/* +0x102 */ int32_t laser_origin_dx;               /* 4 */
	/* +0x106 */ int32_t laser_origin_dy;               /* 4 */
	/* +0x10A */ int32_t laser_origin_dz;               /* 4 */
	/* +0x10E */ int32_t laser_origin_dx_prev;          /* 4 */
	/* +0x112 */ int32_t laser_origin_dy_prev;          /* 4 */
	/* +0x116 */ int32_t laser_origin_dz_prev;          /* 4 */
	/* +0x11A */ uint8_t friendly_kill_count;           /* 1 */
	/* +0x11B */ uint8_t _pad_friendly[7];              /* 7 */
	/* +0x122 */ int16_t radio_target;                  /* 2 */
} PlayerInFlightState;                                  /* 0x124 = 292 bytes */
#pragma pack(pop)

extern PlayerInFlightState pstate;

extern MissionFile mission_file_header;

/*
 * EMissionGoal -- one mission-goal record (28 bytes on disk and in memory).
 * Loaded N-at-a-time into cut[] by create_loadmission from the .TIE file
 * (record size 28, count = mission_file_header.num_goals). In practice the
 * engine hardcodes three slots: cut[0]=primary, cut[1]=secondary,
 * cut[2]=bonus; the 4th slot exists on disk but has zero engine xrefs.
 *
 * The engine only reads 9 bytes out of 28 (subcond[0..7] + or_joined at +25).
 * The 17 bytes of `editor_name` and the 2 trailing pad bytes are loaded and
 * saved with the mission but never consulted at runtime. They presumably
 * carry the mission-editor goal description text.
 */
#pragma pack(push, 1)
typedef struct EMissionGoal {
	ECondStruct subcond[2];  /* +0..+7  pair of goal subconditions       */
	uint8_t editor_name[17]; /* +8..+24 editor-only string, never read   */
	uint8_t or_joined;       /* +25     1 = subconds OR'd, else AND      */
	uint8_t _pad[2];         /* +26..+27 editor-only padding             */
} EMissionGoal;              /* 28 bytes total                           */
#pragma pack(pop)

/* Active mission goal cache (_cut[112] = EMissionGoal[4] at 0xF4808 in the
 * binary). Owned by tie.c per watdbg. Populated by create_loadmission;
 * consumed by score_checkobjective (full primary/secondary/bonus eval),
 * goals_missiongoalsroom (display), and fsfx_checkcriticalcraft / collide_*
 * (primary-goal shortcut checks on cut[0]).
 *
 * Slot convention:
 *   cut[0] = primary goal
 *   cut[1] = secondary goal
 *   cut[2] = bonus goal
 *   cut[3] = reserved (zero-initialised; not referenced by any engine code) */
extern EMissionGoal cut[4];

/*
 * Radio-message cutscene table (_radiomsg[1440] in the binary at 0xF4268).
 * 16 entries × 90 bytes each; see msg.c / score.c consumers. Owned by tie.c
 * per watdbg.
 */
extern uint8_t radiomsg[1440];

/*
 * HardpointPos — weapon hardpoint position within a ship model.
 * 8 bytes per hardpoint, up to 16 per species.
 *
 * Field-to-slot mapping inherited from the on-disk hardpoint record
 * (see ShipModelHardpoint): the LucasArts model format consistently
 * stores v0 = X (local side), v1 = Y (local up), v2 = Z (local fwd).
 * All consumers feed (x, y, z) to pai_calcrotatedpoint(obj, side, up,
 * fwd) in that order.
 */
#pragma pack(push, 1)
typedef struct {
	int16_t x;         /* +0x00: HP.v0; local-X (side) */
	int16_t y;         /* +0x02: HP.v1; local-Y (up)   */
	int16_t z;         /* +0x04: HP.v2; local-Z (fwd)  */
	int8_t link;       /* +0x06: linked hardpoint index (-1 = none) */
	uint8_t component; /* +0x07: ship component index */
} HardpointPos;        /* 8 bytes */
#pragma pack(pop)

/*
 * SpecData — per-species rendering/combat data.
 * 236 bytes, 69 entries (_spec array). Populated by fillinspec.
 * Field names from IDA RE of FEDISKIO_fillinspec, PANEL_update*,
 * COLLIDE_*, DRAW_*, and CREATE_createcraft.
 */
#pragma pack(push, 1)
typedef struct {
	char short_name[10];    /* +0x00: HUD/radio abbreviation ("X-W", "T/F", "MIS") */
	int32_t name_ptr;       /* +0x0A: char* to species display name (from STRINGS.DAT) */
	uint8_t kill_value;     /* +0x0E: kill score value for scoring */
	uint8_t field_0F;       /* +0x0F */
	uint8_t has_hyperdrive; /* +0x10: 0 = species has no hyperdrive.
							 *   - CREATE_createcraft: when 0 (and fgversion != 9),
							 *     clears SF_HYPER_DRIVE (0x80) in subsystem_active.
							 *   - PAIORDER_hyperspaceorder: gates both leader-driven
							 *     and solo hyperspace entry.
							 *   - TIE_simulator: gates CREATE_createhyperin. */
	/* 0 = species has no shield generator. CREATE_createcraft, when
	 * has_shields == 0 AND fgversion != 8, zeroes forward_shield /
	 * rear_shield, toggles SF_SHIELDS (0x01) in subsystem_active, and
	 * toggles bit 0x0800 in installed_subsystems. PANEL_getcraftstatus also
	 * gates its "shields destroyed" status (6) on has_shields being
	 * set (else the craft was never expected to have shields). */
	uint8_t has_shields;   /* +0x11 */
	int16_t shield_points; /* +0x12: shield/combat stat */
	/* AI attack-maneuver evasive break-off threshold: once the craft
	 * accumulates `evade_hit_threshold` hits (CraftData.hit_count),
	 * PAIMAN_attackmaneuver triggers a random pitch swerve. Higher
	 * value = more hits absorbed before evasive action. */
	uint8_t evade_hit_threshold; /* +0x14 */
	/* 0 = species does not display cargo info in the target/threat
	 * panel. When set, PANEL_updatethreatname shows the craft's cargo
	 * (friendlies) or "UNKNOWN" (hostiles); when clear, it shows
	 * "NONE". Typical of transport/freighter species; fighters unset. */
	uint8_t has_cargo;       /* +0x15 */
	int16_t hull_max;        /* +0x16: seeds CraftData.hull_max */
	int16_t hull_strength;   /* +0x18: seeds CraftData.hull_strength */
	int16_t max_speed;       /* +0x1A: species base max speed.
							  *   CREATE_createcraft caches it into
							  *   CraftData.max_speed_cache and seeds
							  *   FlightObject.current_speed with
							  *   max_speed * throttle_fraction.
							  *   PAIORDER_underattackorder compares
							  *   defender vs attacker to pick the
							  *   stern-evasion maneuver (slower ->
							  *   random stern weave, faster -> zoom
							  *   dive 16). */
	int16_t max_accel;       /* +0x1C: peak acceleration (speed-units/sec)
							  *         at full throttle. DYNAMIX_adjustvelocity:
							  *         accel_step = max_accel*(0.25 + 0.75*throttle_frac). */
	int16_t decel_gain_frac; /* +0x1E: proportional-feedback gain (0..0xFFFF
							  *         fraction) applied to velocity overshoot
							  *         to compute per-frame braking rate. */
	/* Species base PITCH angular rate. CREATE_createcraft copies into
	 * CraftData.pitch_rate_cache. DYNAMIX_planedynamics computes per-tick
	 * pitch_step = frac(frac(pitch_rate/framerate, ai_target_d), ai_pitch_step)
	 * to drive pitch toward ai_target_pitch. */
	int16_t pitch_rate;          /* +0x20 */
	int16_t roll_per_pitch_frac; /* +0x22: 0..0xFFFF fraction; each frame of the pitch
								  *         autopilot, a fraction of pitch_step is bled
								  *         into roll (opposite sign) as a visual bank. */
	/* Species base ROLL angular rate. CREATE_createcraft copies into
	 * CraftData.roll_rate_cache. DYNAMIX_planedynamics drives roll toward
	 * ai_target_roll at roll_rate/framerate*ai_target_c*ai_roll_step per tick.
	 * USER_inputforplane scales it as percentage(roll_rate, 0x3000)/2 to set
	 * the player's roll responsiveness. */
	int16_t roll_rate; /* +0x24 */
	/* Species base HEADING (yaw) angular rate. CREATE_createcraft copies
	 * into CraftData.heading_rate_cache. DYNAMIX_planedynamics drives
	 * orient_heading toward ai_target_heading at heading_rate/framerate*
	 * ai_target_b*ai_heading_step per tick (with pole-wrap attitude flip). */
	int16_t heading_rate; /* +0x26 */
	/* Maximum death-spin rate cap. COLLIDE_damagecraft generates a random
	 * initial spin (HIBYTE|0x20..0x5F) then halves it repeatedly until it
	 * fits within max_spin_rate; the resulting value is stored in
	 * FlightObject.spin_rate to drive the death-tumble animation. */
	int16_t max_spin_rate; /* +0x28 */
	/* Per-axis translational (lateral) push limit. MOVE_moveobjects clamps
	 * CraftData.push_accum_x/y/z to [-max_push_rate, +max_push_rate] before
	 * integrating into world_x/y/z via xmovedist/ymovedist/zmovedist. The
	 * species' 'strafe' / lateral thrust cap. */
	int16_t max_push_rate;        /* +0x2A */
	char internal_name[9];        /* +0x2C: embedded species name ("X-wing", etc.) */
	uint8_t laser_type[2];        /* +0x35: weapon type per laser slot */
	uint8_t laser_start[2];       /* +0x37: start hardpoint index per laser slot */
	uint8_t laser_end[2];         /* +0x39: end hardpoint index per laser slot */
	uint8_t laser_count[2];       /* +0x3B: hardpoint count per laser slot */
	uint8_t laser_fire_mode[2];   /* +0x3D: fire mode per laser slot (0/1/2) */
	uint8_t missile_type[2];      /* +0x3F: weapon type per missile slot */
	uint8_t missile_start[2];     /* +0x41: start hardpoint index per missile slot */
	uint8_t missile_end[2];       /* +0x43: end hardpoint index per missile slot */
	uint8_t missile_count[2];     /* +0x45: hardpoint count per missile slot */
	uint8_t missile_fire_mode[2]; /* +0x47: fire mode per missile slot. Mirrors
								   * laser_fire_mode at +0x3D, but FEDISKIO_fillinspec
								   * never writes it (no missile equivalent of the
								   * laser hardpoint-type → fire-mode logic). Always
								   * BSS-zero in retail; CREATE_createcraft and
								   * PAIMAN_boardmaneuver read it as the `val` arg
								   * to MATH2_fraction → 0 → forced to 1 by the
								   * `if (!count) count = 1` fallback. */
	uint8_t hp_pad;               /* +0x49: 1-byte pad before hardpoint array */
	HardpointPos hp[16];          /* +0x4A: weapon hardpoint positions (16 × 8 bytes) */
	/* Local hardpoint offsets populated by FEDISKIO. HP 0x1F supplies the
	 * laser muzzle. HP 0x1B..0x1E supply active/passive docking anchors,
	 * selected by the other craft's light/heavy class. Missing anchors use
	 * speed- or shield-derived fallbacks. */
	int16_t gun_muzzle_fwd;     /* +0xCA: HP 0x1F.v2 (laser bolt forward) */
	int16_t gun_muzzle_up;      /* +0xCC: HP 0x1F.v1 (laser bolt up offset) */
	int16_t dock_fwd;           /* +0xCE: HP 0x1B-0x1E shared v2 (dock approach forward) */
	int16_t dock_passive_light; /* +0xD0: HP 0x1C (passive-side anchor, light pair / fallback) */
	int16_t dock_passive_heavy; /* +0xD2: HP 0x1B (passive-side anchor, heavy-pair only) */
	int16_t dock_active_light;  /* +0xD4: HP 0x1E (active-side anchor, engaging light target) */
	int16_t dock_active_heavy;  /* +0xD6: HP 0x1D (active-side anchor, engaging heavy target) */
	int16_t cockpit_x;          /* +0xD8: HP 0x19.v0; cockpit local-X (side) */
	int16_t cockpit_y;          /* +0xDA: HP 0x19.v1; cockpit local-Y (up)   */
	int16_t cockpit_z;          /* +0xDC: HP 0x19.v2; cockpit local-Z (fwd)  */
	int16_t engine_x;           /* +0xDE: engine position (case 0x1A) */
	int16_t engine_y;           /* +0xE0 */
	int16_t engine_z;           /* +0xE2 */
	int16_t model_scale_shift;  /* +0xE4: per-model log2 size-scale exponent
								 *        (== ShipModelData.model_scale_shift +
								 *        the loader's bound-storage compression
								 *        shift; see FEDISKIO_fillinspec). */
	int16_t bound_width;        /* +0xE6: bounding box half-width (from model width>>1) */
	int16_t bound_height;       /* +0xE8: bounding box half-height (from model height>>1) */
	int16_t bound_depth;        /* +0xEA: bounding box half-depth (from model depth>>1) */
} SpecData;                     /* 236 bytes (0xEC) */
#pragma pack(pop)

#define NUM_SPEC_DATA 69

extern SpecData spec_data[NUM_SPEC_DATA];

/*
 * ShipModelMesh — mesh component within a ship model. 64 bytes per mesh.
 * mesh_type is the MeshType enum (values from the LfdReader tool).
 *
 * Naturally aligned on the host (sizeof stays 64); fview.c uses the
 * `mesh + rotation_offset` byte-arithmetic pattern to locate the
 * ComponentRotData associated with this mesh, so the on-disk layout
 * must match the runtime layout. _Static_assert(sizeof == 64) at
 * draw.c:706 enforces this.
 */
typedef struct ShipModelMesh {
	uint16_t mesh_type;        /* +0x00: MeshType enum (4=GunTurret, 5=SmallGun, 21=RotGunTurret, ...) */
	int16_t flags;             /* +0x02: bit 1 = component can detach */
	uint8_t pad_04[6];         /* +0x04..+0x09: dead bytes; no readers in binary */
	uint16_t explosion_scale;  /* +0x0A: per-mesh debris/ember spawn scale.
								*        Read by STARSHIP_damagecomponent at 0x539c8 as
								*        the size input for the killed-component ember
								*        (>>9-model_scale_shift, clamped to u8). Unread elsewhere. */
	int32_t draw_distance;     /* +0x0C: max eye-z at which this mesh is drawn */
	int16_t center_side;       /* +0x10: X component of mesh origin in craft frame */
	int16_t center_fwd;        /* +0x12: Z component */
	int16_t center_up;         /* +0x14: Y component */
	int16_t pad_16;            /* +0x16: dead bytes; no readers (single apparent hit
								*        in STARSHIP_checkstarshiphit is an unaligned
								*        dword-load base for bbox_min_side at +0x18). */
	int16_t bbox_min_side;     /* +0x18 */
	int16_t bbox_min_fwd;      /* +0x1A */
	int16_t bbox_min_up;       /* +0x1C */
	int16_t bbox_max_side;     /* +0x1E */
	int16_t bbox_max_fwd;      /* +0x20 */
	int16_t bbox_max_up;       /* +0x22 */
	uint8_t pad_24[7];         /* +0x24..+0x2A: dead bytes; no readers in binary */
	uint8_t num_hardpoints;    /* +0x2B: count of 16-byte ShipModelHardpoint records */
	uint16_t render_offset;    /* +0x2C: self-relative u16 offset to per-mesh ShipMeshLOD table */
	uint16_t hardpoint_offset; /* +0x2E: self-relative u16 offset to hardpoint table */
	uint8_t pad_30[2];         /* +0x30..+0x31: dead bytes; no readers in binary */
	uint16_t rotation_offset;  /* +0x32: rotation-transform offset (used by FVIEW_componentrotation) */
	int16_t has_position;      /* +0x34: >0 selects pos_xyz as LOD anchor instead of center_* */
	/* LFD CRFT mesh position alternate (selected by has_position).
	 * Stored in the same non-standard side/fwd/up order as center_*
	 * — i.e. local-frame (X, Z, Y) at offsets +0x36/+0x38/+0x3A. */
	int16_t pos_side;  /* +0x36: local-X (side) */
	int16_t pos_fwd;   /* +0x38: local-Z (fwd)  */
	int16_t pos_up;    /* +0x3A: local-Y (up)   */
	uint8_t pad_3C[4]; /* +0x3C..+0x3F: dead bytes; no readers in binary */
} ShipModelMesh;       /* 64 bytes (0x40) */

/*
 * ShipModelHardpoint — weapon/component hardpoint within a mesh.
 * 16 bytes per hardpoint.
 *
 * The three coordinate slots encode a 3D position in the craft's local
 * frame, in a consistent order across every hardpoint type: local_x =
 * side, local_y = up, local_z = fwd. Each value is 16.1 fixed-point;
 * FEDISKIO_fillinspec reads each as `int16 >> 1`.
 *
 * fillinspec dispatches on `type` and writes the values to SpecData:
 *
 *   type 0x19 cockpit  -> cockpit_x   / cockpit_y   / cockpit_z
 *   type 0x1A engine   -> engine_x    / engine_y    / engine_z
 *   type 0x1B-0x1E dock-> (ignored)   / dock_*_*    / dock_fwd
 *   type 0x1F gun      -> (ignored)   / gun_muzzle_up / gun_muzzle_fwd
 *   type >= 0x78 weapon-> hp[].x      / hp[].y      / hp[].z
 *
 * local_x is unused for dock anchors and the gun muzzle — those points
 * sit on the model centerline so they have no lateral offset.
 *
 * Note: the 6-byte BSP-style hardpoints walked by STARSHIP_firelasergunner
 * are a different inline structure embedded in polygon LOD records, NOT
 * this struct.
 */
#pragma pack(push, 2)
typedef struct {
	uint8_t type;      /* +0x00: 0x19..0x1F = component, else weapon_id + 120 */
	uint8_t pad_01;    /* +0x01: unread */
	int16_t local_x;   /* +0x02: side coord, 16.1 fixed-point */
	int16_t local_y;   /* +0x04: up coord,   16.1 fixed-point */
	int16_t local_z;   /* +0x06: fwd coord,  16.1 fixed-point */
	uint8_t pad_08[4]; /* +0x08..+0x0B: unread */
	uint8_t link;      /* +0x0C: link byte for paired hardpoints */
	uint8_t pad_0D[3]; /* +0x0D..+0x0F: alignment pad; unread */
} ShipModelHardpoint;  /* 16 bytes (0x10) */
#pragma pack(pop)

/*
 * LODRecord — ship-level LOD dispatch entry. 6 bytes each. Walked by
 * draw_drawcomplexobject. NOT to be confused with ShipMeshLOD (per-mesh
 * detail-level table; opposite byte order).
 *
 * MUST stay packed: z_max sits at +2 in the on-disk layout, which is
 * not 4-aligned, so natural alignment would shift it to +4 and grow
 * sizeof from 6 to 8.
 */
#pragma pack(push, 2)
typedef struct LODRecord {
	uint16_t bsp_offset; /* +0x00: self-relative byte offset to BSP tree root */
	uint32_t z_max;      /* +0x02: maximum eye-z for this LOD */
} LODRecord;
#pragma pack(pop)

/*
 * ShipModelData — binary ship model format read from species LFD entries.
 * Variable-length. The fixed header is 34 bytes, followed by an LOD dispatch
 * table (6 * num_lods bytes) and a mesh component table (64 bytes per mesh).
 *
 * LOD dispatch table layout (each entry 6 bytes):
 *     [+0..+1] u16 bsp_offset   — self-relative byte offset to a BSP tree root
 *     [+2..+5] u32 z_max        — maximum eye-space z at which this LOD applies
 * DRAW_drawcomplexobject walks this table picking the first record whose
 * z_max >= objecteyez, then DRAW_gettreeorder traverses the BSP at
 * base + bsp_offset.
 *
 * Parsed by fediskio_fillinspec to extract dimensions, hardpoints, and
 * component reference positions.
 */
/* ShipModelData starts after the two-byte file prefix.
 *
 * MUST stay packed: shield_default (i32) sits at +0x12, which is not
 * 4-aligned, so natural alignment would shift it forward by 2 bytes
 * and break every offset relative to the on-disk header. */
#pragma pack(push, 2)
typedef struct ShipModelData {
	uint16_t prefix;  /* +0x00: constant 0x0100 across all shipped ships (format magic / version). */
	uint16_t _pad_02; /* +0x02: 2 bytes; retail does not read them. */
	uint16_t width;   /* +0x04: X-axis bound — retail FEDISKIO_fillinspec reads v48+4 */
	uint16_t height;  /* +0x06: Y-axis bound — retail reads v48+6 */
	uint16_t depth;   /* +0x08: Z-axis bound — retail reads v48+8 */
	uint16_t length;  /* +0x0A: bounding-sphere diameter — retail reads v48+10 (length/4 = bound_qdepth) */
	int32_t render_distance; /* +0x0C: max render distance — retail DRAW_drawcomplexobject reads
							  *        [objectblockptr+12] where objectblockptr = buffer+2 */
	uint16_t _pad_10;        /* +0x10: 2 bytes; no identified reader */
	int32_t shield_default;  /* +0x12: default shield center (16.16 fixed, >>17) — retail FEDISKIO_fillinspec
								reads [v48+0x12] */
	uint16_t _pad_16;        /* +0x16: 2 bytes; no identified reader */
	int32_t speed_default;   /* +0x18: default speed value (16.16 fixed, >>17) — retail reads [v48+0x18] */
	uint8_t num_meshes;      /* +0x1C: number of ShipModelMesh records after the LOD table */
	uint8_t num_lods; /* +0x1D: number of 6-byte LOD dispatch records — retail BPFLIGHT_draw_Engine reads
						 [v48+0x1D] */
	uint8_t model_scale_shift; /* +0x1E: per-model log2 size-scale exponent.
								*        Engine treats craft-local positions as
								*        2^N times their stored value where this
								*        feeds (bounds, articulation pivots, homing
								*        offsets, PIP framing, BSP-traversal
								*        exponent). At exactly N==2 the engine also
								*        routes vertex rasterisation through
								*        transfm2_geteyecoordsS2 via the HIBYTE
								*        bump on parentobject (draw.c:840-849).
								*        Retail reads [v48+0x1E]. */
	uint8_t _pad_1F;           /* +0x1F: all-zero in shipped data */
	/* Followed by: LOD dispatch table [num_lods entries] at +0x20,
	 * then mesh table [64 * num_meshes bytes]. Mesh table start = &lod_records[num_lods].
	 * Retail BPFLIGHT_draw_Engine computes this as v48 + 0x20 + 6*num_lods
	 * (0x79b5d: add ecx, 20h after imul eax, 6 on the num_lods byte). */
	struct LODRecord lod_records[]; /* +0x20: flexible array, num_lods entries */
} ShipModelData;                    /* 32 bytes fixed header + variable data */
#pragma pack(pop)

/*
 * ShipMeshLOD — per-mesh detail-LOD dispatch entry. 6 bytes each, located
 * at (ShipModelMesh + render_offset). Walked by draw_getdetailptr.
 * Terminator record has distance = INT_MAX (0x7FFFFFFF).
 */
#pragma pack(push, 2)
typedef struct ShipMeshLOD {
	int32_t distance; /* +0x00: max eye-z for this detail level */
	uint16_t offset;  /* +0x04: self-relative byte offset to polygon header */
} ShipMeshLOD;
#pragma pack(pop)

/*
 * BSPNode — binary-space-partition tree node used by draw_gettreeorder for
 * painter's-order traversal. 18 bytes per node.
 *
 *  Branch node (left_off != 0):
 *    normal_xyz   = plane normal (i16 triplet, Q15)
 *    center_xyz   = plane reference point in ship local frame
 *    left_off     = self-relative offset to LEFT child  (camera on -ve side)
 *    right_off    = self-relative offset to RIGHT child (camera on +ve side)
 *
 *  Leaf node (left_off == 0):
 *    normal/center fields are unused
 *    right_off    = mesh index (into componentblockptr[])
 */
#pragma pack(push, 2)
typedef struct BSPNode {
	int16_t normal_x;  /* +0x00 */
	int16_t normal_y;  /* +0x02 */
	int16_t normal_z;  /* +0x04 */
	int16_t center_x;  /* +0x06 */
	int16_t center_y;  /* +0x08 */
	int16_t center_z;  /* +0x0A */
	int16_t left_off;  /* +0x0C: 0 marks a leaf */
	int16_t right_off; /* +0x0E: at leaves, this is the mesh index */
} BSPNode;
#pragma pack(pop)

/* ------------------------------------------------------------------
 * Additional tie.c-owned globals (per watdbg). Ordered by functional
 * area; every entry below corresponds to a `_<name>` symbol emitted
 * into the tie.c OBJ in the shipped binary.
 * ------------------------------------------------------------------ */

/* Per-mission state (xtimer/fediskio/create seed these). */
extern uint8_t acceleratedtimesetting; /* accelerated-game-clock gear shift */
extern uint16_t baseframerate;         /* mission base framerate (default 20) */
extern uint16_t tickcounter;           /* monotonic tick counter (xtimer) */
extern uint16_t missionversion;        /* .TIE file format version */
extern uint8_t mtimer_state;           /* mission-timer FSM state */
extern uint8_t mtimer_min;             /* mission-timer minutes */
extern uint8_t mtimer_sec;             /* mission-timer seconds */

/* Per-frame flags. */
extern int16_t targetblinkflag; /* blink toggle for the HUD target box */
extern uint8_t calcframerate;   /* 1 = refresh HUD framerate readout this frame */
extern uint8_t entercombatflag; /* set when the player leaves training for combat */
extern uint8_t player_ejected;  /* set by user_ejectcamera; gates flight beam/laser/HUD post-eject */
extern uint8_t lightflag;       /* global directional light is enabled this frame */
extern uint8_t colorcycleflag;  /* palette cycling active */
extern uint8_t blankcondition;  /* bit 0 = fade-to-black active */
extern uint16_t gatecolor;      /* draw color used for training gates */

/* Object/craft id counters. */
extern uint16_t idnumber;          /* monotonic per-craft id */
extern uint16_t currentdebrisslot; /* cycled DEBRIS_FIRST_SLOT..NUM_OBJECTS-1 by create_checkdebris (retail:
									  112..119, demo was 108..115) */
extern uint8_t shieldblink; /* which shield half just took a hit: 0=forward, 1=rear (byte_EB75B). Paired with
							   timers[TIMER_SHIELD_FLASH]. */
extern int32_t approxdist;  /* last collide_roughdistance3d result */
extern int32_t roughdistance; /* per-frame rough distance scratch */
/* Per-frame bitmap queue length consumed by anim_sort_and_draw_bitmaps. */
extern int16_t numbitmaps;
extern int16_t frontResolution; /* front-end screen resolution selector */
extern int32_t maxPixelsDeep;   /* front-end pixel-height cap */

/* Starfield color-table inputs (see rtsvga2_drawstars). */
extern uint8_t stars[512];
extern uint8_t starcol1;

/* Starship detail/explosion LOD thresholds. */
extern uint16_t starshipdetail;
extern uint16_t starshipexplodetail;

/* Replay / clip-recording state (the rest live in replay.h). Watdbg owns
 * these here in tie.c; the higher-level orchestration + camera state is
 * in replay.c. */
extern int16_t replaypercent;
extern int16_t recordingreplay;
extern int32_t replaytotalcnt;
extern int32_t replaymaxcnt;
extern char replayclipname[14];
extern char replaystartfile[10];    /* "start.rpy"  - per-replay state snapshot */
extern char replaysavegamefile[13]; /* "savegame.rpy" - in-flight checkpoint */
extern char inputspoolfile[10];     /* "input.spl"  - on-disk frame spool */
extern uint32_t replaytotalcntdown; /* playback counter (counts up to replaytotalcnt) */
extern int16_t replaybuffercntdown; /* per-frame pacing decrement */
extern uint16_t replayrandomseed;   /* RNG seed captured at record start */
extern int16_t replayviewtype;      /* view kind saved per frame */
extern int16_t lastreplayviewtype;
extern int16_t replayobjectnum;
extern int16_t replaydebounce;
extern uint8_t endgamereplayflag; /* 1 = suppress UI during end-mission spool */
extern uint8_t replayescapeflag;
extern uint8_t replayfpctr;
extern uint8_t lastreplayname;
extern uint8_t replayfg;
extern uint8_t updateactionflag; /* 1 = replay is actively stepping frames */
extern uint16_t replayavailable; /* 1 if a saved clip is loadable */
extern uint8_t replayspoolflag;  /* 1 = spool input frames to disk */
extern void* replayptr;          /* write/read cursor into replaybuffer */
extern uint16_t replaybuffercnt; /* frames in the current page */

/* Mission file / runtime paths. */
extern char missionfilename[64];

/* VESA paging (see rtsvga2 / logbuf2). */
extern uint32_t vesa_grains_per_page;

/* Lighting vectors. rotlight{X,Y,Z} = world-space directional light
 * rotated into the current craft's local frame. */
extern int32_t rotlightX, rotlightY, rotlightZ;
extern int16_t thicknessMultiple; /* drawpol silhouette thickening */

/* Front-end graphics dispatch (FEINPUT_SetGraphicsPtrs wires these). */
extern void* initgraph;
/* Install BGR palette entries from host memory. */
extern void (*buildpalette)(const uint8_t* rgb_src, uint16_t start_idx, uint16_t count);
extern void* savepalette;
extern void* restorepalette;
extern uint32_t (*calcposition)(uint16_t x, uint16_t y);
/* First arg is a pointer to the shape data. Retail used `int` because all
 * pointers were 32-bit; on LP64 the (int) cast truncated shape pointers
 * and every subsequent deref landed on a garbage address. */
extern void (*drawshape)(const void* shape, int16_t x, int16_t y, int16_t skip_color, uint16_t flip_x);
extern void (*fillbox)(uint16_t left, uint16_t top, uint16_t right, uint16_t bottom);
extern void* savebox;
extern void* restorebox;

/* --------------------------------------------------------------------------
 * TIE module: per-frame engine driver (tie.c).
 * --------------------------------------------------------------------------
 *
 * Mirrors the binary's TIE_* functions. tie_Push_Simulator_Task owns the
 * lifecycle of one mission as a tie_core task; tie_doframe runs the
 * per-frame pipeline; tie_updatescreen paints the world; tie_updatetime
 * advances clocks and craft-state timers; tie_updatemusic re-evaluates
 * the iMUSE state machine; the three eye-space helpers
 * (tie_getobjecteyexyz / tie_check[static]objecteyexyz) project a world
 * point into camera space and cull it.
 *
 * tie_Push_Simulator_Task pushes the multi-phase simulator task on the
 * tie_core task stack. replay_mode = 0 selects a live mission, non-zero
 * selects replay-only playback. The task drives the hyperspace cinematic
 * and mission flight loops as sub-tasks; the host loop pumps TieRuntime_Tick
 * until the simulator task pops. */
void tie_Push_Simulator_Task(int replay_mode);

/* Release port-owned model and texture caches after a flight or BPFlight
 * viewer has stopped using them. */
void TieFlightRuntime_ReleaseRecoveredResources(void);

/* Push the live flight-mission task. Called from replayio's "re-enter
 * sim from viewer" path; tie_simulator's INIT phase also drives this
 * via the internal setup helper. The task pops when mission.end_flag
 * becomes non-zero. */
void tie_Push_FlightMission_Task(void);

void tie_initflightresolution(void);

/* --- Input configuration --- */

/* Per-frame flight driver. Returns true if a frame's work ran (or
 * was deliberately skipped via pause / end_flag / mapflag — those
 * are still "ran" from the caller's perspective: the engine made
 * progress, the next TieRuntime_Tick can advance further). Returns false
 * if the call was a time-wait early-return: the host's PIT-tick
 * accumulator hadn't reached 4 yet, so no work happened. Callers
 * driving the flight loop from a task should YIELD on false (the
 * xtimer cursor only advances between tie_ticks; spinning here
 * would never satisfy the budget). */
bool tie_doframe(void);

void tie_updatescreen(void);

int tie_makelocallights(int obj_idx);
int tie_makelocallights_tie98(FlightObject* source_object);
extern int32_t g_localLightsEnabled;
extern int32_t g_explosionLightBase;
void tie_getobjecteyexyz(uint16_t obj_idx);
int16_t tie_checkobjecteyexyz(uint16_t obj_idx, uint16_t bound);
int16_t tie_checkstaticobjecteyexyz(int16_t wx, int16_t wy, int16_t wz, uint16_t bound);
void tie_updatetime(void);
void tie_updatemusic(void);

/* --------------------------------------------------------------------------
 * tie.c globals not declared above (used by tie_doframe / updatescreen).
 * -------------------------------------------------------------------------- */

/* Per-frame "skip rendering" flag set by USER_userinterface when the
 * mission map is up; tie_doframe gates the world-render block on it.
 * Owned by tie.c. */
extern uint8_t mapflag;

/* Cocoon state for the replay fast-forward UI: when fastforwardflag != 0
 * tie_doframe accumulates frameticks against fastforwardtimer (236 reload)
 * and only renders when the timer drains. */
extern uint8_t fastforwardflag;
extern int16_t fastforwardtimer;

/* Hyperspace-cinematic gate consumed by tie_simulator. Was the binary's
 * byte_D354C in retail; named transitions_on in the demo's tie.c sources.
 * Set by SHELLEXT preferences ("transitions" toggle). */
extern uint8_t transitions_on;

/* Last-frame XTIMER read (in ticks). tie_doframe spins reading
 * XTIMER_Time_Elapsed until tickcounter >= 4, then snapshots it here. */
extern int16_t lastcounter;

/* MissionClock — the watdbg `_date[8]` storage at retail 0xE6384..0xE638B,
 * 8 bytes total. Source-level it's a single struct; the Watcom backend
 * emits per-byte / per-dword reads, which IDA labels as `byte_E6387`,
 * `dword_E6388`, etc. — those are *not* separate globals.
 *
 * Tick semantics (driven entirely by tie_updatetime):
 *   - subsec decrements by `frameticks` every frame.
 *   - On underflow (<=0), subsec += 236 and second++. Cascade through
 *     minute and hour; hour wraps at 24.
 *
 * Layout parity with the binary is required: replayio.c serialises the
 * 8-byte block by &_date / sizeof(_date), and the static pointer table
 * at retail 0xC7354 dumps `(start=&_date, end=&_date+8)`. */
#pragma pack(push, 1)
typedef struct MissionClock {
	uint8_t reserved_0; /* +0  no runtime readers in any function */
	uint8_t reserved_1; /* +1  scanned; the binary preserves the   */
	uint8_t reserved_2; /* +2  bytes verbatim through save/replay. */
	uint8_t hour;       /* +3  mission elapsed hour (0..23) */
	uint8_t minute;     /* +4  mission elapsed minute (0..59) */
	uint8_t second;     /* +5  mission elapsed second (0..59) */
	int16_t subsec;     /* +6  sub-second tick countdown; refilled */
						/*     with 236 each second by tie_updatetime. */
} MissionClock;         /* 8 bytes */
#pragma pack(pop)

extern MissionClock _date;

/* 8-byte mission-timer countdown state (watdbg _timeleft[8]). */
extern uint8_t timeleft[8];

/* fopen mode literal "rb" — owned by tie.c, used by every module that
 * opens a binary asset file. Watcom kept it as a single global to share
 * its address across translation units. */
extern const char _readmode[3];

/* Per-system damage state for the player. Each slot tracks one of the 10
 * cockpit subsystems (radar, lasers, shields, etc.). */
extern uint16_t player_system_damage_hash[10];
extern uint16_t player_system_repair_timer[10];

/* iMUSE per-frame evaluation state. tie_updatemusic updates them each
 * frame; held as globals for .bss persistence so the replay state-dump
 * captures them. */
extern int16_t music_state;
extern uint16_t music_intensity;
extern uint8_t musicflag;

/* Accelerated-time tick counter (binary u8 in both demo and retail). */
extern uint8_t acceleratedtimectr;

/* Hyperspace/detail tier globals (owned by tie.c; several modules read
 * them). */
extern int16_t hyperspacedetail;

/* HUD target-blink tick countdown. */
extern int16_t blinkticks;

/* Persistent user "Color Cycling" option (binary's byte_EB766). Set 1 by
 * tie_simulator at start; toggled by OPTION_optionsroom row 6. Read by
 * GAMESND_Host_Int alongside colorcycleflag to gate palette cycling. */
extern uint8_t palette_cycle_user;

/* Always zero because the host does not use DOS expanded memory. */
extern int panels_in_ems;

/* Write-only TIE_simulator initialization flags. */
extern uint8_t deadflag_EB76C;
extern uint8_t deadflag_EB774;

/* Mission-file flag: bit 0 forces eject-pod rescue (story gate). */
extern uint8_t rescue_override_flag;

/* Quicksave / quick-recall target-slot arrays; indexed by scan key. */
extern uint16_t quickrecall_fold_base[256];
extern uint16_t quicksave_fold_base[256];

/* View-angle lookup table for the 0..9 numpad view keys. */
extern int16_t squarerootable[512];

/* Mission-file timestamp + RNG seed (sampled at room load). */
extern uint8_t mfile_time_min, mfile_time_sec;
extern int16_t mfile_rnd_seed;

/* Cockpit instrument knockout flag (panel_updatecockpitdamage). */
extern uint8_t byte_F8FAB;

/* Transient user-side palette-cycling enable flag. */
extern uint8_t colorcycleuserflag;

/* "View Film" menu label pointer (resolved from strings.dat by fediskio). */
extern void* viewfilmstr;

#endif
