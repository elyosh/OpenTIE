#ifndef __CREATE_H__
#define __CREATE_H__

#include "tie/tie.h"
#include <stdint.h>

/* --- Public API (20 functions, 1:1 with binary CREATE_*) --- */

/* Load a .TIE mission file: zero object tables, read header/FG/radio/cutscene
 * records, flag loaded species, roll special_craft counts, seed the backdrop.
 * For training missions patches fg[0] with mission.train_craft_type_src.
 * Returns 1 on success, 0 if file open/read failed. */
int16_t create_loadmission(const char* filename);

/* Hyperspace-in transition: rebuild mission state and clear non-player
 * FlightObjects + static slots. Resets player to heading=0x4000, pitch=0.
 * Returns player_craft pointer. */
CraftData* create_createhyperin(void);

/* Per-mission initialization run after loadmission. Zeros fgstatus entries,
 * computes each FG's cond[0].count (arrival quota = wave_count * (waves+1),
 * squared for class-8 mines, minus linked-data). Triggers immediate spawn
 * for the player FG and any FG with no arrival condition. Resets HUD/radar
 * globals, message counters, camera view state. */
int16_t create_createmission(void);

/* Activate one flight group. Sets fgstatus[fg_idx].active=1, dispatches to
 * create_createstaticflightgroup (for static-class species) or
 * create_createflightgroup. craft_slot == -1 spawns the whole FG; otherwise
 * just that craft index. Returns 1. */
int create_startflightgroup(int16_t craft_slot, int16_t fg_idx);

/* Per-frame FG spawn/reinforce driver. Two 236-tick timers
 * (timers[TIMER_FG_ARRIVAL], timers[TIMER_FG_SPAWN]) pace
 * arrival-condition checks and wave reinforcement.
 * Inactive FGs hit SCORE_checkcondition on start_cond[0]/[1] combined by
 * start_op; active FGs respawn their wave when all craft are dead. */
void create_updatefgstatus(void);

/* Spawn the next wave for FG fg_idx (craft_slot=-1). Decrements
 * fgstatus[fg_idx].waves_remaining. Returns byte offset of the updated
 * fgstatus row (fg_idx * 48). */
uint16_t create_reinforceflightgroup(int16_t fg_idx);

/* Spawn a dynamic (non-static) FG. Computes spawn pose from waypoints
 * or from a carrier craft (hangar-spawn). Sets fg{species,skill,side,...}
 * globals, rolls friendly skill bump on difficulty 0, loops from
 * craftcnt=0..count calling create_createcraft. On first-frame-of-mission
 * dispatches MSG_reportfgcreation + FSCRIPT_MsSetSequence based on
 * fgside + fggenus.
 * craft_slot == -1 means spawn all; >= 0 spawns just that craft index. */
int create_createflightgroup(int16_t craft_slot, int16_t fg_idx);

/* Full craft init: allocate a FlightObject slot for fggenus, set pose,
 * initialize all weapon banks (lasers/missiles/beam), shields (doubled
 * for player on easy), warhead loadout with warheadconvert+warheadadjust
 * +fgversion modifiers, per-mesh damage state, AI orders (ordersldr /
 * ordersflw), throttleconvert. Calls PAI_setupcraftaivars + PAI_initplan.
 * Returns the new FlightObject slot index, or 0xFFFF on slot exhaustion. */
uint16_t create_createcraft(void);

/* Spawn a static FG (mine grid / planet / asteroid cloud). Branches on
 * species.ship_class: 8 = mine grid (count*count cube), 9 = single planet,
 * 10 = count asteroids placed randomly in ±256 cube around waypoint 0.
 * Returns fg_idx * 48 (byte offset to fgstatus row). */
int create_createstaticflightgroup(int16_t craft_slot);

/* Create one StaticObject from the staging_static_* globals. Bumps
 * idnumber and fgstatus[fg_idx].cond[0].detail. Returns slot index,
 * or 0xFFFF when the 64-slot table is full. */
int create_createstaticobject(uint16_t fg_idx, uint8_t ship_class, uint8_t species);

/* Resolve a 16-bit object reference (see OBJ_REF_* in tie.h) to
 * worldlocx/y/z. */
void create_getworldposition(uint16_t obj_or_kind, int fg_idx);

/* Build random skybox for a new mission: 6 wall counts (front/back/left/
 * right = 4, top/bottom = 3) and 22 packed direction descriptors;
 * distributes backdropspecies[] across species 117 (planet) and ramps
 * 118..123 + 125-126. */
void create_createbackdrop(void);

/* Detach a random un-damaged flag-bit-2 mesh of obj_idx's craft; spawns
 * debris via create_createcomponent, applies random spin/pitch/heading,
 * marks mesh as MESH_STATE_BLOWN_OFF. stop_after_first == 0 means process all such
 * meshes; otherwise stop on first detach (always 1 in shipped code). */
int16_t create_blowoffcomponent(uint16_t obj_idx, int16_t stop_after_first);

/* Spawn genus-11 mesh-debris FlightObject; clones parent's pose.
 * death_timer = 236 * (rand&7 + 4) ticks. Returns slot or 0xFFFF. */
uint16_t create_createcomponent(uint16_t parent_obj, uint8_t mesh_idx);

/* Spawn a genus-13 flame ember: clones parent, jitters pitch/heading,
 * bumps speed. death_timer = 236 * (rand&3 + 1). Returns slot or 0xFFFF. */
uint16_t create_createember(uint16_t parent_obj);

/* Find first unallocated FlightObject slot in the per-genus range
 * [genus[g], genus_limit[g]). Returns slot or 0xFFFF. Side-effect:
 * zeros the slot's self_idx / damage_state fields. */
uint16_t create_findslot(uint16_t genus);

/* Find first unallocated StaticObject slot (.species == 0). 0xFFFF if full. */
uint16_t create_findstaticslot(void);

/* Per-frame reusable debris cycler: advances through slots 108..115,
 * repopulates slot with ship_idx 110..113 when >0x800 distance from
 * player, positioned random ±256 in player's forward+below+side frame. */
void create_checkdebris(void);

/* Watcom modulo RNG: returns math2_getrandom() % max, or 0 if max == 0. */
uint16_t create_maxrandom(uint16_t max);

/* Compute world drop position for craft_index of fg_idx, anchored on
 * anchor_obj (0xFFFF means anchor at the FG's default waypoint). Writes
 * worldlocx/y/z. Branches on species.ship_class: planet (9, direct
 * waypoint), mine (8, grid stepping), or normal craft (formation-table
 * lookup with version-1 refinement + LOD shift). Returns the applied
 * drop-Z offset. */
int create_getdropposition(uint16_t fg_idx, uint16_t craft_index, uint16_t anchor_obj);

/* --- Module-owned global tables (watdbg: create.c) --- */

/* Team/IFF ID of the local player. Held as a byte even though the record
 * reserves 2 bytes. Defaults to 1 (Imperial) at mission load. */
extern uint8_t playerside;

/* Skill -> AI reaction delay (6 u16 entries; watdbg declares 12 bytes). */
extern uint16_t skilltranslate[6];

/* Skill -> AI update period in ticks (6 u16 entries). */
extern uint16_t aiupdatetranslate[6];

/* EAI.order (EFGStruct .ai[i].order) -> leader / follower order code. */
extern uint8_t ordersldr[33];
extern uint8_t ordersflw[33];

/* Craft-type -> species index (161-space narrowed to 89-space). */
extern uint8_t speciesconvert[89];

/* Species -> engine genus (ship family class). */
extern uint8_t genusconvert[9];
extern uint8_t familyconvert[4];

/* Warhead slot id -> warhead ship id; per-type base ammo scale. */
extern uint8_t warheadconvert[8];
extern uint16_t warheadadjust[8];

/* MeshType -> initial damage-state byte (per-mesh preload in createcraft). */
extern uint8_t initialdamagestate[32];

/* Per-special-ship table of destroyable component indices (12/ship). */
extern uint8_t componentsgone[60];

/* Arrival-difficulty filter bits, indexed by (fg.difficulty) / by
 * mission.difficulty. AND of fgdiffmask[fg.difficulty] &
 * diffmask[mission.difficulty] > 0 means FG will arrive. */
extern uint8_t fgdiffmask[6];
extern uint8_t diffmask[4];

/* --- Flight-group staging (globals written by create_createflightgroup
 *     before spawning each craft, read by create_createcraft). --- */

extern int32_t fglocx, fglocy, fglocz;
extern int16_t staging_static_x, staging_static_y, staging_static_z;
extern int8_t staging_static_pitch, staging_static_yaw, staging_static_roll;
extern uint16_t craftcnt;
extern uint16_t fgcnt;
extern int16_t fgheadingxy;
extern int16_t fgheadingz;
extern uint8_t fgversion;
extern uint8_t fghangar;
extern uint8_t fghyperspace;
extern uint8_t fggenus;
extern uint8_t leaderflag;
extern uint8_t fgspecies;
extern uint8_t fgwarhead;
extern uint8_t fgseparation;
extern uint8_t fgformation;
extern uint8_t fgflightflag;
extern uint8_t fgskill;
extern uint8_t fgside;
extern uint16_t fgsidecreated;

#endif
