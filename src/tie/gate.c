#include "tie/gate.h"
#include "tie/draw.h"
#include "tie/drawpol.h"
#include "tie/edition.h"
#include "tie/festring.h"
#include "tie/flight_surface_tie98.h"
#include "tie/frontend_display_tie98.h"
#include "tie/fsfx.h"
#include "tie/fview.h"
#include "tie/mission.h"
#include "tie/modelbounds.h"
#include "tie/modelmesh.h"
#include "tie/msg.h"
#include "tie/msg_templates.h"
#include "tie/panel.h"
#include "tie/render_scene_tie98.h"
#include "tie/tie.h"
#include "tie/xtimer.h"
#include "tie_runtime/display/classic_display.h"
#include "tie_runtime/runtime/bonus_countdown_task.h"
#include "tie_runtime/runtime/profile.h"
#include "tie_runtime/runtime/wide_arithmetic.h"
#ifdef TIE_MODERN
#include <landru/task.h>
#endif

#include <stddef.h>
#include <stdint.h>

/* outchar is declared in tie.h as a function-pointer global. */

/* Player-pose history rings. 4-slot ring; slot [3] is the snapshot pose
 * used by collide_collisions on a briefing/training/combat collision. */
// GLOBAL: TIE95 0xD4AFC
// GLOBAL: TIE98 0x6258A8
int16_t gatepreviousroll[4];
// GLOBAL: TIE95 0xD4B24
// GLOBAL: TIE98 0x6258F0
int32_t gatepreviousx[4];
// GLOBAL: TIE95 0xD4B14
// GLOBAL: TIE98 0x625900
int32_t gatepreviousy[4];
// GLOBAL: TIE95 0xD4B04
// GLOBAL: TIE98 0x6258E0
int32_t gatepreviousz[4];
// GLOBAL: TIE95 0xD4B34
// GLOBAL: TIE98 0x6258C8
int16_t gatepreviousheading[4];
// GLOBAL: TIE95 0xD4B3C
// GLOBAL: TIE98 0x6258C0
int16_t gatepreviouspitch[4];

/* -------------------------------------------------------------------------
 * Module globals.  Layout taken from watdbg (docs/watdbg-prototypes.txt):
 *   _gatespeed[40]    size=40  (20 words)
 *   _powersof10[36]   size=36  (9 dwords)
 *   _gatetimer[6]     size=6   (3 words)
 *   _gateguntimer[2]  size=2
 *   _currentgate[4]   size=4 from watdbg / 2 in IDA -- scalar u16 + alignment.
 * ---------------------------------------------------------------------- */

/* Rotation period per training level (20 entries; only levels 0..19 are
 * reachable in practice). Values taken directly from the shipped .EXE data
 * segment. Lower = faster mesh rotation. */
// GLOBAL: TIE95 0xC5360
// GLOBAL: TIE98 0x4E3950
uint16_t gatespeed[20] = {
	24, 24, 24, 24, 20, 16, 14, 14, 14, 12, 12, 12, 10, 8, 6, 6, 6, 6, 6, 6,
};

/* Powers-of-ten divisors for gate_outdnum. The duplicated '1' at index 0
 * is intentional -- gate_outdnum's loop runs pos = num_digits..1 and reads
 * powersof10[pos], so pos==1 maps to divisor 1 (the ones place). Pulled
 * from the binary's data segment at D4C54. */
// GLOBAL: TIE95 0xC5388
uint32_t powersof10[9] = {
	1, 1, 10, 100, 1000, 10000, 100000, 1000000, 10000000,
};

/* String pointers to the CRT labels. Populated by fediskio_loadstringdata
 * from strings.dat; this module only reads them. */
// GLOBAL: TIE95 0xD4B50
// GLOBAL: TIE98 0x6258B8
void* gatelevelstr;
// GLOBAL: TIE95 0xD4B48
// GLOBAL: TIE98 0x6258D8
void* gateremainstr;
// GLOBAL: TIE95 0xD4B54
// GLOBAL: TIE98 0x6258A0
void* gatepassedstr;
// GLOBAL: TIE95 0xD4B4C
// GLOBAL: TIE98 0x6258B0
void* targetshitstr;
// GLOBAL: TIE95 0xD4B44
// GLOBAL: TIE98 0x6258B4
void* scorestr;

/* Animation timers: [0] = cargopod, [1] = wing, [2] = antenna. */
// GLOBAL: TIE95 0xD4B58
// GLOBAL: TIE98 0x6258D0
int16_t gatetimer[3];

/* Unused training-gun timer. */
// GLOBAL: TIE95 0xD4B5E
// GLOBAL: TIE98 0x6258A4
int16_t gateguntimer;

/* Next gate the player must cross (1..12). Reset to 1 by
 * gate_settraininglevel; advanced by gate_updategateanimations. */
// GLOBAL: TIE95 0xD4B60
uint16_t currentgate;

// GLOBAL: TIE98 0x6258DC
uint16_t gate_render_reference_object;

// FUNCTION: TIE98 0x426310
// GATE_setrenderreferenceobject (inferred)
void gate_setrenderreferenceobject(uint16_t object_index) { gate_render_reference_object = object_index; }

/* -------------------------------------------------------------------------
 * gate_createtraininggates  (0x27e60)
 *
 * Build the 12-gate training course: zero each gate's FlightObject /
 * CraftData, assign ship_idx / pitch / heading / roll from a hard-coded
 * per-gate init table, call FVIEW_calcrotatemove / calcrotateorient to
 * populate the craftS/f/U basis vectors, and place the gate at a running
 * cumulative world position. Called once at mission start from
 * tie_simulator.
 * ---------------------------------------------------------------------- */
#ifdef __WATCOMC__
#pragma pack(2)
#else
#pragma pack(push, 2)
#endif
typedef struct {
	uint16_t ship_idx[14]; /* slots 0 and 13 unused */
	uint16_t pitch[14];
	uint16_t heading[14];
	uint16_t roll[14];
} GATE_InitTable;
#ifdef __WATCOMC__
#pragma pack()
#else
#pragma pack(pop)
#endif
/* -------------------------------------------------------------------------
 * gate_savegatelastpos  (0x27c90)
 *
 * Shift the 4-slot player-pose history rings by one frame and store the
 * current player pose into slot 0. Called per-tick from move_moveobjects
 * while a training mission is active; gate_checkgateedge reads this history
 * to perform swept-volume plane-crossing detection.
 * ---------------------------------------------------------------------- */
// FUNCTION: TIE95 0x28FD0
void gate_savegatelastpos(void) {
	/* Shift rings: [3]=[2], [2]=[1], [1]=[0] (i = 2, 1, 0). */
	uint16_t i = 3;

	while (i--) {
		gatepreviousx[i + 1] = gatepreviousx[i];
		gatepreviousy[i + 1] = gatepreviousy[i];
		gatepreviousz[i + 1] = gatepreviousz[i];
		gatepreviousroll[i + 1] = gatepreviousroll[i];
		gatepreviouspitch[i + 1] = gatepreviouspitch[i];
		gatepreviousheading[i + 1] = gatepreviousheading[i];
	}

	gatepreviousx[0] = pstate.player->world_x_prev;
	gatepreviousy[0] = pstate.player->world_y_prev;
	gatepreviousz[0] = pstate.player->world_z_prev;
	gatepreviousroll[0] = pstate.player->roll;
	gatepreviouspitch[0] = pstate.player->pitch;
	gatepreviousheading[0] = pstate.player->heading;
}

/* -------------------------------------------------------------------------
 * gate_drawtraininggate  (0x27d48)
 *
 * Render a single gate. Current gate and the one after it get the full
 * DRAW_drawcomplexobject; past gates render only the MESH_MainHull. Called
 * once per visible gate by tie_updatescreen.
 * ---------------------------------------------------------------------- */
// FUNCTION: TIE95 0x29088
void gate_drawtraininggate(uint16_t obj_idx) {
	ShipModelMesh* mesh;
	uint16_t i;
	int32_t eye_x, eye_y, eye_z;
	const uint16_t* poly_detail;

	int16_t saved_target;

	if (obj_idx == currentgate || obj_idx == currentgate + 1) {
		/* Binary has a `if (obj_idx < currentgate) bluetarget = obj_idx;`
		 * here; given the outer condition that branch is unreachable
		 * (CSE artifact). Preserved as a dead conditional below. */
		if (obj_idx < currentgate)
			bluetarget = obj_idx;
		draw_drawcomplexobject(obj_idx);
	} else {
		draw_Lockshipfileptrs(objects[obj_idx].ship_idx);
		/* Tag the parent-object with 0x7000 so the draw/pick pipeline
		 * recognises it as a training gate rather than a regular ship. */
		parentobject = (uint16_t)(obj_idx + 0x7000);
	}

	/* Find the MESH_MainHull (value 1) within the current ship's mesh list. */
	mesh = componentblockptr;
	for (i = 0; i < objectblockptr->num_meshes - 1; i++, mesh++) {
		if (mesh->mesh_type == 1)
			break;
	}
	solidindex = (int16_t)i;

	/* Save and optionally override the current-target highlight. */
	saved_target = (int16_t)currenttarget;
	if (obj_idx < currentgate) {
		currenttarget = parentobject;
		highlightcolor = 1;
	}

	eye_z = objecteyez;
	eye_y = objecteyey;
	eye_x = objecteyex;
	poly_detail = draw_getcompdetailptr(mesh, objecteyez);
	drawpol_drawpolyobject(poly_detail, eye_x, eye_y, eye_z);

	currenttarget = (uint16_t)saved_target;
}

// FUNCTION: TIE95 0x291A0
void gate_createtraininggates(void) {
	GATE_InitTable init;
	int32_t cum_x, cum_y, cum_z;
	uint16_t gate_idx;

	mission.mission_score = 0;
	cum_x = 0;
	cum_y = 0;
	cum_z = 0;
	mission.train_targets = 0;

	/* Per-gate init table: ship_idx alternates base post (98 = 'b') and
	 * crossbar (99 = 'c'); angles are binary (0x4000 = 90 deg). */
	init.ship_idx[1] = 98;
	init.pitch[1] = 0x4000;
	init.heading[1] = 0;
	init.roll[1] = 0;
	init.ship_idx[2] = 99;
	init.pitch[2] = 0x4000;
	init.heading[2] = 0;
	init.roll[2] = 0;
	init.ship_idx[3] = 98;
	init.pitch[3] = 0;
	init.heading[3] = 0;
	init.roll[3] = 0;
	init.ship_idx[4] = 99;
	init.pitch[4] = 0;
	init.heading[4] = 0x4000;
	init.roll[4] = 0;
	init.ship_idx[5] = 98;
	init.pitch[5] = 0x4000;
	init.heading[5] = 0xC000;
	init.roll[5] = 0;
	init.ship_idx[6] = 99;
	init.pitch[6] = 0x4000;
	init.heading[6] = 0xC000;
	init.roll[6] = 0x8000;
	init.ship_idx[7] = 98;
	init.pitch[7] = 0x8000;
	init.heading[7] = 0;
	init.roll[7] = 0;
	init.ship_idx[8] = 99;
	init.pitch[8] = 0x8000;
	init.heading[8] = 0x8000;
	init.roll[8] = 0;
	init.ship_idx[9] = 98;
	init.pitch[9] = 0x4000;
	init.heading[9] = 0x8000;
	init.roll[9] = 0;
	init.ship_idx[10] = 99;
	init.pitch[10] = 0x4000;
	init.heading[10] = 0x8000;
	init.roll[10] = 0x4000;
	init.ship_idx[11] = 98;
	init.pitch[11] = 0x4000;
	init.heading[11] = 0x4000;
	init.roll[11] = 0;
	init.ship_idx[12] = 99;
	init.pitch[12] = 0x4000;
	init.heading[12] = 0x4000;
	init.roll[12] = 0x4000;

	for (gate_idx = 1; gate_idx < 13; gate_idx++) {
		uint16_t ship_idx = init.ship_idx[gate_idx];
		int mesh_count;
		uint16_t m;
		int16_t fwd_step;
		int16_t up_step;
		int16_t side_step;
		int16_t fwd_advance;
		int16_t up_advance;
		int32_t dx, dy, dz;

		objects[gate_idx].ship_idx = (uint8_t)ship_idx;
		objects[gate_idx].spin_rate = 0;
		objects[gate_idx].current_speed = 0;
		objects[gate_idx].speed_remainder = 0;
		objects[gate_idx].idnumber = 1;
		objects[gate_idx].collision_radius = 0x7FFF;
		objects[gate_idx].death_timer = 0;
		objects[gate_idx].age_ticks = 0;
		objects[gate_idx].self_idx = 0;
		objects[gate_idx].category = 6;
		objects[gate_idx].genus = 14; /* GENUS_GATE */
		objects[gate_idx].ship_type_override = 0;
		objects[gate_idx].side = 0;
		objects[gate_idx].orient_dirty = 1;
		objects[gate_idx].move_dirty = 1;
		objects[gate_idx].fg_idx = 1;
		objects[gate_idx].craft_ptr = &crafts[gate_idx];
		craftptr = objects[gate_idx].craft_ptr;

		/* Retail writes fg_array[1].version = 5 every iteration of the
		 * gate loop. The version byte is consumed by other readers that
		 * gate behaviour on FG state. */
		fg_array[1].version = 5;

		for (m = 0; m < 2; m++) {
			if (m == 0)
				objects[gate_idx].anim_frame = 0;
			else
				objects[gate_idx].anim_frame_alt = 0;
		}

		/* --- CraftData per-mesh init (mesh_state / mesh_rotation / mesh_component_hp) --- */
		mesh_count = 40;
		if (TIE_FLIGHT_TIE98) {
			modelmesh_require_craft_capacity(ship_idx);
			mesh_count = modelmesh_getcount(ship_idx) + 1;
		}
		for (m = 0; m < mesh_count; m++) {
			craftptr->mesh_state[m] = MESH_STATE_VISIBLE;
			craftptr->mesh_rotation[m] = 0;
			craftptr->mesh_component_hp[m] = 0xFF; /* -1 as uint8_t */
		}

		craftptr->hull_max = 0x7FFF;
		craftptr->hull_strength = 0x7FFF;
		craftptr->hull_damage = 0;
		craftptr->dead_0B0 = 0;
		craftptr->pad_0B4 = 0;
		craftptr->was_hit_flag = 0;
		craftptr->pad_0B6 = 0;
		craftptr->dock_state_flags = 0;
		craftptr->ai_anim_flags = 0;
		craftptr->beam_state = 0;
		craftptr->subsystem_active = 1; /* SF_SHIELDS */
		craftptr->is_player_craft = 0;
		craftptr->forward_shield = 0x7FFF;
		craftptr->rear_shield = 0;
		craftptr->status_flags = craftptr->subsystem_active;

		if (!TIE_FLIGHT_TIE98)
			draw_Lockshipfileptrs(ship_idx);

		/* Apply orientation. */
		objects[gate_idx].roll = init.roll[gate_idx];
		objects[gate_idx].heading = init.heading[gate_idx];
		objects[gate_idx].pitch = init.pitch[gate_idx];
		fview_calcrotatemove(objects[gate_idx].pitch, objects[gate_idx].heading, &objects[gate_idx]);
		fview_calcrotateorient(objects[gate_idx].roll, 0, &objects[gate_idx]);

		if (TIE_FLIGHT_TIE98) {
			/* TIE98 uses unscaled 32-bit OPT bounds. */
			int32_t fwd = -modelbounds_getmaxy(ship_idx);
			int32_t up = 0;
			int32_t adv_fwd = 0;
			int32_t adv_up = 0;

			if (ship_idx == 98) {
				adv_fwd = modelbounds_getsizey(ship_idx);
			} else if (ship_idx == 99) {
				const int32_t base_min_z = modelbounds_getminz(98);
				up = modelbounds_getminz(99) - base_min_z;
				adv_fwd = base_min_z + modelbounds_getsizey(99);
				adv_up = base_min_z + modelbounds_getsizez(99);
			}
			objects[gate_idx].world_x = cum_x - (math2_mul_q15(craftf1, fwd) + math2_mul_q15(craftU1, up));
			objects[gate_idx].world_y = cum_y - (math2_mul_q15(craftf2, fwd) + math2_mul_q15(craftU2, up));
			objects[gate_idx].world_z = cum_z - (math2_mul_q15(craftf3, fwd) + math2_mul_q15(craftU3, up));
			objects[gate_idx].world_x_prev = objects[gate_idx].world_x;
			objects[gate_idx].world_y_prev = objects[gate_idx].world_y;
			objects[gate_idx].world_z_prev = objects[gate_idx].world_z;
			cum_x += math2_mul_q15(craftf1, adv_fwd) + math2_mul_q15(craftU1, adv_up);
			cum_y += math2_mul_q15(craftf2, adv_fwd) + math2_mul_q15(craftU2, adv_up);
			cum_z += math2_mul_q15(craftf3, adv_fwd) + math2_mul_q15(craftU3, adv_up);
			continue;
		}

		/* TIE95 derives 16-bit offsets from its object block and doubles
		 * the resulting geometry. Every gate is either a base post (98) or
		 * a crossbar (99). */
		fwd_step = (int16_t)objectblockptr->speed_default;
		up_step = 0;
		fwd_step = -fwd_step;
		if (ship_idx == 98) {
			side_step = 0;
			up_advance = 0;
			fwd_advance = objectblockptr->height;
		} else if (ship_idx == 99) {
			int16_t shield_hi = (int16_t)(objectblockptr->shield_default >> 16);
			fwd_advance = objectblockptr->height + shield_hi;
			up_advance = objectblockptr->depth + shield_hi;
			side_step = 0;
		}

		dx = (craftS1 * up_step) >> 15;
		dy = (craftS2 * up_step) >> 15;
		dz = (craftS3 * up_step) >> 15;
		dx += (craftf1 * fwd_step) >> 15;
		dy += (craftf2 * fwd_step) >> 15;
		dz += (craftf3 * fwd_step) >> 15;
		dx += (craftU1 * up_step) >> 15;
		dy += (craftU2 * up_step) >> 15;
		dz += (craftU3 * up_step) >> 15;
		objects[gate_idx].world_x = cum_x - dx * 2;
		objects[gate_idx].world_y = cum_y - dy * 2;
		objects[gate_idx].world_z = cum_z - dz * 2;

		dx = (craftS1 * side_step) >> 15;
		dy = (craftS2 * side_step) >> 15;
		dz = (craftS3 * side_step) >> 15;
		dx += (craftf1 * fwd_advance) >> 15;
		dy += (craftf2 * fwd_advance) >> 15;
		dz += (craftf3 * fwd_advance) >> 15;
		dx += (craftU1 * up_advance) >> 15;
		dy += (craftU2 * up_advance) >> 15;
		dz += (craftU3 * up_advance) >> 15;
		cum_x += dx * 2;
		cum_y += dy * 2;
		cum_z += dz * 2;

		objects[gate_idx].world_x_prev = objects[gate_idx].world_x;
		objects[gate_idx].world_y_prev = objects[gate_idx].world_y;
		objects[gate_idx].world_z_prev = objects[gate_idx].world_z;
	}
}

// FUNCTION: TIE98 0x425590
void gate_drawtraininggate_tie98(uint16_t object_index) {
	FlightObject* object = &objects[object_index];
	uint16_t main_hull_mesh_index;
	int mesh_count;
	uint16_t saved_current_target;

	if (object_index == gate_render_reference_object ||
		object_index == (uint16_t)(gate_render_reference_object + 1)) {
		if (object_index < currentgate)
			bluetarget = object_index;
		draw_drawcomplexobject_tie98(object_index);
		FlightModel_Draw_Object(object);
	} else {
		parentobject = (uint16_t)(object_index + 0x7000);
	}

	main_hull_mesh_index = 0;
	mesh_count = modelmesh_getcount(object->ship_idx);
	while (main_hull_mesh_index < mesh_count - 1 &&
		   modelmesh_gettype(object->ship_idx, main_hull_mesh_index) != TIE_MESH_MAIN_HULL)
		++main_hull_mesh_index;

	saved_current_target = currenttarget;
	solidindex = (int16_t)main_hull_mesh_index;
	if (object_index < currentgate) {
		highlightcolor = 1;
		currenttarget = parentobject;
	}
	FlightModel_Draw_Object_Mesh(object, main_hull_mesh_index);
	currenttarget = saved_current_target;
}

/* -------------------------------------------------------------------------
 * gate_settraininglevel  (0x28498)
 *
 * Apply difficulty `level` to the already-built course: reset the
 * mission's gate counters and time limit, then walk every gate's meshes
 * and set up mesh_state / mesh_rotation / mesh_component_hp per type.
 * mesh_component_hp here is overloaded: positive = rotation-speed-per-tick,
 * 0 = static / visible, 0xFF = free-spin (reset).
 * ---------------------------------------------------------------------- */
// FUNCTION: TIE95 0x297D8
void gate_settraininglevel(uint16_t level) {
	uint8_t speed_gun;
	uint8_t speed_pod;
	uint8_t speed_wing;
	uint16_t obj_idx;

	currentgate = 1;
	mission.train_gates_remaining = 12;
	mission.train_gates_passed = 0;

	if (level > 8) {
		timeleft.minute = 0;
		timeleft.second = (uint8_t)(60 - 5 * (uint8_t)(level - 8));
	} else {
		timeleft.minute = (uint8_t)((10 - level) / 2);
		timeleft.second = (uint8_t)(30 * (level & 1));
	}

	speed_wing = (uint8_t)(24 * level);
	speed_gun = (uint8_t)(2 * level);
	speed_pod = (uint8_t)(3 * level);

	for (obj_idx = 0; obj_idx < NUM_CRAFTS; ++obj_idx) {
		uint16_t ship_idx = objects[obj_idx].ship_idx;
		uint16_t mesh_idx;
		ShipModelMesh* mesh;

		if (ship_idx == 0 || objects[obj_idx].genus != 14 /* GENUS_GATE */)
			continue;

		craftptr = objects[obj_idx].craft_ptr;
		if (TIE_FLIGHT_TIE98)
			modelmesh_require_craft_capacity(ship_idx);
		else
			draw_Lockshipfileptrs(ship_idx);

		mesh = componentblockptr;
		for (mesh_idx = 0; mesh_idx < TIE_FLIGHT_EDITION(objectblockptr->num_meshes,
														 (uint16_t)modelmesh_getcount(ship_idx));
			 ++mesh_idx, ++mesh) {
			if (obj_idx == 1) {
				/* Gate 1 (course start) is always frozen. */
				if (TIE_FLIGHT_EDITION(mesh->mesh_type, (uint16_t)modelmesh_gettype(ship_idx, mesh_idx)) ==
					1 /* MESH_MainHull */)
					craftptr->mesh_component_hp[mesh_idx] = 0xFF;
				else
					craftptr->mesh_component_hp[mesh_idx] = 0;
				craftptr->mesh_state[mesh_idx] = MESH_STATE_HIDDEN;
				continue;
			}

			switch (TIE_FLIGHT_EDITION(mesh->mesh_type, (uint16_t)modelmesh_gettype(ship_idx, mesh_idx))) {
				case 1: /* MESH_MainHull */
					craftptr->mesh_state[mesh_idx] = MESH_STATE_HIDDEN;
					break;
				case 5: /* MESH_SmallGun */
					/* No mesh_rotation reset for guns. */
					craftptr->mesh_state[mesh_idx] = MESH_STATE_VISIBLE;
					craftptr->mesh_component_hp[mesh_idx] = speed_gun;
					break;
				case TIE_MESH_CARGO_POD:
					if (TIE_FLIGHT_TIE98) {
						modelmesh_enableexplosiontype2(ship_idx, mesh_idx);
						modelmesh_enableexplosiontype1(ship_idx, mesh_idx);
					} else {
						mesh->flags |= 3;
					}
					if (level < 2) {
						craftptr->mesh_state[mesh_idx] = MESH_STATE_HIDDEN;
						craftptr->mesh_component_hp[mesh_idx] = 0;
					} else {
						craftptr->mesh_state[mesh_idx] = MESH_STATE_VISIBLE;
						craftptr->mesh_rotation[mesh_idx] = 0;
						craftptr->mesh_component_hp[mesh_idx] = speed_pod;
					}
					break;
				case 18: /* MESH_MiscHull */
					if (level < 5) {
						craftptr->mesh_state[mesh_idx] = MESH_STATE_HIDDEN;
						craftptr->mesh_component_hp[mesh_idx] = 0;
					} else {
						craftptr->mesh_state[mesh_idx] = MESH_STATE_VISIBLE;
						craftptr->mesh_rotation[mesh_idx] = TIE_FLIGHT_EDITION(0, 1);
						craftptr->mesh_component_hp[mesh_idx] = 0xFF;
					}
					break;
				case 19: /* MESH_Antenna */
					if (level < 7) {
						craftptr->mesh_state[mesh_idx] = MESH_STATE_HIDDEN;
						craftptr->mesh_component_hp[mesh_idx] = 0;
					} else {
						craftptr->mesh_state[mesh_idx] = MESH_STATE_VISIBLE;
						craftptr->mesh_rotation[mesh_idx] = 0;
						craftptr->mesh_component_hp[mesh_idx] = 0xFF;
					}
					break;
				case TIE_MESH_WING:
					if (TIE_FLIGHT_TIE98) {
						modelmesh_enableexplosiontype2(ship_idx, mesh_idx);
						modelmesh_enableexplosiontype1(ship_idx, mesh_idx);
					} else {
						mesh->flags |= 3;
					}
					if (level < 3) {
						craftptr->mesh_state[mesh_idx] = MESH_STATE_HIDDEN;
						craftptr->mesh_component_hp[mesh_idx] = 0;
					} else {
						craftptr->mesh_state[mesh_idx] = MESH_STATE_VISIBLE;
						craftptr->mesh_rotation[mesh_idx] = 0;
						craftptr->mesh_component_hp[mesh_idx] = speed_wing;
					}
					break;
			}
		}
	}

	if (!replayviewmode)
		panel_initpanel();
}

/* -------------------------------------------------------------------------
 * gate_updategateanimations  (0x28774)
 *
 * Per-frame animation tick. Three-phase:
 *   (1) Advance the cargopod/wing/antenna timers by frameticks; when one
 *       overflows, compute the number of periods elapsed and capture it
 *       as the per-mesh rotation delta for this frame.
 *   (2) Walk every gate's meshes and add the appropriate delta to
 *       mesh_rotation for the mesh type (gated by train_level).
 *   (3) Check whether the player crossed the next gate's plane; on a
 *       crossing advance currentgate and, if we just passed gate 12, run
 *       the level-complete reward count-down.
 * ---------------------------------------------------------------------- */
// FUNCTION: TIE95 0x29AB4
// FUNCTION: TIE98 0x426020
void gate_updategateanimations(void) {
#ifdef TIE_MODERN
	uint16_t tickbudget = 0;
	BonusCountdownTask* continuation = NULL;
	if (bonus_countdown_active) {
		continuation = landru_task_top();
		tickbudget = continuation->tickbudget;
		continuation->waiting = false;
	} else
#endif
	{
		int16_t delta[3];
		uint16_t i;
		uint16_t j;
		uint16_t next_gate;

		/* Phase 1: timers. */
		for (i = 0; i < 3; ++i) {
			gatetimer[i] -= frameticks;
			if (gatetimer[i] < 0) {
				uint16_t speed = gatespeed[mission.train_level];
				if (i == 2) {
#ifdef TIE_MODERN
					/* The original reads gatespeed[-1] at train_level 0. */
					if (mission.train_level != 0)
#endif
						speed = gatespeed[mission.train_level - 1];
				}
				delta[i] = -gatetimer[i] / speed + 1;
				gatetimer[i] += speed * delta[i];
			} else {
				delta[i] = 0;
			}
		}

		/* Phase 2: apply deltas to each gate's meshes. */
		for (j = 1; j < 13; ++j) {
			uint16_t num_meshes;
			uint16_t mesh_idx;
#if !defined(TIE_MODERN) && !defined(TIE98)
			ShipModelMesh* mesh;
#endif

#ifdef TIE_MODERN
			if (!TIE_FLIGHT_TIE98)
				draw_Lockshipfileptrs(objects[j].ship_idx);
			num_meshes = TIE_FLIGHT_EDITION((uint16_t)objectblockptr->num_meshes,
											(uint16_t)modelmesh_getobjecttypemeshcount(objects[j].ship_idx));
#elif defined(TIE98)
			num_meshes = (uint16_t)modelmesh_getobjecttypemeshcount(objects[j].ship_idx);
#else
			draw_Lockshipfileptrs(objects[j].ship_idx);
			mesh = componentblockptr;
			num_meshes = objectblockptr->num_meshes;
#endif
			craftptr = objects[j].craft_ptr;

			for (mesh_idx = 0; mesh_idx < num_meshes; ++mesh_idx) {
#ifdef TIE_MODERN
				switch (TIE_FLIGHT_EDITION(componentblockptr[mesh_idx].mesh_type,
										   modelmesh_getobjecttypemeshtype(objects[j].ship_idx, mesh_idx))) {
#elif defined(TIE98)
				switch (modelmesh_getobjecttypemeshtype(objects[j].ship_idx, mesh_idx)) {
#else
				switch (mesh->mesh_type) {
#endif
					case 2: /* MESH_Wing */
						if (mission.train_level >= 4)
							craftptr->mesh_rotation[mesh_idx] += delta[1];
						break;
					case 18: /* MESH_MiscHull */
					case 19: /* MESH_Antenna */
						if (mission.train_level >= 6)
							craftptr->mesh_rotation[mesh_idx] += delta[2];
						break;
					case 17: /* MESH_CargoPod */
						if (mission.train_level >= 3)
							craftptr->mesh_rotation[mesh_idx] += delta[0];
						break;
				}
#if !defined(TIE_MODERN) && !defined(TIE98)
				++mesh;
#endif
			}
		}

		/* Phase 3: check the next gate. */
#ifdef TIE_MODERN
		gate_updatecourseprogress();
		return;
#else
		if (currentgate == 12)
			next_gate = 1;
		else
			next_gate = currentgate + 1;
		if (!gate_checkgateedge(next_gate))
			return;

		currentgate = next_gate;
		++mission.train_gates_passed;
		--mission.train_gates_remaining;

		if (next_gate != 1)
			return;

		/* Convert the remaining time into the level-completion bonus. */
		if (TIE_DISPLAY_DX5)
			g_flightDrawToOffscreenSurface = 0;
		msg_messageprintf(MSG_LEVEL_COMPLETED);
		mission.train_bonus = 0;
		if (TIE_DISPLAY_DX5) {
			FlightSurface_Lock();
			gate_updatebonuspoints();
			FlightSurface_Unlock();
			g_flightDrawToOffscreenSurface = 1;
			FrontendDisplay_PresentFrontSurface();
			FrontendDisplay_PresentFrame();
		} else {
			gate_updatebonuspoints();
		}
#endif
	}
#ifdef TIE_MODERN
	while (timeleft.minute || timeleft.second) {
		tickbudget = (uint16_t)(tickbudget + (uint16_t)xtimer_Time_Elapsed());
		if (tickbudget < 4) {
			continuation->tickbudget = tickbudget;
			continuation->waiting = true;
			return;
		}
		tickbudget = 0;
		if (timeleft.second) {
			--timeleft.second;
		} else {
			timeleft.second = 59;
			--timeleft.minute;
		}
		mission.mission_score += 10;
		mission.train_bonus += 10;
		if ((mission.mission_score % 100) == 0)
			fsfx_triggersfx(0x21, 0xFFFF);
		if (TIE_DISPLAY_DX5) {
			g_flightDrawToOffscreenSurface = 0;
			FlightSurface_Lock();
			gate_updatebonuspoints();
			FlightSurface_Unlock();
			g_flightDrawToOffscreenSurface = 1;
			FrontendDisplay_PresentFrame();
		} else {
			gate_updatebonuspoints();
		}
		continuation->tickbudget = tickbudget;
		return;
	}
#else
	while (timeleft.minute || timeleft.second) {
		if (timeleft.second) {
			--timeleft.second;
		} else {
			timeleft.second = 59;
			--timeleft.minute;
		}
		mission.mission_score += 10;
		mission.train_bonus += 10;
		if ((mission.mission_score % 100) == 0)
			fsfx_triggersfx(0x21, 0xFFFF);
		if (!TIE_DISPLAY_DX5)
			gate_updatebonuspoints();
		do {
			tickcounter += xtimer_Time_Elapsed();
		} while (tickcounter < 4);
		tickcounter = 0;
		if (TIE_DISPLAY_DX5) {
			g_flightDrawToOffscreenSurface = 0;
			FlightSurface_Lock();
			gate_updatebonuspoints();
			FlightSurface_Unlock();
			g_flightDrawToOffscreenSurface = 1;
			FrontendDisplay_PresentFrame();
		}
	}
#endif
	argtable[0] = (uint16_t)mission.train_bonus;
	msg_messageprintf(MSG_BONUS_AWARDED);
	++mission.train_level;
	if (TIE_DISPLAY_DX5)
		FlightSurface_Lock();
	gate_settraininglevel(mission.train_level);
	if (TIE_DISPLAY_DX5)
		FlightSurface_Unlock();
#ifdef TIE_MODERN
	bonus_countdown_active = 0;
#endif
}

/* -------------------------------------------------------------------------
 * gate_checkgateedge  (0x28a54)
 *
 * Swept-volume test: did the player cross the plane of gate `obj_idx`
 * between the previous tick and this one?
 * ---------------------------------------------------------------------- */
// FUNCTION: TIE95 0x29D94
int gate_checkgateedge(uint16_t obj_idx) {
	uint16_t ship_idx = objects[obj_idx].ship_idx;
	int16_t base_offset;
	int32_t fwd_offset;
	FlightObject* obj;
	int32_t plane_x;
	int32_t plane_y;
	int32_t plane_z;
	int32_t dx_cur;
	int32_t dy_cur;
	int32_t dz_cur;
	int32_t dx_prev;
	int32_t dy_prev;
	int32_t dz_prev;
	int16_t cur_signed;
	int16_t prev_signed;

	if (!TIE_FLIGHT_TIE98)
		draw_Lockshipfileptrs(ship_idx);

	if (ship_idx == 98)
		base_offset = TIE_FLIGHT_EDITION(-objectblockptr->speed_default, -modelbounds_getmaxy(ship_idx));
	else
		base_offset = 0;

	if (TIE_FLIGHT_TIE98)
		fwd_offset = (obj_idx == currentgate) ? base_offset - 1024 : base_offset + 32;
	else
		fwd_offset = (obj_idx == currentgate) ? (int16_t)(base_offset - 1024) : (int16_t)(base_offset + 32);

	obj = &objects[obj_idx];
	if (TIE_FLIGHT_TIE98) {
		plane_x = obj->world_x + math2_mul_q15(obj->fwd_x, fwd_offset);
		plane_y = obj->world_y + math2_mul_q15(obj->fwd_y, fwd_offset);
		plane_z = obj->world_z + math2_mul_q15(obj->fwd_z, fwd_offset);
	} else {
		plane_x = (fwd_offset * obj->fwd_x) >> 15;
		plane_y = (fwd_offset * obj->fwd_y) >> 15;
		plane_z = (fwd_offset * obj->fwd_z) >> 15;
		plane_x = obj->world_x + plane_x * 2;
		plane_y = obj->world_y + plane_y * 2;
		plane_z = obj->world_z + plane_z * 2;
	}

	dx_cur = pstate.player->world_x - plane_x;
	dy_cur = pstate.player->world_y - plane_y;
	dz_cur = pstate.player->world_z - plane_z;

	/* Fast reject on the current-tick position. */
	if (dx_cur > 0x4000 || dx_cur < -0x4000 || dy_cur > 0x4000 || dy_cur < -0x4000 || dz_cur > 0x4000 ||
		dz_cur < -0x4000)
		return 0;

	dx_prev = pstate.player->world_x_prev - plane_x;
	dy_prev = pstate.player->world_y_prev - plane_y;
	dz_prev = pstate.player->world_z_prev - plane_z;

	if (dx_prev > 0x4000 || dx_prev < -0x4000 || dy_prev > 0x4000 || dy_prev < -0x4000 || dz_prev > 0x4000 ||
		dz_prev < -0x4000)
		return 0;

	if (TIE_FLIGHT_TIE98) {
		cur_signed = math2_mul_q15(obj->fwd_x, dx_cur) + math2_mul_q15(obj->fwd_y, dy_cur) +
					 math2_mul_q15(obj->fwd_z, dz_cur);
		prev_signed = math2_mul_q15(obj->fwd_x, dx_prev) + math2_mul_q15(obj->fwd_y, dy_prev) +
					  math2_mul_q15(obj->fwd_z, dz_prev);
	} else {
		cur_signed = math2_dot3_q15_clamped((int16_t)dx_cur, (int16_t)dy_cur, (int16_t)dz_cur, obj->fwd_x,
											obj->fwd_y, obj->fwd_z);
		prev_signed = math2_dot3_q15_clamped((int16_t)dx_prev, (int16_t)dy_prev, (int16_t)dz_prev, obj->fwd_x,
											 obj->fwd_y, obj->fwd_z);
	}

	/* A crossing puts the two ticks on opposite sides of (or on) the plane. */
	if ((cur_signed >= 0 && prev_signed <= 0) || (cur_signed <= 0 && prev_signed >= 0))
		return 1;
	return 0;
}

/* Pre-release training-gun entry point. Shipped game paths do not call it;
 * only its timer behavior is represented because its hardpoint format is unknown. */
// FUNCTION: TIE95 0x2A02C
void gate_updategateguns(void) {
	if ((uint16_t)gateguntimer <= frameticks) {
		gateguntimer = 59;
	} else {
		gateguntimer = (int16_t)(gateguntimer - (int16_t)frameticks);
	}
}

/* Course progression is split from mesh animation so the unlocked-rate
 * port can check the player's one-tick swept position on animation-skipped
 * ticks. gate_updategateanimations still calls this in the recovered path. */
#ifdef TIE_MODERN
void gate_updatecourseprogress(void) {
	uint16_t next_gate = (currentgate == 12) ? 1 : (uint16_t)(currentgate + 1);

	int crossed = gate_checkgateedge(next_gate);
	if (!crossed)
		return;

	currentgate = next_gate;
	++mission.train_gates_passed;
	--mission.train_gates_remaining;

	if (next_gate != 1)
		return;

	/* Level complete. Push the per-second reward count-down task and
	 * return; the task runs one decrement step every 4 PIT ticks via
	 * the flight loop's tick cadence (sim_clock advanced by TieRuntime_Tick),
	 * so the HOST_DRIVEN clock keeps progressing. The countdown's end
	 * step posts MSG_BONUS_AWARDED and bumps train_level. */
	msg_messageprintf(MSG_LEVEL_COMPLETED);
	mission.train_bonus = 0;
	if (TIE_DISPLAY_DX5) {
		g_flightDrawToOffscreenSurface = 0;
		FlightSurface_Lock();
		gate_updatebonuspoints();
		FlightSurface_Unlock();
		g_flightDrawToOffscreenSurface = 1;
		FrontendDisplay_PresentFrontSurface();
		FrontendDisplay_PresentFrame();
	} else {
		gate_updatebonuspoints();
	}
	TieBonusCountdown_Begin();
}
#endif

/* -------------------------------------------------------------------------
 * gate_trainingupdatecrt  (0x2948c)
 *
 * Draw the labeled numeric read-out ("LEVEL", "REMAIN", "PASSED", "TARGETS",
 * "SCORE") on the cockpit CRT. Labels only drawn when initpanelflag is set;
 * numeric values refreshed every call.
 * ---------------------------------------------------------------------- */

// FUNCTION: TIE95 0x2A7CC
void gate_trainingupdatecrt(uint16_t x_origin, uint16_t y_origin) {
	int16_t side_offset;
	int16_t gates_col_x;
	int16_t score_col_x;
	int16_t level_label_x;
	int16_t score_label_x;
	int16_t block_width;
	int16_t level_value_x;

	switch (flightResolution) {
		case TIE_FLIGHT_RES_SVGA:
#if defined(TIE98) || defined(TIE_MODERN)
		case TIE_FLIGHT_RES_SVGA_16:
		case TIE_FLIGHT_RES_SVGA_D3D:
#endif
			side_offset = 16;
			level_label_x = 52;
			level_value_x = 106;
			block_width = 180;
			score_label_x = 40;
			x_origin += 10;
			y_origin -= 10;
			gates_col_x = 150;
			score_col_x = 90;
			break;
		case TIE_FLIGHT_RES_VGA:
			side_offset = 8;
			level_label_x = 26;
			level_value_x = 53;
			block_width = 90;
			score_label_x = 20;
			y_origin -= 6;
			gates_col_x = 75;
			score_col_x = 45;
			break;
		default:
			y_origin -= 6;
			side_offset = 8;
			level_label_x = 26;
			level_value_x = 53;
			block_width = 90;
			score_label_x = 20;
			gates_col_x = 75;
			score_col_x = 45;
			break;
	}

	/* Choose left-or-right-of-origin based on the player's ship type. */
	if (TIE_FLIGHT_TIE98 && flightResolution != TIE_FLIGHT_RES_VGA && pstate.player_spec_num == 4) {
		/* TIE98 centers the SVGA read-out for this craft. */
	} else {
		switch (pstate.player_spec_num + 1) {
			case 8:
			case 9:
			case 12:
			case 16:
				x_origin += side_offset;
				break;
			default:
				x_origin -= side_offset;
				break;
		}
	}

	if (initpanelflag) {
		/* Static labels. setfontsize(1) updates the global `fontheight`
		 * (320x200: 5→9, 640x480: 9→21), so every Y formula reads it after
		 * the call. */
		festring_setfontsize(1);
		festring_setbound(x_origin + side_offset, y_origin, x_origin + 10 * side_offset,
						  y_origin + fontheight);
		festring_setautofill(1);
		festring_setbackcolor(0x30);
		clearwindow();

		festring_settextcolor(0x49);
		festring_setcursor(x_origin + level_label_x, y_origin);
		festring_outstring((const uint8_t*)gatelevelstr);

		festring_settextcolor(0x4A);
		festring_setcursor(x_origin + level_value_x, y_origin);
		panelrts_outnum(mission.train_level, 2, 2);

		festring_setbound(x_origin, y_origin + fontheight + 1, x_origin + block_width,
						  y_origin + 1 + 5 * fontheight);

		festring_settextcolor(0x45);
		festring_setcursor(x_origin, y_origin + fontheight + 1);
		festring_outstring((const uint8_t*)gateremainstr);

		festring_setcursor(x_origin, y_origin + 1 + 2 * fontheight);
		festring_outstring((const uint8_t*)gatepassedstr);

		festring_settextcolor(0x4D);
		festring_setcursor(x_origin, y_origin + 1 + 3 * fontheight);
		festring_outstring((const uint8_t*)targetshitstr);

		festring_settextcolor(0x51);
		festring_setcursor(x_origin + score_label_x, y_origin + 1 + 4 * fontheight);
		festring_outstring((const uint8_t*)scorestr);
	}

	/* Dynamic values (every frame). */
	festring_setfontsize(1);
	festring_setbound(x_origin, y_origin + fontheight + 1, x_origin + block_width,
					  y_origin + 1 + 5 * fontheight);
	festring_setautofill(1);
	festring_setbackcolor(0x30);
	festring_settextcolor(0x46);

	festring_setcursor(x_origin + gates_col_x, y_origin + fontheight + 1);
	panelrts_outnum((uint16_t)mission.train_gates_remaining, 3, 1);
	outchar(' ');

	festring_setcursor(x_origin + gates_col_x, y_origin + 1 + 2 * fontheight);
	panelrts_outnum((uint16_t)mission.train_gates_passed, 3, 1);
	outchar(' ');

	festring_settextcolor(0x4E);
	festring_setcursor(x_origin + gates_col_x, y_origin + 1 + 3 * fontheight);
	panelrts_outnum((uint16_t)mission.train_targets, 3, 1);
	outchar(' ');

	festring_settextcolor(0x52);
	festring_setcursor(x_origin + score_col_x, y_origin + 1 + 4 * fontheight);
	gate_outdnum(mission.mission_score, 6, 1);
	outchar(' ');

	festring_setfontsize(2);
}

/* -------------------------------------------------------------------------
 * gate_outdnum  (0x298a8)
 *
 * Print a decimal integer to the FESTRING cursor, right-aligned in a field
 * of num_digits, with at least min_digits forced to a digit character (the
 * remaining high positions are padded with spaces). Digits above 9 are
 * clamped to '9'.
 * ---------------------------------------------------------------------- */
// FUNCTION: TIE95 0x2ABE8
void gate_outdnum(int32_t value, uint16_t num_digits, uint16_t min_digits) {
	uint16_t started = 0;
	uint16_t digit;

	while (num_digits > 0) {
		int32_t divisor = powersof10[num_digits];

		/* The quotient is truncated to 16 bits before the remainder is
		 * computed, discarding any overflow beyond the field width. */
		digit = value / divisor;
		value -= digit * divisor;
		if (started == 0 && num_digits > min_digits && digit == 0) {
			digit = ' ';
		} else {
			started = 1;
			if (digit > 9)
				digit = 9;
			digit += '0';
		}
		num_digits--;
		outchar((uint8_t)digit);
	}
}

/* -------------------------------------------------------------------------
 * gate_updatebonuspoints  (0x29934)
 *
 * Redraw the HUD timer (MM:SS) and bonus score. Position is resolution-
 * dependent. Calls panel_updatepanel unless replayviewmode is set.
 * ---------------------------------------------------------------------- */
// FUNCTION: TIE95 0x2AC74
void gate_updatebonuspoints(void) {
	int16_t y, timer_x, bonus_x;

	switch (flightResolution) {
		case TIE_FLIGHT_RES_SVGA:
#if defined(TIE98) || defined(TIE_MODERN)
		case TIE_FLIGHT_RES_SVGA_16:
		case TIE_FLIGHT_RES_SVGA_D3D:
#endif
			y = 456;
			bonus_x = 465;
			timer_x = 360;
			break;
		case TIE_FLIGHT_RES_VGA:
			y = 190;
			bonus_x = 255;
			timer_x = 200;
			break;
		default:
			/* Unknown modes fall back to the 320x200 layout. */
			y = 190;
			bonus_x = 255;
			timer_x = 200;
			break;
	}

	dropflag = 1;
	festring_setbackcolor(0x2C);
	festring_settextcolor(0x43);
	festring_setautofill(0);
	festring_setfontsize(1);
	festring_setbound(0, y, (int16_t)screenXRes, (int16_t)screenYRes);
	festring_setcursor(timer_x, y);
	panelrts_outnum(timeleft.minute, 2, 2);
	outchar(':');
	panelrts_outnum(timeleft.second, 2, 2);
	festring_setcursor(bonus_x, y);
	panelrts_outnum((uint16_t)mission.train_bonus, 5, 5);

	if (!replayviewmode)
		panel_updatepanel();
}
