#ifndef __PAIFIGHT_H__
#define __PAIFIGHT_H__

#include <stdint.h>

#include "tie/score.h" /* GoalTargetType */

/* -- Public shooter state shared with STARSHIP / STATIC. Written by the
 *    gunner defense/offense handlers as "where did my turret fire from".
 *    All three are 24.8 fixed-point world coordinates. */
extern int32_t shooterx;
extern int32_t shootery;
extern int32_t shooterz;

/* -- Last FG chosen by searchforclosestingroup; consumed by
 *    paifight_checkescortorder_entry to stamp craftptr->escortee_fg_idx. */
extern uint8_t escortfg;

/* -- Tail 14 bytes of watdbg's _ai[52] scratch at 0xF8F6E..0xF8F7B.
 *    Shared between the PAIFIGHT scanners.
 *
 *      ai.search_flags bits
 *        0x01  require paifight_countattackers pass (dog-pile cap)
 *        0x02  scan-marker: written by scan/allgone callers, never
 *              consumed (dead in the shipped binary).
 *        0x04  require pai_checkcombatarea pass
 *        0x10  cap distance at 0x10000 fixed-point units (gunner range)
 *        0x20  use (ai.search_x,y,z) instead of the AI's own world pos
 *
 *      ai.search_x/y/z : search origin, set only by gunneroffense. */
/* search_flags + search_xyz moved into AiContext (pai.h). Use
 * ai.search_flags / ai.search_x / ai.search_y / ai.search_z. */

/* -- Per-skill gunner tables (watdbg-owned by paifight.c).
 *      frwdgunnerranges[3]  fixed-point 24.8 max gunner engagement range
 *                           per ai.skill_tier (0/1/2).
 *      frwdgunnerbursts[4]  burst-shot count per skill tier. */
extern const uint32_t frwdgunnerranges[3];
extern const uint8_t frwdgunnerbursts[4];

/* -- Combat target scanners.
 *    Selector bytes (pri_type/pri_id/sec_type/sec_id) are EAIStruct
 *    single-byte fields (GoalTargetType selectors). op is a sign-
 *    extended byte from the EAI stream; 1 = AND, else OR. */

int16_t paifight_findtargetingroup(uint8_t pri_type, uint8_t pri_id, int16_t op, uint8_t sec_type,
								   uint8_t sec_id);

int16_t paifight_findescorterofgroup(uint8_t pri_type, uint8_t pri_id, int16_t op, uint8_t sec_type,
									 uint8_t sec_id);

int16_t paifight_findattackedtargetingroup(uint8_t pri_type, uint8_t pri_id, int16_t op, uint8_t sec_type,
										   uint8_t sec_id);

uint16_t paifight_findgunnertargetingroup(uint8_t pri_type, uint8_t pri_id, int16_t op, uint8_t sec_type,
										  uint8_t sec_id);

int16_t paifight_searchforclosestingroup(uint8_t pri_type, uint8_t pri_id, int16_t op, uint8_t sec_type,
										 uint8_t sec_id);

int16_t paifight_futuretargets(uint8_t pri_type, uint8_t pri_id, int16_t op, uint8_t sec_type,
							   uint8_t sec_id);

/* -- Priority-selector checks with fallback to the general selector.
 *    ai_entry is the EAIStruct index (0..2) within the AI's FG. */

int16_t paifight_checkfortargets(uint16_t ai_entry);
int16_t paifight_checkforescortertargets(uint16_t ai_entry);
int16_t paifight_checkforattackedtargets(uint16_t ai_entry);
int16_t paifight_checkforfuturetargets(uint16_t ai_entry);

/* -- Scan dispatchers: classify the AI entry's order via ordersldr[] and
 *    route to the matching target-finder. */
int16_t paifight_scanfortargetswitch(uint16_t ai_entry);   /* PAIORDER_orderswitchorder */
int16_t paifight_scanfortargetsallgone(uint16_t ai_entry); /* PAI_aicompletioncheck */

/* -- Leaf helpers. */

/* Dog-pile cap predicate: "is target_obj_idx under-attacked enough for
 * another AI to pile in?". Counts active objects whose ai_target_ref ==
 * target_obj_idx in modes 11/12/23, compares to a cap that depends on
 * target genus / side / mission.difficulty when target is the player. */
int paifight_countattackers(uint16_t target_obj_idx);

/* Pick a random hull-type mesh (mesh_type 1 or 3) on target_obj_idx's
 * ship. Returns the mesh index, or 0 when target is out of range. */
int16_t paifight_gethullcomponent(uint16_t target_obj_idx);

/* -- OrderFunc entries (returns transition flag; 0 = stay on current
 *    order, non-zero = consume next_order). */

int16_t paifight_fightershootorder(void);      /* plan slot  5 */
int16_t paifight_gunnerselfdefenseorder(void); /* plan slot  6 */
int16_t paifight_gunneroffenseorder(void);     /* plan slot  7 */
int16_t paifight_missiledefenseorder(void);    /* plan slot  8 */
int16_t paifight_scanfortargetorder(void);     /* plan slot  9 */
int16_t paifight_coverleaderorder(void);       /* plan slot 13 */
int16_t paifight_followleadatkorder(void);     /* plan slot 14 */
int16_t paifight_escorttargetorder(void);      /* plan slot 23 */

/* -- Direct entry from pai_updatecraftplan when default_order_ldr == 20
 *    (Escort) on the player's own craft. */
void paifight_checkescortorder_entry(void);

#endif /* __PAIFIGHT_H__ */
