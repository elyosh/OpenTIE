#include <stddef.h>
#include <stdint.h>

#include "tie/draw.h"
#include "tie/drawpol.h"
#include "tie/festring.h"
#include "tie/flight_surface_tie98.h"
#include "tie/frontend_display_tie98.h"
#include "tie/fsfx.h"
#include "tie/fview.h"
#include "tie/gate.h"
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
#include "tie_runtime/runtime/profile.h"
#include <landru/task.h>

/* outchar is declared in tie.h as a function-pointer global. */

/* Player-pose history rings. 4-slot ring; slot [3] is the snapshot pose
 * used by collide_collisions on a briefing/training/combat collision.
 * NOTE: the binary writes player->heading into gatepreviouspitch[0] and
 * player->pitch into gatepreviousheading[0] (swap); readers compensate. */
int16_t gatepreviousroll[4];
int32_t gatepreviousx[4];
int32_t gatepreviousy[4];
int32_t gatepreviousz[4];
int16_t gatepreviousheading[4];
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
 * segment at D4C2C. Lower = faster mesh rotation. */
uint16_t gatespeed[20] = {
	24, 24, 24, 24, 20, 16, 14, 14, 14, 12, 12, 12, 10, 8, 6, 6, 6, 6, 6, 6,
};

/* Powers-of-ten divisors for gate_outdnum. The duplicated '1' at index 0
 * is intentional -- gate_outdnum's loop runs pos = num_digits..1 and reads
 * powersof10[pos], so pos==1 maps to divisor 1 (the ones place). Pulled
 * from the binary's data segment at D4C54. */
// GLOBAL: TIE 0xC5388
uint32_t powersof10[9] = {
	1, 1, 10, 100, 1000, 10000, 100000, 1000000, 10000000,
};

/* String pointers to the CRT labels. Populated by fediskio_loadstringdata
 * from strings.dat; this module only reads them. */
void* gatelevelstr;
void* gateremainstr;
void* gatepassedstr;
void* targetshitstr;
void* scorestr;

/* Animation timers: [0] = cargopod, [1] = wing, [2] = antenna. */
int16_t gatetimer[3];

/* Unused training-gun timer. */
int16_t gateguntimer;

/* Next gate the player must cross (1..12). Reset to 1 by
 * gate_settraininglevel; advanced by gate_updategateanimations. */
// GLOBAL: TIE 0xD4B60
uint16_t currentgate;

// GLOBAL: TIE98 0x6258DC
uint16_t gate_render_reference_object;

// FUNCTION: TIE98 0x426310 GATE_setrenderreferenceobject (inferred)
void gate_setrenderreferenceobject(uint16_t object_index) { gate_render_reference_object = object_index; }

/* -------------------------------------------------------------------------
 * Small local helpers
 * ---------------------------------------------------------------------- */

/* Clamp a 32-bit fixed-point accumulator to the range the binary uses
 * before its arithmetic right shift by 15. Matches the three-instruction
 * sequence: cmp+cmov at 0x40000000 and -0x40000000, replacing the value
 * with +/- 0x3FFF0000 (== 1073676288). */
static inline int32_t clamp_dot_30(int32_t v) {
	if (v >= 0x40000000)
		return 0x3FFF0000;
	if (v <= -0x40000000)
		return -0x3FFF0000;
	return v;
}

static inline int32_t gate_mul_q15(int16_t axis, int32_t distance) {
	return (int32_t)(((int64_t)axis * distance) >> 15);
}

/* -------------------------------------------------------------------------
 * gate_savegatelastpos  (0x27c90)
 *
 * Shift the 4-slot player-pose history rings by one frame and store the
 * current player pose into slot 0. Called per-tick from move_moveobjects
 * while a training mission is active; gate_checkgateedge reads this history
 * to perform swept-volume plane-crossing detection.
 * ---------------------------------------------------------------------- */
// FUNCTION: TIE 0x28FD0
void gate_savegatelastpos(void) {
	/* Shift rings: [3]=[2], [2]=[1], [1]=[0] (i = 2, 1, 0). */
	for (int i = 2; i >= 0; --i) {
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
	gatepreviouspitch[0] = pstate.player->heading; /* Binary writes heading here; see note. */
	gatepreviousheading[0] =
		pstate.player->pitch; /* And pitch here -- the two names are swapped vs their uses. */
}
/* Note on the field swap: the binary assigns player->heading into
 * gatepreviouspitch[0] and player->pitch into gatepreviousheading[0]. It is
 * consistent between writer and readers (collide / user), so both sides see
 * the same swap. Preserved verbatim to match the binary. */

/* -------------------------------------------------------------------------
 * gate_outdnum  (0x298a8)
 *
 * Print a decimal integer to the FESTRING cursor, right-aligned in a field
 * of num_digits, with at least min_digits forced to a digit character (the
 * remaining high positions are padded with spaces). Digits above 9 are
 * clamped to '9'.
 * ---------------------------------------------------------------------- */
// FUNCTION: TIE 0x2ABE8
void gate_outdnum(int32_t value, uint16_t num_digits, uint16_t min_digits) {
	int16_t started = 0;
	uint16_t pos = num_digits;

	while (pos) {
		uint32_t divisor = powersof10[pos];
		int32_t digit = value / (int32_t)divisor;
		/* Binary does: value -= (uint16_t)(value/divisor) * divisor.
		 * The uint16_t cast is deliberate: it discards quotient overflow
		 * beyond 16 bits before computing the remainder. */
		value -= (int32_t)((uint16_t)digit) * (int32_t)divisor;

		uint8_t ch;
		if (started || pos <= min_digits || (uint16_t)digit != 0) {
			started = 1;
			if ((uint16_t)digit > 9)
				digit = 9;
			ch = (uint8_t)(digit + '0');
		} else {
			ch = ' ';
		}
		--pos;
		outchar(ch);
	}
}

/* -------------------------------------------------------------------------
 * gate_drawtraininggate  (0x27d48)
 *
 * Render a single gate. Current gate and the one after it get the full
 * DRAW_drawcomplexobject; past gates render only the MESH_MainHull. Called
 * once per visible gate by tie_updatescreen.
 * ---------------------------------------------------------------------- */
// FUNCTION: TIE 0x29088
void gate_drawtraininggate(uint16_t obj_idx) {
	ShipModelMesh* mesh;
	uint16_t i;
	int32_t eye_x, eye_y, eye_z;
	const uint16_t* poly_detail;

	if (obj_idx == currentgate || obj_idx == (uint16_t)(currentgate + 1)) {
		/* Binary has a `if (obj_idx < currentgate) bluetarget = obj_idx;`
		 * here; given the outer condition that branch is unreachable
		 * (CSE artifact). Preserved as a dead conditional below. */
		if (obj_idx < currentgate)
			bluetarget = obj_idx;
		draw_drawcomplexobject(obj_idx);
	} else {
		draw_lockshipfileptrs(objects[obj_idx].ship_idx);
		/* Tag the parent-object with 0x7000 so the draw/pick pipeline
		 * recognises it as a training gate rather than a regular ship. */
		parentobject = (uint16_t)(obj_idx + 0x7000);
	}

	/* Find the MESH_MainHull (value 1) within the current ship's mesh list. */
	mesh = componentblockptr;
	for (i = 0; i < (uint16_t)(objectblockptr->num_meshes - 1) && mesh->mesh_type != 1; ++i)
		++mesh;
	solidindex = (int16_t)i;

	/* Save and optionally override the current-target highlight. */
	int16_t saved_target = (int16_t)currenttarget;
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

// FUNCTION: TIE98 0x425590
void gate_drawtraininggate_tie98(uint16_t object_index) {
	FlightObject* object = &objects[object_index];
	if (object_index == gate_render_reference_object ||
		object_index == (uint16_t)(gate_render_reference_object + 1)) {
		if (object_index < currentgate)
			bluetarget = object_index;
		draw_process_object_components_tie98(object_index);
		FlightModel_Draw_Object(object);
	} else {
		parentobject = (uint16_t)(object_index + 0x7000);
	}

	uint16_t main_hull_mesh_index = 0;
	const int mesh_count = modelmesh_getcount(object->ship_idx);
	while (main_hull_mesh_index < mesh_count - 1 &&
		   modelmesh_gettype(object->ship_idx, main_hull_mesh_index) != TIE_MESH_MAIN_HULL)
		++main_hull_mesh_index;

	const uint16_t saved_current_target = currenttarget;
	solidindex = (int16_t)main_hull_mesh_index;
	if (object_index < currentgate) {
		highlightcolor = 1;
		currenttarget = parentobject;
	}
	FlightModel_Draw_Object_Mesh(object, main_hull_mesh_index);
	currenttarget = saved_current_target;
}

/* -------------------------------------------------------------------------
 * gate_createtraininggates  (0x27e60)
 *
 * Build the 12-gate training course: zero each gate's FlightObject /
 * CraftData, assign ship_idx / heading / pitch / roll from a hard-coded
 * per-gate init table, call FVIEW_calcrotatemove / calcrotateorient to
 * populate the craftS/f/U basis vectors, and place the gate at a running
 * cumulative world position. Called once at mission start from
 * tie_simulator.
 * ---------------------------------------------------------------------- */
#pragma pack(push, 2)
typedef struct {
	uint16_t ship_idx[14]; /* slots 0 and 13 unused */
	int16_t heading[14];
	int16_t pitch[14];
	int16_t roll[14];
} GATE_InitTable;
#pragma pack(pop)

// FUNCTION: TIE 0x291A0
void gate_createtraininggates(void) {
	GATE_InitTable init = { 0 };

	/* Per-gate ship_idx -- alternates base post (98 = 'b') / crossbar
	 * (99 = 'c') across gates 1..12. */
	init.ship_idx[1] = 98;
	init.ship_idx[2] = 99;
	init.ship_idx[3] = 98;
	init.ship_idx[4] = 99;
	init.ship_idx[5] = 98;
	init.ship_idx[6] = 99;
	init.ship_idx[7] = 98;
	init.ship_idx[8] = 99;
	init.ship_idx[9] = 98;
	init.ship_idx[10] = 99;
	init.ship_idx[11] = 98;
	init.ship_idx[12] = 99;

	/* Heading table (radians-as-int16, 0x4000 = 90 deg). */
	init.heading[1] = 0x4000;
	init.heading[2] = 0x4000;
	init.heading[5] = 0x4000;
	init.heading[6] = 0x4000;
	init.heading[7] = (int16_t)0x8000;
	init.heading[8] = (int16_t)0x8000;
	init.heading[9] = 0x4000;
	init.heading[10] = 0x4000;
	init.heading[11] = 0x4000;
	init.heading[12] = 0x4000;

	/* Pitch table. */
	init.pitch[4] = 0x4000;
	init.pitch[5] = (int16_t)0xC000; /* -0x4000 == -90 deg */
	init.pitch[6] = (int16_t)0xC000;
	init.pitch[8] = (int16_t)0x8000;
	init.pitch[9] = (int16_t)0x8000;
	init.pitch[10] = (int16_t)0x8000;
	init.pitch[11] = 0x4000;
	init.pitch[12] = 0x4000;

	/* Roll table. */
	init.roll[6] = (int16_t)0x8000;
	init.roll[10] = 0x4000;
	init.roll[12] = 0x4000;

	mission.mission_score = 0;
	mission.train_targets = 0;

	int32_t cum_x = 0, cum_y = 0, cum_z = 0;

	for (uint16_t gate_idx = 1; gate_idx < 13; ++gate_idx) {
		uint16_t ship_idx = init.ship_idx[gate_idx];
		FlightObject* obj = &objects[gate_idx];
		CraftData* craft = &crafts[gate_idx];

		/* --- FlightObject scalar init --- */
		obj->ship_idx = (uint8_t)ship_idx;
		obj->spin_rate = 0;
		obj->current_speed = 0;
		obj->speed_remainder = 0;
		obj->death_timer = 0;
		obj->age_ticks = 0;
		obj->self_idx = 0;
		obj->idnumber = 1;
		obj->collision_radius = 0x7FFF;
		obj->category = 6;
		obj->genus = 14; /* GENUS_GATE */
		obj->ship_type_override = 0;
		obj->side = 0;
		obj->orient_dirty = 1;
		obj->move_dirty = 1;
		obj->fg_idx = 1;
		obj->anim_frame = 0;
		obj->anim_frame_alt = 0;
		obj->craft_ptr = craft;

		/* Retail writes fg_array[1].version = 5 every iteration of the
		 * gate loop (effectively a single post-loop assignment, but the
		 * binary places it inline). The version byte is consumed by
		 * other readers that gate behaviour on FG state. */
		fg_array[1].version = 5;

		/* --- CraftData per-mesh init (mesh_state / mesh_rotation / mesh_component_hp) --- */
		uint16_t component_count = 40;
		if (TieProfile_UsesTie98Logic()) {
			modelmesh_require_craft_capacity(ship_idx);
			component_count = (uint16_t)modelmesh_getcount(ship_idx) + 1;
		}
		for (uint16_t m = 0; m < component_count; ++m) {
			craft->mesh_state[m] = MESH_STATE_VISIBLE;
			craft->mesh_rotation[m] = 0;
			craft->mesh_component_hp[m] = 0xFF; /* -1 as uint8_t */
		}

		craft->hull_max = 0x7FFF;
		craft->hull_strength = 0x7FFF;
		craft->hull_damage = 0;
		craft->dead_0B0 = 0;
		craft->pad_0B4 = 0;
		craft->was_hit_flag = 0;
		craft->pad_0B6 = 0;
		craft->dock_state_flags = 0;
		craft->ai_anim_flags = 0;
		craft->beam_state = 0;
		craft->subsystem_active = 1; /* SF_SHIELDS */
		craft->is_player_craft = 0;
		craft->forward_shield = 0x7FFF;
		craft->rear_shield = 0;
		craft->status_flags = craft->subsystem_active;

		/* Publish craftptr for model-dependent code below. */
		craftptr = craft;
		const bool tie98 = TieProfile_UsesTie98Logic();
		if (!tie98)
			draw_lockshipfileptrs(ship_idx);

		/* Apply orientation. */
		obj->heading = init.heading[gate_idx];
		obj->pitch = init.pitch[gate_idx];
		obj->roll = init.roll[gate_idx];
		fview_calcrotatemove(obj->heading, obj->pitch, obj);
		fview_calcrotateorient(obj->roll, 0, obj);

		/* TIE95 derives 16-bit offsets from its object block and doubles the
		 * resulting geometry. TIE98 uses unscaled 32-bit OPT bounds. */
		int32_t fwd_step =
			tie98 ? -modelbounds_getmaxy(ship_idx) : (int16_t)-(int16_t)objectblockptr->speed_default;
		int32_t up_step_gate = 0;
		int32_t side_step = 0;
		int32_t up_step_advance = 0;
		int32_t fwd_advance = 0;

		if (tie98 && ship_idx == 98) {
			fwd_advance = modelbounds_getsizey(ship_idx);
		} else if (tie98 && ship_idx == 99) {
			const int32_t base_min_z = modelbounds_getminz(98);
			up_step_gate = modelbounds_getminz(99) - base_min_z;
			fwd_advance = base_min_z + modelbounds_getsizey(99);
			up_step_advance = base_min_z + modelbounds_getsizez(99);
		} else if (ship_idx == 98) {
			/* Base-post spacing uses model height. */
			int16_t height = (int16_t)objectblockptr->height;
			up_step_advance = 0;
			fwd_advance = height;
		} else if (ship_idx == 99) {
			/* Crossbar forward and vertical spacing use height and depth respectively. */
			int16_t shield_hi = (int16_t)(objectblockptr->shield_default >> 16);
			int16_t depth = (int16_t)objectblockptr->depth;
			int16_t height = (int16_t)objectblockptr->height;
			fwd_advance = (int16_t)(shield_hi + height);
			up_step_advance = (int16_t)(shield_hi + depth);
		}

		/* Gate position = cumulative minus the model-local placement offset. */
		const int32_t geometry_scale = tie98 ? 1 : 2;
		int32_t dx_gate = gate_mul_q15(craftf1, fwd_step) + gate_mul_q15(craftU1, up_step_gate);
		int32_t dy_gate = gate_mul_q15(craftf2, fwd_step) + gate_mul_q15(craftU2, up_step_gate);
		int32_t dz_gate = gate_mul_q15(craftf3, fwd_step) + gate_mul_q15(craftU3, up_step_gate);

		obj->world_x = cum_x - geometry_scale * dx_gate;
		obj->world_y = cum_y - geometry_scale * dy_gate;
		obj->world_z = cum_z - geometry_scale * dz_gate;
		obj->world_x_prev = obj->world_x;
		obj->world_y_prev = obj->world_y;
		obj->world_z_prev = obj->world_z;

		int32_t dx_adv = gate_mul_q15(craftf1, fwd_advance) + gate_mul_q15(craftU1, up_step_advance) +
						 gate_mul_q15(craftS1, side_step);
		int32_t dy_adv = gate_mul_q15(craftf2, fwd_advance) + gate_mul_q15(craftU2, up_step_advance) +
						 gate_mul_q15(craftS2, side_step);
		int32_t dz_adv = gate_mul_q15(craftf3, fwd_advance) + gate_mul_q15(craftU3, up_step_advance) +
						 gate_mul_q15(craftS3, side_step);

		cum_x += geometry_scale * dx_adv;
		cum_y += geometry_scale * dy_adv;
		cum_z += geometry_scale * dz_adv;
	}
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
// FUNCTION: TIE 0x297D8
void gate_settraininglevel(uint16_t level) {
	currentgate = 1;
	mission.train_gates_remaining = 12;
	mission.train_gates_passed = 0;

	if (level <= 8) {
		mtimer_min = (uint8_t)((10 - level) / 2);
		mtimer_sec = (uint8_t)(30 * (level & 1));
	} else {
		mtimer_min = 0;
		mtimer_sec = (uint8_t)(60 - 5 * (level - 8));
	}

	uint8_t speed_wing = (uint8_t)(24 * level);
	uint8_t speed_gun = (uint8_t)(2 * level);
	uint8_t speed_pod = (uint8_t)(3 * level);

	CraftData* craft = craftptr;
	for (uint16_t obj_idx = 0; obj_idx < NUM_CRAFTS; ++obj_idx) {
		uint16_t ship_idx = objects[obj_idx].ship_idx;
		if ((uint8_t)ship_idx == 0 || objects[obj_idx].genus != 14 /* GENUS_GATE */)
			continue;

		craftptr = objects[obj_idx].craft_ptr;
		const bool tie98 = TieProfile_UsesTie98Logic();
		if (tie98)
			modelmesh_require_craft_capacity(ship_idx);
		else
			draw_lockshipfileptrs(ship_idx);
		craft = craftptr;

		const uint16_t mesh_count =
			tie98 ? (uint16_t)modelmesh_getcount(ship_idx) : (uint16_t)objectblockptr->num_meshes;
		for (uint16_t mesh_idx = 0; mesh_idx < mesh_count; ++mesh_idx) {
			uint16_t mesh_type = tie98 ? (uint16_t)modelmesh_gettype(ship_idx, mesh_idx)
									   : componentblockptr[mesh_idx].mesh_type;

			if (obj_idx == 1) {
				/* Gate 1 (course start) is always frozen. */
				craft->mesh_component_hp[mesh_idx] = (mesh_type == 1 /* MESH_MainHull */) ? 0xFF : 0;
				craft->mesh_state[mesh_idx] = MESH_STATE_HIDDEN;
				continue;
			}

			/* mesh_type switch follows the binary's cmp chain. */
			if (mesh_type == 0 /* MESH_Default */) {
				continue;
			}
			if (mesh_type == 1 /* MESH_MainHull */) {
				craft->mesh_state[mesh_idx] = MESH_STATE_HIDDEN;
				continue;
			}
			if (mesh_type == TIE_MESH_WING) {
				if (tie98) {
					modelmesh_enableexplosiontype2(ship_idx, mesh_idx);
					modelmesh_enableexplosiontype1(ship_idx, mesh_idx);
				} else {
					componentblockptr[mesh_idx].flags =
						(int16_t)((uint16_t)componentblockptr[mesh_idx].flags | 3);
				}
				if (level < 3) {
					craft->mesh_state[mesh_idx] = MESH_STATE_HIDDEN;
					craft->mesh_component_hp[mesh_idx] = 0;
				} else {
					craft->mesh_state[mesh_idx] = MESH_STATE_VISIBLE;
					craft->mesh_rotation[mesh_idx] = 0;
					craft->mesh_component_hp[mesh_idx] = speed_wing;
				}
				continue;
			}
			if (mesh_type == 5 /* MESH_SmallGun */) {
				/* Retail jumps directly to the mesh_component_hp write,
				 * bypassing the mesh_rotation=0 store the other branches
				 * use. Leave mesh_rotation untouched here. */
				craft->mesh_state[mesh_idx] = MESH_STATE_VISIBLE;
				craft->mesh_component_hp[mesh_idx] = speed_gun;
				continue;
			}
			if (mesh_type == TIE_MESH_CARGO_POD) {
				if (tie98) {
					modelmesh_enableexplosiontype2(ship_idx, mesh_idx);
					modelmesh_enableexplosiontype1(ship_idx, mesh_idx);
				} else {
					componentblockptr[mesh_idx].flags =
						(int16_t)((uint16_t)componentblockptr[mesh_idx].flags | 3);
				}
				if (level < 2) {
					craft->mesh_state[mesh_idx] = MESH_STATE_HIDDEN;
					craft->mesh_component_hp[mesh_idx] = 0;
				} else {
					craft->mesh_state[mesh_idx] = MESH_STATE_VISIBLE;
					craft->mesh_rotation[mesh_idx] = 0;
					craft->mesh_component_hp[mesh_idx] = speed_pod;
				}
				continue;
			}
			if (mesh_type == 18 /* MESH_MiscHull */) {
				if (level < 5) {
					craft->mesh_state[mesh_idx] = MESH_STATE_HIDDEN;
					craft->mesh_component_hp[mesh_idx] = 0;
				} else {
					craft->mesh_state[mesh_idx] = MESH_STATE_VISIBLE;
					craft->mesh_rotation[mesh_idx] = tie98 ? 1 : 0;
					craft->mesh_component_hp[mesh_idx] = 0xFF;
				}
				continue;
			}
			if (mesh_type == 19 /* MESH_Antenna */) {
				if (level >= 7) {
					craft->mesh_state[mesh_idx] = MESH_STATE_VISIBLE;
					craft->mesh_rotation[mesh_idx] = 0;
					craft->mesh_component_hp[mesh_idx] = 0xFF;
				} else {
					craft->mesh_state[mesh_idx] = MESH_STATE_HIDDEN;
					craft->mesh_component_hp[mesh_idx] = 0;
				}
				continue;
			}
			/* Other mesh types: leave as-is. */
		}
	}

	/* Match the binary: craftptr is left pointing at the last gate's craft
	 * pointer. No caller of settraininglevel reads it immediately. */
	craftptr = craft;

	if (!replayviewmode)
		panel_initpanel();
}

/* -------------------------------------------------------------------------
 * gate_checkgateedge  (0x28a54)
 *
 * Swept-volume test: did the player cross the plane of gate `obj_idx`
 * between the previous tick and this one?
 * ---------------------------------------------------------------------- */
// FUNCTION: TIE 0x29D94
int gate_checkgateedge(uint16_t obj_idx) {
	FlightObject* obj = &objects[obj_idx];
	uint8_t ship_idx = obj->ship_idx;

	const bool tie98 = TieProfile_UsesTie98Logic();
	if (!tie98)
		draw_lockshipfileptrs(ship_idx);

	int32_t base_offset;
	if (ship_idx == 98) {
		base_offset =
			tie98 ? -modelbounds_getmaxy(ship_idx) : (int16_t)(-(int16_t)objectblockptr->speed_default);
	} else {
		base_offset = 0;
	}

	int32_t fwd_offset;
	if (tie98)
		fwd_offset = (obj_idx == currentgate) ? base_offset - 1024 : base_offset + 32;
	else
		fwd_offset = (obj_idx == currentgate) ? (int16_t)(base_offset - 1024) : (int16_t)(base_offset + 32);

	const int32_t geometry_scale = tie98 ? 1 : 2;
	int32_t plane_x = obj->world_x + geometry_scale * gate_mul_q15(obj->fwd_x, fwd_offset);
	int32_t plane_y = obj->world_y + geometry_scale * gate_mul_q15(obj->fwd_y, fwd_offset);
	int32_t plane_z = obj->world_z + geometry_scale * gate_mul_q15(obj->fwd_z, fwd_offset);

	int32_t dx_cur = pstate.player->world_x - plane_x;
	int32_t dy_cur = pstate.player->world_y - plane_y;
	int32_t dz_cur = pstate.player->world_z - plane_z;

	/* Fast reject on the current-tick position. */
	if (dx_cur > 0x4000 || dx_cur < -0x4000)
		return 0;
	if (dy_cur > 0x4000 || dy_cur < -0x4000)
		return 0;
	if (dz_cur > 0x4000 || dz_cur < -0x4000)
		return 0;

	int32_t dx_prev = pstate.player->world_x_prev - plane_x;
	int32_t dy_prev = pstate.player->world_y_prev - plane_y;
	int32_t dz_prev = pstate.player->world_z_prev - plane_z;

	if (dx_prev > 0x4000 || dx_prev < -0x4000 || dy_prev > 0x4000 || dy_prev < -0x4000 || dz_prev > 0x4000 ||
		dz_prev < -0x4000)
		return 0;

	int32_t cur_signed;
	int32_t prev_signed;
	if (tie98) {
		cur_signed = gate_mul_q15(obj->fwd_x, dx_cur) + gate_mul_q15(obj->fwd_y, dy_cur) +
					 gate_mul_q15(obj->fwd_z, dz_cur);
		prev_signed = gate_mul_q15(obj->fwd_x, dx_prev) + gate_mul_q15(obj->fwd_y, dy_prev) +
					  gate_mul_q15(obj->fwd_z, dz_prev);
	} else {
		int32_t dot_cur = (int32_t)obj->fwd_z * (int16_t)dz_cur + (int32_t)obj->fwd_y * (int16_t)dy_cur +
						  (int32_t)obj->fwd_x * (int16_t)dx_cur;
		cur_signed = clamp_dot_30(dot_cur) >> 15;
		int32_t dot_prev = (int32_t)obj->fwd_z * (int16_t)dz_prev + (int32_t)obj->fwd_y * (int16_t)dy_prev +
						   (int32_t)obj->fwd_x * (int16_t)dx_prev;
		prev_signed = clamp_dot_30(dot_prev) >> 15;
	}

	/* Same-sign on both ticks = no crossing. The binary encodes this as
	 * two OR'd sign/polarity checks; the truth-table collapse is:
	 * return 0 if sign(cur) == sign(prev). */
	int cur_neg = ((uint16_t)cur_signed & 0x8000u) != 0;
	int prev_neg = ((uint16_t)prev_signed & 0x8000u) != 0;
	int cur_pos = (int16_t)cur_signed > 0;
	int prev_pos = (int16_t)prev_signed > 0;

	if ((cur_neg || prev_pos) && (cur_pos || prev_neg))
		return 0;
	return 1;
}

/* -------------------------------------------------------------------------
 * gate_updatebonuspoints  (0x29934)
 *
 * Redraw the HUD timer (MM:SS) and bonus score. Position is resolution-
 * dependent. Calls panel_updatepanel unless replayviewmode is set.
 * ---------------------------------------------------------------------- */
// FUNCTION: TIE 0x2AC74
void gate_updatebonuspoints(void) {
	int16_t y, bonus_x, timer_x;

	if (tie_is_high_resolution_flight()) {
		y = 456;
		bonus_x = 465;
		timer_x = 360;
	} else {
		/* Every other resolution (including the 320x200 default) uses this
		 * layout -- matches the binary's fall-through. */
		y = 190;
		bonus_x = 255;
		timer_x = 200;
	}

	dropflag = 1;
	festring_setbackcolor(0x2C);
	festring_settextcolor(0x43);
	festring_setautofill(0);
	festring_setfontsize(1);
	festring_setbound(0, y, (int16_t)screenXRes, (int16_t)screenYRes);
	festring_setcursor(timer_x, y);
	panelrts_outnum(mtimer_min, 2, 2);
	outchar(':');
	panelrts_outnum(mtimer_sec, 2, 2);
	festring_setcursor(bonus_x, y);
	panelrts_outnum((uint16_t)mission.train_bonus, 5, 5);

	if (!replayviewmode)
		panel_updatepanel();
}

/* Course progression is split from mesh animation so the unlocked-rate
 * port can check the player's one-tick swept position on animation-skipped
 * ticks. gate_updategateanimations still calls this in the recovered path. */
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
	if (TieClassicDisplay_UsesDx5()) {
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
	gate_Push_Bonus_Countdown_Task();
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
// FUNCTION: TIE 0x29AB4, TIE98 0x426020
void gate_updategateanimations(void) {
	int16_t delta_cargopod = 0;
	int16_t delta_wing = 0;
	int16_t delta_antenna = 0;
	int16_t* deltas[3] = { &delta_cargopod, &delta_wing, &delta_antenna };

	CraftData* saved_craft = craftptr;

	/* Phase 1: timers. */
	for (uint16_t timer_idx = 0; timer_idx < 3; ++timer_idx) {
		int16_t new_timer = (int16_t)(gatetimer[timer_idx] - (int16_t)frameticks);
		gatetimer[timer_idx] = new_timer;
		if (new_timer >= 0) {
			*deltas[timer_idx] = 0;
			continue;
		}

		uint16_t period;
		if (timer_idx == 2) {
			/* Binary uses *((word*)&off_D4C28 + train_level + 1), which
			 * evaluates to gatespeed[train_level - 1] since off_D4C28
			 * happens to sit 4 bytes before gatespeed in the data segment.
			 * Clamp to avoid an under-flow read at train_level == 0
			 * (antennas only actually animate at train_level >= 6 so this
			 * is a defensive bound, not a gameplay change). */
			uint8_t tl = mission.train_level;
			period = (tl >= 1) ? gatespeed[tl - 1] : gatespeed[0];
		} else {
			period = gatespeed[mission.train_level];
		}
		if (period == 0)
			period = 1; /* defensive guard, unreachable on shipped data */

		/* The binary's `-(*(int*)((char*)&gatepassedstr + 2*i + 2) >> 16)`
		 * is just the post-decrement value of gatetimer[timer_idx]: the
		 * Watcom compiler emitted a load-base pattern that made the memory
		 * read overlap with gatetimer through the preceding string
		 * pointers. Resolved directly here. */
		int16_t negated = (int16_t)(-new_timer);
		int16_t steps = (int16_t)((uint16_t)negated / period + 1);
		gatetimer[timer_idx] = (int16_t)(gatetimer[timer_idx] + (int16_t)(steps * period));
		*deltas[timer_idx] = steps;
	}

	/* Phase 2: apply deltas to each gate's meshes. */
	for (uint16_t j = 1; j < 13; ++j) {
		uint16_t ship_idx = objects[j].ship_idx;
		craftptr = saved_craft;
		const bool tie98 = TieProfile_UsesTie98Logic();
		if (!tie98)
			draw_lockshipfileptrs(ship_idx);

		CraftData* craft = objects[j].craft_ptr;
		uint16_t num_meshes =
			tie98 ? (uint16_t)modelmesh_getcount(ship_idx) : (uint16_t)objectblockptr->num_meshes;

		for (uint16_t mesh_idx = 0; mesh_idx < num_meshes; ++mesh_idx) {
			uint16_t mesh_type = tie98 ? (uint16_t)modelmesh_gettype(ship_idx, mesh_idx)
									   : componentblockptr[mesh_idx].mesh_type;
			int16_t delta = 0;

			if (mesh_type == 17 /* MESH_CargoPod */) {
				if (mission.train_level < 3)
					continue;
				delta = delta_cargopod;
			} else if (mesh_type == 2 /* MESH_Wing */) {
				if (mission.train_level < 4)
					continue;
				delta = delta_wing;
			} else if (mesh_type == 18 || mesh_type == 19 /* MESH_Antenna */) {
				if (mission.train_level < 6)
					continue;
				delta = delta_antenna;
			} else {
				continue;
			}

			craft->mesh_rotation[mesh_idx] = (uint8_t)(craft->mesh_rotation[mesh_idx] + (uint8_t)delta);
		}
		saved_craft = craft;
	}

	/* Phase 3: check the next gate. */
	craftptr = saved_craft;
	gate_updatecourseprogress();
}

/* The host advances the simulation clock between runtime ticks, so the
 * countdown must yield instead of waiting synchronously for its next tick. */

typedef struct BonusCountdownTask {
	uint16_t tickbudget; /* accumulated PIT ticks since last decrement */
} BonusCountdownTask;

/* Set while the countdown task is on the stack. Mirrors the window
 * during which `gate_updatebonuspoints` overdraws the bonus-bar
 * region every step — outside that window panel_updatepanel's
 * cockpit-bitmap paint leaves the region bare. Host renderers read
 * via gate.h to gate the bonus-bar emission to the same window. */
uint8_t bonus_countdown_active;

static LandruTaskStepResult bonus_countdown_step(void* self) {
	BonusCountdownTask* t = (BonusCountdownTask*)self;

	if (!mtimer_min && !mtimer_sec) {
		argtable[0] = (uint16_t)mission.train_bonus;
		msg_messageprintf(MSG_BONUS_AWARDED);
		if (TieClassicDisplay_UsesDx5())
			FlightSurface_Lock();
		gate_settraininglevel(++mission.train_level);
		if (TieClassicDisplay_UsesDx5())
			FlightSurface_Unlock();
		bonus_countdown_active = 0;
		return LANDRU_TASK_STEP_DONE;
	}

	t->tickbudget = (uint16_t)(t->tickbudget + (uint16_t)xtimer_time_elapsed());
	if (t->tickbudget < 4)
		return LANDRU_TASK_STEP_YIELD; /* xtimer cursor advances between tie_ticks */
	t->tickbudget = 0;

	/* One iteration of the original loop body. */
	if (mtimer_sec) {
		--mtimer_sec;
	} else {
		mtimer_sec = 59;
		--mtimer_min;
	}

	mission.mission_score += 10;
	mission.train_bonus += 10;

	if ((mission.mission_score % 100) == 0)
		fsfx_triggersfx(0x21, 0xFFFF);

	if (TieClassicDisplay_UsesDx5()) {
		g_flightDrawToOffscreenSurface = 0;
		FlightSurface_Lock();
		gate_updatebonuspoints();
		FlightSurface_Unlock();
		g_flightDrawToOffscreenSurface = 1;
		FrontendDisplay_PresentFrame();
	} else {
		gate_updatebonuspoints();
	}
	return LANDRU_TASK_STEP_CONTINUE;
}

static const LandruTaskVtable bonus_countdown_task_vt = {
	.step = bonus_countdown_step,
};

void gate_Push_Bonus_Countdown_Task(void) {
	BonusCountdownTask* t = (BonusCountdownTask*)landru_task_push(&bonus_countdown_task_vt);
	if (!t)
		return;
	t->tickbudget = 0;
	bonus_countdown_active = 1;
}

/* -------------------------------------------------------------------------
 * gate_trainingupdatecrt  (0x2948c)
 *
 * Draw the labeled numeric read-out ("LEVEL", "REMAIN", "PASSED", "TARGETS",
 * "SCORE") on the cockpit CRT. Labels only drawn when initpanelflag is set;
 * numeric values refreshed every call.
 * ---------------------------------------------------------------------- */

// FUNCTION: TIE 0x2A7CC
void gate_trainingupdatecrt(int16_t x_origin, int16_t y_origin) {
	int16_t side_offset;
	int16_t y;
	int16_t level_label_x, level_value_x, score_label_x, block_width;
	int16_t gates_col_x, score_col_x;

	if (tie_is_high_resolution_flight()) {
		x_origin += 10;
		y = (int16_t)(y_origin - 10);
		side_offset = 16;
		level_label_x = 52;
		level_value_x = 106;
		block_width = 180;
		score_label_x = 40;
		gates_col_x = 150;
		score_col_x = 90;
	} else {
		/* Both flightResolution == TIE_FLIGHT_RES_VGA and the fall-through default path
		 * use the 320x200 layout. */
		y = (int16_t)(y_origin - 6);
		side_offset = 8;
		level_label_x = 26;
		level_value_x = 53;
		block_width = 90;
		score_label_x = 20;
		gates_col_x = 75;
		score_col_x = 45;
	}

	/* Choose left-or-right-of-origin based on the player's ship type. */
	uint32_t spec_plus_1 = (uint32_t)pstate.player_spec_num + 1;
	int draw_right;
	if (spec_plus_1 < 12) {
		if (spec_plus_1 < 8 || spec_plus_1 > 9)
			draw_right = 0;
		else
			draw_right = 1;
	} else if (spec_plus_1 <= 12 || pstate.player_spec_num == 15) {
		draw_right = 1;
	} else {
		draw_right = 0;
	}
	int16_t crt_x;
	if (TieProfile_UsesTie98Logic() && tie_is_high_resolution_flight() && pstate.player_spec_num == 4)
		crt_x = x_origin;
	else
		crt_x = draw_right ? (int16_t)(x_origin + side_offset) : (int16_t)(x_origin - side_offset);

	if (initpanelflag) {
		/* Static labels. setfontsize(1) updates the global `fontheight`
		 * (320x200: 5→9, 640x480: 9→21). Retail uses the global directly
		 * in every Y formula below, so we read it AFTER the setfontsize
		 * call. Caching the pre-call height left the dynamic-value block
		 * using the size-2 fontheight, which compressed the value rows
		 * vertically and ran them up over the static label column. */
		festring_setfontsize(1);
		uint8_t fh = fontheight;
		festring_setbound((int16_t)(crt_x + side_offset), y, (int16_t)(crt_x + 10 * side_offset),
						  (int16_t)(y + fh));
		festring_setautofill(1);
		festring_setbackcolor(0x30);
		clearwindow();

		festring_settextcolor(0x49);
		festring_setcursor((int16_t)(crt_x + level_label_x), y);
		festring_outstring((const uint8_t*)gatelevelstr);

		festring_settextcolor(0x4A);
		festring_setcursor((int16_t)(crt_x + level_value_x), y);
		panelrts_outnum(mission.train_level, 2, 2);

		festring_setbound(crt_x, (int16_t)(y + fh + 1), (int16_t)(crt_x + block_width),
						  (int16_t)(y + 1 + 5 * fh));

		festring_settextcolor(0x45);
		festring_setcursor(crt_x, (int16_t)(y + fh + 1));
		festring_outstring((const uint8_t*)gateremainstr);

		festring_setcursor(crt_x, (int16_t)(y + 1 + 2 * fh));
		festring_outstring((const uint8_t*)gatepassedstr);

		festring_settextcolor(0x4D);
		festring_setcursor(crt_x, (int16_t)(y + 1 + 3 * fh));
		festring_outstring((const uint8_t*)targetshitstr);

		festring_settextcolor(0x51);
		festring_setcursor((int16_t)(crt_x + score_label_x), (int16_t)(y + 1 + 4 * fh));
		festring_outstring((const uint8_t*)scorestr);
	}

	/* Dynamic values (every frame). Re-capture fontheight after the local
	 * setfontsize(1) so this block doesn't depend on whatever fontsize
	 * the caller chain left in place. */
	festring_setfontsize(1);
	uint8_t fh = fontheight;
	festring_setbound(crt_x, (int16_t)(y + fh + 1), (int16_t)(crt_x + block_width),
					  (int16_t)(y + 1 + 5 * fh));
	festring_setautofill(1);
	festring_setbackcolor(0x30);
	festring_settextcolor(0x46);

	festring_setcursor((int16_t)(crt_x + gates_col_x), (int16_t)(fh + y + 1));
	panelrts_outnum((uint16_t)mission.train_gates_remaining, 3, 1);
	outchar(' ');

	int16_t y_plus_1 = (int16_t)(y + 1);
	festring_setcursor((int16_t)(crt_x + gates_col_x), (int16_t)(y_plus_1 + 2 * fh));
	panelrts_outnum((uint16_t)mission.train_gates_passed, 3, 1);
	outchar(' ');

	festring_settextcolor(0x4E);
	festring_setcursor((int16_t)(crt_x + gates_col_x), (int16_t)(y_plus_1 + 3 * fh));
	panelrts_outnum((uint16_t)mission.train_targets, 3, 1);
	outchar(' ');

	festring_settextcolor(0x52);
	festring_setcursor((int16_t)(score_col_x + crt_x), (int16_t)(y_plus_1 + 4 * fh));
	gate_outdnum(mission.mission_score, 6, 1);
	outchar(' ');

	festring_setfontsize(2);
}

/* Pre-release training-gun entry point. Shipped game paths do not call it;
 * only its timer behavior is represented because its hardpoint format is unknown. */
// FUNCTION: TIE 0x2A02C
void gate_updategateguns(void) {
	if ((uint16_t)gateguntimer <= frameticks) {
		gateguntimer = 59;
	} else {
		gateguntimer = (int16_t)(gateguntimer - (int16_t)frameticks);
	}
}
