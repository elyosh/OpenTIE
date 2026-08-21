#ifndef __PAI_H__
#define __PAI_H__

#include <stdint.h>

#include "tie/tie.h"

/* ---- Order-handler dispatch table type. -------------------------------
 * Each entry returns non-zero to tell the plan VM "consume the following
 * next_order byte and transition", or 0 to stay in the current slot.
 * The handler bodies read/write the PAI context globals directly; they
 * do not take arguments (Watcom register-passing convention in the binary
 * left EAX/EDX undisturbed when calling through the table). */
typedef int16_t (*OrderFunc)(void);

/* ---- Public functions (23 total, 1:1 with IDA PAI_*). ------------------ */

/* Per-frame driver (tie_doframe). Walks objects[0..0x1C); for every active
 * non-category-0 flight slot whose flight_flag is neither 3 nor 4, kicks
 * the plan VM once the per-craft ai_update_rate_copy countdown expires. */
void pai_updateplaneai(void);

/* Initialize the plan for the craft currently in the PAI context.
 * Consumes planptrs[craftptr->current_order][0..1] as (waypoint_selector,
 * initial_mode_byte) and sets up ai_target_ref / waypoint_*_cache /
 * ai_plan_state / mode_byte / attacker_idx / ai_update_rate_copy. */
void pai_initplan(void);

/* One step of the plan VM. Reads opcode bytes from pai_plan_ptr; on a
 * true return from ordersfunctionptrs[opcode], consumes the next byte as
 * the new order (0x41 = wildcard pulled from pai_flag_6d) and reboots the
 * plan via pai_setupcraftaivars + pai_initplan. */
uint8_t pai_updatecraftplan(void);

/* Prime the per-craft PAI context from objects[obj_idx]. Returns the
 * plan body pointer (planptrs[current_order] + 2). Called once per tick
 * before dispatching any PAI handler. */
uint8_t* pai_setupcraftaivars(uint16_t obj_idx);

/* 16-bit skill classifier: 0 / 1 / 2 for skill < 0x8000 / <0xC000 / else. */
int pai_getprof(uint16_t skill);

/* Scan objects[0..0x1C) for the FG leader (craft whose
 * leader_obj_idx == 0xFF and fg_idx == arg). Returns slot or 0xFFFF. */
uint16_t pai_searchformother(uint16_t fg_idx);

/* Gate: obj_ref is worth attacking AND within engagement range.
 * pursue_hot != 0 extends the radius by 4/3. */
char pai_checktargetforattack(uint16_t obj_ref, int16_t pursue_hot);

/* Filter: obj_ref is alive, not in a transition mode, and not identical
 * to the scoring craft with impossible shields. */
char pai_worthytarget(uint16_t obj_ref);

/* Proximity test within a skill-tiered combat radius (2560..3520 world
 * units <<8). */
char pai_checkcombatarea(uint16_t obj_ref);

/* Two-group presence probe; returns 1 if any active object matches the
 * (combine_op, group1, group2) selectors. Dead in the shipped binary. */
int pai_searchforcraftingroup(uint8_t group_type1, uint16_t group_id1, int16_t combine_op,
							  uint8_t group_type2, uint16_t group_id2);

/* True if the given FG has any craft currently available to be disabled. */
char pai_lookfordisableswitch(uint16_t fg_idx);

/* Find a disable target for the current AI entry; wraps
 * pai_finddisabledingroup twice: first using the AI entry's pri/sec
 * group selectors, then on miss using its target-type/target-id pair. */
uint16_t pai_checkfortargetstodisable(uint16_t ai_entry);

/* Chebyshev-weighted taxicab proximity test against the PAI context
 * world position. Writes roughdistance. Returns 1 if radius_24_8 >
 * roughdistance, else 0. */
int16_t pai_roughproximitycheck(uint16_t obj_ref, int32_t radius_24_8);

/* Cache craftptr->ai_target_ref's world position into waypoint_*_cache. */
void pai_settarget(void);

/* Propagate (formation, separation) to the leader craft and every
 * follower in the FG. Dead in the shipped binary. */
void pai_setformation(uint16_t leader_obj_idx, uint8_t formation, uint8_t separation);

/* Exact polar distance between two world references; writes
 * trig2_polardistance / trig2_xyangle / trig2_zangle. */
void pai_distancebetween(uint16_t a_ref, uint16_t b_ref);

/* Chebyshev-weighted taxicab 3D distance between two world references;
 * writes roughdistance. */
void pai_roughdistancebetween(uint16_t a_ref, uint16_t b_ref);

/* Rotate local-space (side, up, fwd) by obj's orientation; writes
 * rotatedx / rotatedy / rotatedz. Refreshes the orientation basis
 * from heading/pitch/roll if obj->orient_dirty is set. */
void pai_calcrotatedpoint(FlightObject* obj, int16_t side, int16_t up, int16_t fwd);
int32_t pai_RotateLocalVectorToWorldScratch(FlightObject* obj, int side, int up, int fwd);

/* Unused getter; returns CraftData[obj_idx].hull_max. Dead. */
uint16_t pai_getcraftdoomedlevel(uint16_t obj_idx);

/* Polar distance from the current AI craft to its cached waypoint. */
void pai_targetdistance(void);

/* Closest-available search for a disable target. Returns obj_ref
 * (<0x3800 flight, >=0x3800 static) or 0xFFFF. */
uint16_t pai_finddisabledingroup(uint8_t group_type1, uint16_t group_id1, int16_t combine_op,
								 uint8_t group_type2, uint16_t group_id2);

/* Order-completion test: is the (order_code, ai_entry) sub-goal
 * satisfied? Used by PAIORDER_completegohomeorder. */
int pai_aicompletioncheck(uint16_t order_code, uint16_t ai_entry);

/* Is obj_ref a legal target under the current AI entry's pri/sec and
 * target[0]/target[1] selectors? */
char pai_isobjectvalidtarget(uint16_t obj_ref);

/* ---- Active-AI context block -----------------------------------------
 * 52-byte struct at 0xF8F48 (watdbg _ai[52]). Set by pai_setupcraftaivars
 * before each per-craft AI tick; mirrors the binary's single-symbol
 * global. PAIMAN_boardmaneuver / PAIMAN_dropoffmaneuver snapshot+restore
 * the whole block when they temporarily re-home craftptr to a docked
 * target craft. */
#pragma pack(push, 2)
typedef struct AiContext {
	/* active_obj_idx — FlightObject slot of the craft currently being
	 * processed by the PAI tick. Set by pai_setupcraftaivars(obj_idx)
	 * and consumed by every PAIMAN/PAIORDER/PAIFIGHT handler that calls
	 * ai.active_obj_idx to read the owning craft's world pose. */
	uint16_t active_obj_idx; /* +0x00 */
	CraftData* active_craft; /* +0x02: shortcut to objects[active_obj_idx].craft_ptr */
	uint16_t leader_obj_idx; /* +0x06: FG leader's obj slot, 0xFF = self */
	CraftData* leader_craft; /* +0x08: leader's CraftData (== active_craft when self-led) */
	uint16_t fg_idx;         /* +0x0C: active craft's FG index */
	uint16_t ai_entry_count; /* +0x0E: bound on EFGStruct.ai[] loops (from craftptr->ai_state_1C) */
	int32_t world_x;         /* +0x10: active craft's world pose snapshot */
	int32_t world_y;         /* +0x14 */
	int32_t world_z;         /* +0x18 */
	/* skill_tier — 0/1/2 derived from craftptr->skill_value (<0x8000 /
	 * <0xC000 / else). Indexes per-skill LUTs (delayturninside[],
	 * frwdgunnerranges[], frwdgunnerbursts[]). */
	int16_t skill_tier;  /* +0x1C */
	uint16_t plan_order; /* +0x1E */
	uint8_t* plan_ptr;   /* +0x20: plan bytecode cursor */
	/* live_target_only — PAI scanner gate: when set, target-finders reject
	 *   craft with status_flags == 0 (all systems offline → disrupted /
	 *   destroyed) and same-side docked targets. Set by order_class 19
	 *   (leadergohomeorder), the special_order plan slot, and species-76
	 *   mine turrets; cleared elsewhere.
	 * staged_next_order — written by the slot-40..43 order handlers
	 *   (completegohome, completefollow, waitgoother, orderswitch) with
	 *   ordersldr[new_order] or ordersflw[new_order]; the plan VM
	 *   substitutes it for the next-order byte when the literal byte is
	 *   0x41 (wildcard). */
	uint8_t live_target_only;  /* +0x24 */
	uint8_t staged_next_order; /* +0x25 */
	/* search_flags + search_xyz — gunner/disable target-search scratch
	 * used by PAIFIGHT. bits: 0x01 dog-pile cap, 0x04 combat-area gate,
	 * 0x10 range cap, 0x20 use search_xyz instead of active world pos. */
	uint8_t search_flags; /* +0x26 */
	uint8_t _pad_27;      /* +0x27 */
	int32_t search_x;     /* +0x28 */
	int32_t search_y;     /* +0x2C */
	int32_t search_z;     /* +0x30 */
} AiContext;              /* 52 bytes (0x34) */
#pragma pack(pop)

extern AiContext ai;

/* 69-entry table of plan bytecode pointers. Indexed by
 * CraftData.current_order. Each entry points at a 2-byte header
 * (waypoint_selector, initial_mode_byte) followed by a length-prefixed
 * sequence of (order_op, next_order) pairs terminated by 0x00. */
extern const uint8_t* planptrs[69];

/* The two "wrap-up" plans whose maneuver function self-modifies plan[3] to
 * communicate the FG-specific next order to the plan VM. Both maneuvers
 * (PAIMAN_outofhangar / PAIMAN_outofhyperspace) rewrite plan[3] with
 * ordersldr/ordersflw[ai[0].order] before returning 1, which the plan VM
 * then consumes as the transition target. Without that write the craft
 * stays in mode_byte 26/22 forever. Plans are declared non-const here. */
extern uint8_t exithangarplan[5];
extern uint8_t outofhyperspaceplan[5];

/* OPTIONAL enhancement (non-faithful), default 1 = on. When set, same-flight-
 * group AI craft (fighter/transport/utility) whose hulls overlap are gently
 * pushed apart each frame (position only — heading and guns stay locked on
 * their target, unlike an aim offset which makes them fire off-target). The
 * player is never pushed. Set to 0 for byte-faithful behaviour (the original
 * has no fighter-vs-fighter separation). See MOVE_moveobjects. */
extern int8_t pai_friendly_separation;

/* ordersfunctionptrs is paiorder.c-owned per watdbg; declared in paiorder.h. */

/* --- Legacy compatibility wrappers ---
 * A few modules refer to the older "tie.c owns roughdistance" shim
 * interface (pai.h comment blocks). Kept as-is so no includes break. */

#endif /* __PAI_H__ */
