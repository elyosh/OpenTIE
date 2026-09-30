#include "tie/starship.h"
#include "tie/collide.h"
#include "tie/collide_opt.h"
#include "tie/create.h"
#include "tie/draw.h"
#include "tie/edition.h"
#include "tie/fsfx.h"
#include "tie/fview.h"
#include "tie/laser.h"
#include "tie/math2.h"
#include "tie/math2_wide.h"
#include "tie/modelbounds.h"
#include "tie/modelmesh.h"
#include "tie/pai.h"
#include "tie/panel.h"
#include "tie/species.h"
#include "tie/tie.h"
#include "tie/trig2.h"
#ifdef TIE_MODERN
#include "tie_runtime/timing/flight_timing_state.h"
#endif

/* --- external globals referenced by the module --- */

/* MissionClock `_date` owned by tie.c (declared in tie.h). The hour /
 * minute / second fields are the elapsed wall clock; `_date.subsec` is
 * the per-second sub-tick counter (refilled with 236 on the seconds
 * boundary, decremented per frame). starship_firelasergunner reads
 * subsec as a cheap "first half of current second" gate for switching
 * between BSP-walk and spec-table hardpoint lookup. */

/* Mission timer bytes owned by tie.c; incremented by 2 per component kill
 * in training mode. */

#include "tie/user.h" /* user_validcomponent */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

// FUNCTION: TIE98 0x487000
// STARSHIP_damagecomponent
static uint16_t starship_damagecomponent_tie98(uint16_t obj_idx, int16_t component_plus1, uint16_t damage) {
	const uint16_t component_idx = (uint16_t)(component_plus1 - 1);
	const uint8_t hp = craftptr->mesh_component_hp[component_idx];
	uint16_t damage_units;
	uint16_t model_type;
	uint16_t new_obj;
	FlightObject* parent;
	FlightObject* ember;
	int center_x;
	int center_y;
	int center_z;
	int effect_size;

	if (hp == 0 || hp == 0xFF)
		return damage;

	damage_units = damage >> 4;
	if (damage_units == 0)
		damage_units = 1;
	if (hp > damage_units) {
		craftptr->mesh_component_hp[component_idx] = (uint8_t)(hp - damage_units);
		return 0;
	}

	damage = (uint16_t)(16 * (damage_units - hp));
	craftptr->mesh_component_hp[component_idx] = 0;
	model_type = objects[obj_idx].ship_idx;
	if (!modelmesh_isobjecttypemeshdamageable(model_type, component_idx))
		return damage;

	craftptr->mesh_state[component_idx] = MESH_STATE_HIDDEN;
	if (obj_idx == pstate.target_obj_idx && component_idx == pstate.radar_target1) {
		do {
			if (++pstate.radar_target1 >= modelmesh_getcount(model_type))
				pstate.radar_target1 = 0;
		} while ((craftptr->mesh_state[pstate.radar_target1] != MESH_STATE_VISIBLE ||
				  !user_validcomponent_tie98(model_type, pstate.radar_target1)) &&
				 component_idx != pstate.radar_target1);
	}

	if (mission.train_craft_type) {
		++mission.train_targets;
		mission.mission_score += 50;
		if (craftptr->mesh_rotation[component_idx])
			mission.mission_score += 50;
		timeleft.second = (uint8_t)(timeleft.second + 2);
		if (timeleft.second >= 60) {
			timeleft.second = (uint8_t)(timeleft.second - 60);
			++timeleft.minute;
		}
	}

	new_obj = create_findslot(GENUS_EXPLOSION);
	if (new_obj == 0xFFFF)
		return damage;

	parent = &objects[obj_idx];
	ember = &objects[new_obj];
	ember->world_x = parent->world_x;
	ember->world_y = parent->world_y;
	ember->world_z = parent->world_z;

	center_x = modelmesh_getcenterx(model_type, component_idx);
	center_y = modelmesh_getcentery(model_type, component_idx);
	center_z = modelmesh_getcenterz(model_type, component_idx);
	if (mission.train_craft_type && craftptr->mesh_rotation[component_idx]) {
		const uint16_t angle = (uint16_t)craftptr->mesh_rotation[component_idx] << 8;
		const int32_t sine = trig2_getsignedsin(angle);
		const int32_t cosine = trig2_getsignedcos((int16_t)angle);
		const int old_x = center_x;
		int32_t rotated;

		rotated = (int32_t)((uint32_t)center_z * (0u - (uint32_t)sine) + (uint32_t)old_x * (uint32_t)cosine);
		if (rotated >= 0x40000000)
			rotated = 0x3fffffff;
		if (rotated <= -0x40000000)
			rotated = -0x3fff0000;
		center_x = (int32_t)(rotated >> 15);
		rotated = (int32_t)((uint32_t)center_z * (uint32_t)cosine + (uint32_t)old_x * (uint32_t)sine);
		if (rotated >= 0x40000000)
			rotated = 0x3fffffff;
		if (rotated <= -0x40000000)
			rotated = -0x3fff0000;
		center_z = (int32_t)(rotated >> 15);
	}
	pai_calcrotatedpoint(parent, (int16_t)center_x, (int16_t)center_z, (int16_t)-center_y);

	ember->world_x += rotatedx;
	ember->world_y += rotatedy;
	ember->world_z += rotatedz;
	ember->craft_ptr = NULL;
	ember->ship_idx = 129;
	ember->genus = GENUS_EXPLOSION;
	ember->category = 5;
	ember->anim_frame = 2;
	ember->age_ticks = 0;
	ember->death_timer = 0;
	ember->current_speed = parent->current_speed;
	ember->pitch = parent->pitch;
	ember->heading = parent->heading;
	ember->roll = 0;
	ember->orient_dirty = 1;
	ember->move_dirty = 1;
	fsfx_triggersfx((uint16_t)((math2_getrandom() & 3) + 19), new_obj);

	effect_size = modelmesh_getcomponentmaxextent(model_type, component_idx) >> 9;
	if (effect_size > 255)
		effect_size = 255;
	ember->damage_state = (uint8_t)effect_size;
	return damage;
}

/* ---------------------------------------------------------------- *
 * STARSHIP_getcoordvalue -- resolve one 16-bit coordinate from a BSP
 * vertex stream, following the back-reference chain. When the high byte
 * of a record is 0x7F, the low byte N is a back-reference count and the
 * walk steps N*3 bytes backward. Retail callers inline the walk.
 * ---------------------------------------------------------------- */

// FUNCTION: TIE95 0x52EC0
int16_t starship_getcoordvalue(const uint8_t* bsp_coord) {
	int16_t value;

	while (bsp_coord[1] == 0x7F)
		bsp_coord -= 3 * (int)bsp_coord[0];
	memcpy(&value, bsp_coord, sizeof value);
	return value;
}

/* ---------------------------------------------------------------- *
 * STARSHIP_checkstarshiphit -- per-mesh laser hit test (@0x4F8EC)
 * ---------------------------------------------------------------- */

// FUNCTION: TIE95 0x52EDC
uint16_t starship_checkstarshiphit(uint16_t shooter_obj_idx, uint16_t target_obj_idx) {
	FlightObject* craft;
	int32_t world_x;
	int32_t world_y;
	int32_t world_z;
	uint8_t ship_idx;
	int32_t dx;
	int32_t dy;
	int32_t dz;
	int32_t dxold;
	int32_t dyold;
	int32_t dzold;
	int32_t side_cur;
	int32_t fwd_cur;
	int32_t up_cur;
	int32_t side_prev;
	int32_t fwd_prev;
	int32_t up_prev;
	int32_t base_side_cur, base_up_cur, base_side_prev, base_up_prev;
	int32_t scaled_fwd_cur, scaled_fwd_prev;
	ShipModelMesh* mesh;
	unsigned int num_meshes;
	uint16_t hit_component;
	int32_t best_frac;
	unsigned int i;

	if (TIE_FLIGHT_TIE98)
		return collide_checksweptmodelcollision(shooter_obj_idx, target_obj_idx);

	craft = &objects[target_obj_idx];
	craftptr = craft->craft_ptr;

	world_x = craft->world_x;
	world_y = craft->world_y;
	world_z = craft->world_z;
	ship_idx = craft->ship_idx;

	/* Laser segment relative to the craft's world origin */
	dx = laserx - world_x;
	dy = lasery - world_y;
	dz = laserz - world_z;
	dxold = laserxold - world_x;
	dyold = laseryold - world_y;
	dzold = laserzold - world_z;

	/* Rebuild local orientation matrix if dirty */
	if (craft->orient_dirty) {
		fview_calcrotatemove(craft->pitch, craft->heading, craft);
		fview_calcrotateorient(craft->roll, 0, craft);
	}

	/* Dot-project (dx, dy, dz) and (dxold, dyold, dzold) onto the craft's
	 * local (side, -fwd, up) axes. The binary packs each dot via the Watcom
	 * unaligned-dword trick; the translated form below is a plain Q15 dot
	 * product against the craft's orientation vectors. The fwd axis result
	 * is stored negated (matches the Watcom pipeline convention used by
	 * PAI_calcrotatedpoint). */
	side_cur = math2_mul_q15(dx, craft->side_x) + math2_mul_q15(dy, craft->side_y) +
			   math2_mul_q15(dz, craft->side_z);
	fwd_cur = -(math2_mul_q15(dx, craft->fwd_x) + math2_mul_q15(dy, craft->fwd_y) +
				math2_mul_q15(dz, craft->fwd_z));
	up_cur = math2_mul_q15(dx, craft->up_x) + math2_mul_q15(dy, craft->up_y) + math2_mul_q15(dz, craft->up_z);

	side_prev = math2_mul_q15(dxold, craft->side_x) + math2_mul_q15(dyold, craft->side_y) +
				math2_mul_q15(dzold, craft->side_z);
	fwd_prev = -(math2_mul_q15(dxold, craft->fwd_x) + math2_mul_q15(dyold, craft->fwd_y) +
				 math2_mul_q15(dzold, craft->fwd_z));
	up_prev = math2_mul_q15(dxold, craft->up_x) + math2_mul_q15(dyold, craft->up_y) +
			  math2_mul_q15(dzold, craft->up_z);

	/* Scale by 2^(model_scale_shift - 1). model_scale_shift == 0 means the ship has no LOD
	 * and the laser coords are left-shifted one bit (same effect as
	 * model_scale_shift == 1 taking the default, but the branch exists in the binary
	 * so we reproduce it). */

	if (objectblockptr->model_scale_shift) {
		const int shift = objectblockptr->model_scale_shift - 1;
		base_side_cur = side_cur >> shift;
		base_up_cur = up_cur >> shift;
		scaled_fwd_cur = fwd_cur >> shift;
		base_side_prev = side_prev >> shift;
		base_up_prev = up_prev >> shift;
		scaled_fwd_prev = fwd_prev >> shift;
	} else {
		base_side_cur = 2 * side_cur;
		base_up_cur = 2 * up_cur;
		scaled_fwd_cur = 2 * fwd_cur;
		base_side_prev = 2 * side_prev;
		base_up_prev = 2 * up_prev;
		scaled_fwd_prev = 2 * fwd_prev;
	}

	/* Make sure componentblockptr / objectblockptr / num_meshes reflect
	 * this craft's model. The caller (collide_lasercraftcollide) hasn't
	 * necessarily locked our ship; do it here. */
	draw_lockshipfileptrs(ship_idx);

	mesh = componentblockptr;
	num_meshes = objectblockptr->num_meshes;

	hit_component = 0;
	best_frac = 0x7FFFFFFF;

	for (i = 0; i < num_meshes; ++i, ++mesh) {
		int32_t side_cur_r;
		int32_t up_cur_r;
		int32_t side_prev_r;
		int32_t up_prev_r;
		int32_t is_main_hull_ship98;
		uint8_t rot;
		int32_t bbox_min_side;
		int32_t bbox_min_fwd;
		int32_t bbox_max_fwd;
		int32_t bbox_min_up;
		int32_t bbox_max_up;
		int32_t bbox_max_side;
		int in_bbox;
		const uint8_t* lod_bytes;
		int32_t distance;
		uint16_t final_offset;
		const uint8_t* poly_hdr;
		int32_t hit_frac;

		if (!craftptr->mesh_component_hp[i])
			continue;

		side_cur_r = base_side_cur;
		up_cur_r = base_up_cur;
		side_prev_r = base_side_prev;
		up_prev_r = base_up_prev;
		is_main_hull_ship98 = 0;

		/* Mesh-rotation handling. Retail splits two paths:
		 *   - mission.train_craft_type != 0 (training/preview): rotate
		 *     the mesh coords if mesh_rotation[i] is set, and flag the
		 *     "big main hull" sweep for ship_idx == 98.
		 *   - mission.train_craft_type == 0 (live combat): rotated meshes
		 *     are NOT collided rotated. If the rotated mesh belongs to
		 *     the player's own ship as the laser shooter, skip it
		 *     entirely (avoids self-hit on the spinning Y-wing
		 *     centerpiece etc.). Otherwise fall through and collide with
		 *     unrotated coords. */
		rot = craftptr->mesh_rotation[i];
		if (mission.train_craft_type) {
			if (rot) {
				const uint16_t angle = (uint16_t)(rot << 8);
				const int16_t rot_sin = trig2_getsignedsin(angle);
				const int16_t rot_cos = trig2_getsignedcos((int16_t)angle);
				int32_t temp;

				temp = base_up_cur * -rot_sin + base_side_cur * rot_cos;
				if (temp >= 0x40000000)
					temp = 1073676288;
				if (temp <= -0x40000000)
					temp = -1073676288;
				side_cur_r = (temp >> 15);
				temp = base_up_cur * rot_cos + base_side_cur * rot_sin;
				if (temp >= 0x40000000)
					temp = 1073676288;
				if (temp <= -0x40000000)
					temp = -1073676288;
				up_cur_r = (temp >> 15);
				temp = base_up_prev * -rot_sin + base_side_prev * rot_cos;
				if (temp >= 0x40000000)
					temp = 1073676288;
				if (temp <= -0x40000000)
					temp = -1073676288;
				side_prev_r = (temp >> 15);
				temp = base_up_prev * rot_cos + base_side_prev * rot_sin;
				if (temp >= 0x40000000)
					temp = 1073676288;
				if (temp <= -0x40000000)
					temp = -1073676288;
				up_prev_r = (temp >> 15);
			}
			if (mesh->mesh_type == 1 /* MESH_MainHull */ && ship_idx == 98) {
				is_main_hull_ship98 = 1;
			}
		} else if (rot && shooter_obj_idx == pstate.object_idx) {
			continue;
		}

		/* Mesh AABB reject. The binary reads the bbox via unaligned-dword
		 * loads at (field_addr - 2) >> 16; those resolve to the direct
		 * field_{min,max}_{side,fwd,up} values (see top-of-file note). */
		bbox_min_side = mesh->bbox_min_side;
		bbox_min_fwd = mesh->bbox_min_fwd;
		bbox_max_fwd = mesh->bbox_max_fwd;
		bbox_min_up = mesh->bbox_min_up;
		bbox_max_up = mesh->bbox_max_up;
		bbox_max_side = mesh->bbox_max_side;

		in_bbox = (bbox_min_side <= side_cur_r || bbox_min_side <= side_prev_r) &&
				  (bbox_min_fwd <= scaled_fwd_cur || bbox_min_fwd <= scaled_fwd_prev) &&
				  (up_cur_r >= bbox_min_up || up_prev_r >= bbox_min_up) &&
				  (bbox_max_side >= side_cur_r || bbox_max_side >= side_prev_r) &&
				  (scaled_fwd_cur <= bbox_max_fwd || bbox_max_fwd >= scaled_fwd_prev) &&
				  (up_cur_r <= bbox_max_up || up_prev_r <= bbox_max_up);
		if (!in_bbox)
			continue;

		/* Walk the mesh's LOD dispatch chain. Each record is 6 bytes
		 * (int32 distance + u16 offset). The "budget" byte at
		 * polygon_header[4] (four bytes into the polygon block this LOD
		 * points at) must drop below 0x10 (or 0x18 for ship_idx == 28,
		 * the Super Star Destroyer) for this LOD to be selected;
		 * otherwise advance 6 bytes to the next record. The chain
		 * terminates with distance = 0x7FFFFFFF. */
		lod_bytes = (const uint8_t*)mesh + mesh->render_offset;

		memcpy(&distance, lod_bytes, sizeof distance);
		if (distance != 0x7FFFFFFF) {
			for (;;) {
				uint16_t offset;
				uint8_t budget;

				memcpy(&offset, lod_bytes + 4, sizeof offset);
				budget = lod_bytes[offset + 4];
				if (budget < 0x10u)
					break;
				if (ship_idx == 28 && budget < 0x18u)
					break;
				/* Species 66 always uses the first LOD. */
				if (ship_idx == 66)
					break;

				lod_bytes += 6;
				memcpy(&distance, lod_bytes, sizeof distance);
				if (distance == 0x7FFFFFFF)
					break;
			}
		}

		/* polygon data starts at (current LOD record) + offset */

		memcpy(&final_offset, lod_bytes + 4, sizeof final_offset);
		poly_hdr = lod_bytes + final_offset;

		hit_frac =
			(int32_t)collide_checkhitpolygons(poly_hdr, side_cur_r, scaled_fwd_cur, up_cur_r, side_prev_r,
											  scaled_fwd_prev, up_prev_r, is_main_hull_ship98);

		if (hit_frac && hit_frac < best_frac) {
			collidexoff = math2_mul_q15((laserx - laserxold), hit_frac);
			collideyoff = math2_mul_q15((lasery - laseryold), hit_frac);
			collidezoff = math2_mul_q15((laserz - laserzold), hit_frac);
			best_frac = hit_frac;
			hit_component = (uint16_t)(i + 1);
		}
	}

	return hit_component;
}

/* ---------------------------------------------------------------- *
 * STARSHIP_damagecomponent -- apply hit damage to one mesh (@0x4FED0)
 * ---------------------------------------------------------------- */

// FUNCTION: TIE95 0x534E4
uint16_t starship_damagecomponent(uint16_t obj_idx_in, int16_t component_plus1, uint16_t damage) {
	uint8_t* model_data;
	ShipModelData* model;
	ShipModelMesh* meshes_base;
	uint16_t component_idx;
	uint8_t hp_current;
	uint16_t damage_units;
	uint8_t hp_remaining;
	ShipModelMesh* mesh;
	uint16_t new_obj;
	FlightObject* ember;
	FlightObject* parent;
	int16_t center_side_half;
	int16_t center_up_half;
	int16_t center_fwd_half;
	int32_t ds;

	if (TIE_FLIGHT_TIE98)
		return starship_damagecomponent_tie98(obj_idx_in, component_plus1, damage);

	/* Match the binary's trust model: craftptr is caller-provided (set by
	 * collide_damagecraft to objects[obj_idx_in].craft_ptr before dispatch)
	 * and objectblockptr->num_meshes is read from the global below, but the
	 * ship MODEL itself is re-resolved here from species_table[].model_handle
	 * so we pick up the correct mesh table even if componentblockptr /
	 * objectblockptr happen to be parked on a different ship. This mirrors
	 * the binary's XMEMHDL_Lock_Handle / model_base walk at 0x4FF26..0x4FF6B. */
	/* Skip the 2-byte file-size prefix — matches retail's v48=a1+2. */
	model_data = (uint8_t*)xmemhdl_Lock_Handle(species_table[objects[obj_idx_in].ship_idx].model_handle);
	model = (ShipModelData*)(model_data + 2);
	xmemhdl_Unlock_Handle(species_table[objects[obj_idx_in].ship_idx].model_handle);
	meshes_base = (ShipModelMesh*)&model->lod_records[model->num_lods];

	component_idx = (uint16_t)(component_plus1 - 1);

	/* HP gate: 0 = already dead, 255 = indestructible. Either way pass the
	 * damage straight through. */
	hp_current = craftptr->mesh_component_hp[component_idx];
	if (hp_current == 0 || hp_current == 255)
		return damage;

	/* Damage scaled to 1/16-HP units, clamped up to 1. */
	damage_units = (uint16_t)(damage >> 4);
	if (damage_units == 0)
		damage_units = 1;

	/* Partial-absorb path: still alive after the hit. */
	hp_remaining = craftptr->mesh_component_hp[component_idx];
	if (hp_remaining > damage_units) {
		craftptr->mesh_component_hp[component_idx] = (uint8_t)(hp_remaining - damage_units);
		return 0;
	}

	/* Killed. Compute overflow damage to propagate. */
	craftptr->mesh_component_hp[component_idx] = 0;
	damage = (uint16_t)(16 * (damage_units - hp_remaining));

	/* This mesh must be explodable to trigger the visual spawn. */
	mesh = &meshes_base[component_idx];
	if ((mesh->flags & STARSHIP_MESH_FLAG_EXPLODABLE) == 0)
		return damage;

	/* Flag mesh as destroyed. */
	craftptr->mesh_state[component_idx] = MESH_STATE_HIDDEN;

	/* If radar_target1 was locked on the just-destroyed part of the same
	 * obj that is currently the target, bump it past dead / invalid meshes. */
	if (obj_idx_in == pstate.target_obj_idx && component_idx == pstate.radar_target1) {
		do {
			const unsigned int nm = objectblockptr->num_meshes;
			if ((unsigned int)++pstate.radar_target1 >= nm)
				pstate.radar_target1 = 0;
		} while ((craftptr->mesh_state[pstate.radar_target1] != MESH_STATE_VISIBLE ||
				  !user_validcomponent(pstate.radar_target1)) &&
				 component_idx != pstate.radar_target1);
	}

	/* Training mode bonuses. */
	if (mission.train_craft_type) {
		++mission.train_targets;
		mission.mission_score += 50;
		if (craftptr->mesh_rotation[component_idx])
			mission.mission_score += 50; /* rotating turret = extra 50 */
		timeleft.second = (uint8_t)(timeleft.second + 2);
		if (timeleft.second >= 60) {
			timeleft.second = (uint8_t)(timeleft.second - 60);
			++timeleft.minute;
		}
	}

	/* Allocate ember/debris slot (genus 13). */
	new_obj = create_findslot(13);
	if (new_obj == 0xFFFF)
		return damage;

	ember = &objects[new_obj];
	parent = &objects[obj_idx_in];
	ember->world_x = parent->world_x;
	ember->world_y = parent->world_y;
	ember->world_z = parent->world_z;

	/* The binary reads the mesh center via the Watcom >>17 (=int16 >> 1)
	 * pattern at (mesh_rec + {14,16,18}), which resolves to center_side,
	 * center_fwd, center_up each divided by 2. */
	center_side_half = (int16_t)(mesh->center_side >> 1);
	center_up_half = (int16_t)(mesh->center_up >> 1);
	center_fwd_half = (int16_t)(mesh->center_fwd >> 1);

	/* Apply per-mesh rotation in training mode (2D rotation in the side/up
	 * plane by angle = -256 * mesh_rotation). */
	if (mission.train_craft_type) {
		const uint8_t rot_raw = craftptr->mesh_rotation[component_idx];
		if (rot_raw) {
			const int16_t rot_angle = (int16_t)(-256 * rot_raw);
			const int16_t rot_sin = trig2_getsignedsin((uint16_t)rot_angle);
			const int16_t rot_cos = trig2_getsignedcos(rot_angle);

			int32_t rot_a = (int32_t)center_up_half * -rot_sin + (int32_t)center_side_half * rot_cos;
			int32_t rot_b = (int32_t)center_up_half * rot_cos + (int32_t)center_side_half * rot_sin;

			if (rot_a >= 0x40000000)
				rot_a = 1073676288;
			if (rot_a <= -0x40000000)
				rot_a = -1073676288;
			center_side_half = (int16_t)(rot_a >> 15);
			if (rot_b >= 0x40000000)
				rot_b = 1073676288;
			if (rot_b <= -0x40000000)
				rot_b = -1073676288;
			center_up_half = (int16_t)(rot_b >> 15);
		}
	}

	/* Transform into world frame -- writes rotatedx/y/z. */
	pai_calcrotatedpoint(parent, center_side_half, center_up_half, (int16_t)(-center_fwd_half));

	/* model_scale_shift scaling. The binary reads model_scale_shift via HIBYTE of a dword
	 * straddling the local objblock's speed_default + 3 (= objblock[30]);
	 * we read it from the locally-resolved model rather than the global
	 * objectblockptr to match that "this ship's model, not whatever is
	 * locked globally" behavior. */
	if (model->model_scale_shift) {
		const int shift = model->model_scale_shift;
		/* Shift via uint32_t — binary emits `shl reg, cl`, which
		 * is sign-agnostic; signed left shift on a negative int
		 * is UB in C. */
		rotatedx = (int32_t)((uint32_t)rotatedx << shift);
		rotatedy = (int32_t)((uint32_t)rotatedy << shift);
		rotatedz = (int32_t)((uint32_t)rotatedz << shift);
	}

	ember->world_x += rotatedx;
	ember->world_y += rotatedy;
	ember->world_z += rotatedz;

	ember->craft_ptr = NULL;
	ember->genus = GENUS_EXPLOSION;
	/* Retail damagecomponent always picks the 129 ember (fixed; no
	 * randomization). The 127/128 alternation belongs to the smaller
	 * createstarshipexplo / makestarshipcompexplo embers. */
	ember->ship_idx = 129;
	ember->category = 5;
	ember->anim_frame = 2;
	ember->age_ticks = 0;
	ember->death_timer = 0;
	ember->current_speed = parent->current_speed;
	ember->pitch = parent->pitch;
	ember->heading = parent->heading;
	ember->roll = 0;
	ember->orient_dirty = 1;
	ember->move_dirty = 1;

	fsfx_triggersfx((uint16_t)(19 + (math2_getrandom() & 3)), new_obj);

	/* damage_state derived from the per-mesh explosion_scale u16 (mesh+0x0A). */
	ds = (int32_t)mesh->explosion_scale >> (9 - model->model_scale_shift);
	if (ds > 255)
		ds = -1; /* binary: LOBYTE(ds) = -1 <=> 0xFF */
	ember->damage_state = (uint8_t)ds;

	return damage;
}

/* ---------------------------------------------------------------- *
 * STARSHIP_createstarshipexplo -- whole-ship or sparking (@0x50428)
 * ---------------------------------------------------------------- */

// FUNCTION: TIE98 0x4875F0
// STARSHIP_makestarshipcompexplo
static uint16_t starship_makestarshipcompexplo_tie98(FlightObject* craft, uint16_t component_idx,
													 uint32_t effect_size, int use_random_vertex) {
	const uint16_t model_type = craft->ship_idx;
	int x;
	int y;
	int z;
	int random_vertex_index = 0;
	uint16_t new_obj;
	FlightObject* ember;

	if (use_random_vertex) {
		random_vertex_index =
			(uint16_t)math2_getrandom() % modelmesh_getvertexcount(model_type, component_idx);
		x = modelmesh_getvertexx(model_type, component_idx, random_vertex_index);
		y = modelmesh_getvertexy(model_type, component_idx, random_vertex_index);
		z = modelmesh_getvertexz(model_type, component_idx, random_vertex_index);
	} else {
		x = modelmesh_getcenterx(model_type, component_idx);
		y = modelmesh_getcentery(model_type, component_idx);
		z = modelmesh_getcenterz(model_type, component_idx);
	}
	pai_calcrotatedpoint(craft, (int16_t)x, (int16_t)z, (int16_t)-y);

	new_obj = create_findslot(GENUS_EXPLOSION);
	if (new_obj == 0xFFFF)
		return new_obj;

	ember = &objects[new_obj];
	ember->world_x = rotatedx + craft->world_x;
	ember->world_y = rotatedy + craft->world_y;
	ember->world_z = rotatedz + craft->world_z;
	ember->craft_ptr = NULL;
	ember->ship_idx = random_vertex_index ? (uint8_t)((math2_getrandom() & 1) + 127) : 129;
	ember->genus = GENUS_EXPLOSION;
	ember->category = 5;
	ember->anim_frame = 2;
	ember->age_ticks = 0;
	ember->death_timer = 0;
	ember->damage_state = (uint8_t)(effect_size >> 6);
	ember->current_speed = 0;
	ember->pitch = 0;
	ember->heading = 0;
	ember->roll = 0;
	ember->orient_dirty = 1;
	ember->move_dirty = 1;
	return new_obj;
}

// FUNCTION: TIE98 0x487440
// STARSHIP_createstarshipexplo
static void starship_createstarshipexplo_tie98(uint16_t obj_idx, int16_t full_ship) {
	FlightObject* craft;
	uint16_t model_type;
	uint16_t num_main_hulls;
	int mesh_count;
	int i;

	uint8_t main_hull_slots[16];

	if ((uint16_t)math2_getrandom() >= starshipexplodetail && !full_ship)
		return;

	craft = &objects[obj_idx];
	craftptr = craft->craft_ptr;
	if (craft->orient_dirty)
		fview_newcalcrotate(craft->roll, craft->pitch, craft->heading, 0, craft);

	model_type = craft->ship_idx;

	num_main_hulls = 0;
	mesh_count = modelmesh_getcount(model_type);
	for (i = 0; i < mesh_count; ++i) {
		if (modelmesh_gettype(model_type, i) == TIE_MESH_MAIN_HULL)
			main_hull_slots[num_main_hulls++] = (uint8_t)i;
		if (num_main_hulls == 16)
			break;
	}

	if (full_ship) {
		objects[genus_table[13].start].ship_idx = 0;
		starship_makestarshipcompexplo_tie98(craft, main_hull_slots[0],
											 (uint32_t)modelbounds_getmaxextent(model_type), 0);
		fsfx_triggersfx(18, obj_idx);
	} else {
		const uint16_t component_idx = main_hull_slots[(uint16_t)math2_getrandom() % num_main_hulls];
		if (craftptr->mesh_component_hp[component_idx]) {
			const uint16_t new_obj = starship_makestarshipcompexplo_tie98(
				craft, component_idx, (uint32_t)modelbounds_getmaxextent(model_type) >> 6, 1);
			if (new_obj != 0xFFFF)
				fsfx_triggersfx((uint16_t)((math2_getrandom() & 3) + 19), new_obj);
		}
	}
}

// FUNCTION: TIE95 0x53A3C
void starship_createstarshipexplo(uint16_t obj_idx_in, int16_t full_ship) {
	uint16_t roll;
	FlightObject* craft;
	uint16_t num_main_hull;
	ShipModelMesh* mesh;
	unsigned int num_meshes;
	unsigned int i;

	uint8_t main_hull_slots[16];

	if (TIE_FLIGHT_TIE98) {
		starship_createstarshipexplo_tie98(obj_idx_in, full_ship);
		return;
	}

	/* Two gate conditions: full explosion forces through, otherwise the
	 * RNG must be below starshipexplodetail (0x1000..0x7FFF depending on
	 * the chosen detail level). */
	roll = (uint16_t)math2_getrandom();
	if (roll >= starshipexplodetail && !full_ship)
		return;

	craft = &objects[obj_idx_in];
	craftptr = craft->craft_ptr;

	if (craft->orient_dirty) {
		fview_newcalcrotate(craft->roll, craft->pitch, craft->heading, 0, craft);
	}

	draw_lockshipfileptrs(craft->ship_idx);

	/* Scan meshes for MESH_MainHull entries; record up to 16 indices. */

	num_main_hull = 0;
	mesh = componentblockptr;
	num_meshes = objectblockptr->num_meshes;

	for (i = 0; i < num_meshes; ++i, ++mesh) {
		if (mesh->mesh_type == 1 /* MESH_MainHull */) {
			main_hull_slots[num_main_hull++] = (uint8_t)i;
			if (num_main_hull == 16)
				break;
		}
	}

	if (full_ship) {
		/* Retail only zeroes the head slot (the loop iterates exactly
		 * once). The remaining big-explosion slots get reseeded by the
		 * per-mesh explosions below. */
		uint16_t k;

		objects[genus_table[13].start].ship_idx = 0;

		for (k = 0; k < num_main_hull; ++k) {
			starship_makestarshipcompexplo(craft, main_hull_slots[k],
										   componentblockptr[main_hull_slots[k]].explosion_scale, 0);
		}
		fsfx_triggersfx(0x12u, obj_idx_in);
	} else if (num_main_hull) {
		const uint16_t pick = (uint16_t)math2_getrandom() % num_main_hull;
		const uint8_t idx = main_hull_slots[pick];
		if (craftptr->mesh_component_hp[idx]) {
			const uint16_t size = (uint16_t)(species_table[craft->ship_idx].bound_hwidth >> 6);
			const uint16_t new_obj = starship_makestarshipcompexplo(craft, idx, size, 1);
			if (new_obj != 0xFFFF) {
				fsfx_triggersfx((uint16_t)(19 + (math2_getrandom() & 3)), new_obj);
			}
		}
	}
}

/* ---------------------------------------------------------------- *
 * STARSHIP_makestarshipcompexplo -- one component explosion (@0x50640)
 * ---------------------------------------------------------------- */

// FUNCTION: TIE95 0x53BFC
uint16_t starship_makestarshipcompexplo(FlightObject* craft, uint16_t component_idx, uint16_t size,
										int16_t use_bsp_random) {
	ShipModelMesh* mesh;
	int16_t c_side, c_fwd, c_up;
	uint16_t new_obj;
	int model_scale_shift;
	FlightObject* ember;

	if (TIE_FLIGHT_TIE98)
		return starship_makestarshipcompexplo_tie98(craft, component_idx, size, use_bsp_random);

	mesh = &componentblockptr[component_idx];

	if (use_bsp_random) {
		/* Walk into the mesh's first-LOD polygon block, pick a random
		 * vertex, and resolve each of its 3 int16 coords via the back-ref
		 * chain.
		 *
		 * bsp_hdr = mesh + render_offset + lod[0].offset
		 *   (the binary reaches lod[0].offset via an unaligned int read of
		 *    `&mesh->flags + render_offset` shifted >> 16 because Watcom
		 *    uses a dword load whose upper half is the u16 offset field.)
		 * num_vertices = byte at bsp_hdr+2
		 * vertex_array = bsp_hdr + 17 + byte_at(bsp_hdr+4)
		 * pick vertex at 6 * (rand % num_vertices) */
		const uint8_t* mesh_bytes = (const uint8_t*)mesh;
		const ShipMeshLOD* lod0 = (const ShipMeshLOD*)(mesh_bytes + mesh->render_offset);
		const uint8_t* bsp_hdr = mesh_bytes + mesh->render_offset + lod0->offset;
		const unsigned int num_vertices = bsp_hdr[2];

		const unsigned int pick = (unsigned int)((uint16_t)math2_getrandom() % num_vertices);
		const uint8_t* vertex_base = bsp_hdr + 17 + bsp_hdr[4] + 6 * pick;
		const uint8_t* coord;

		coord = vertex_base;
		while (coord[1] == 0x7F)
			coord -= 3 * (int)coord[0];
		memcpy(&c_side, coord, sizeof c_side);
		coord = vertex_base + 2;
		while (coord[1] == 0x7F)
			coord -= 3 * (int)coord[0];
		memcpy(&c_fwd, coord, sizeof c_fwd);
		coord = vertex_base + 4;
		while (coord[1] == 0x7F)
			coord -= 3 * (int)coord[0];
		memcpy(&c_up, coord, sizeof c_up);

		pai_calcrotatedpoint(craft, c_side, c_up, (int16_t)(-c_fwd));
	} else {
		pai_calcrotatedpoint(craft, mesh->center_side, mesh->center_up, (int16_t)(-mesh->center_fwd));
	}

	new_obj = create_findslot(13);
	if (new_obj == 0xFFFF)
		return new_obj;

	/* Scale the rotated offset by 2^(model_scale_shift - 1): left-shift when
	 * model_scale_shift >= 2, half when model_scale_shift == 0, identity when == 1. */
	model_scale_shift = objectblockptr->model_scale_shift;
	if (model_scale_shift <= 1) {
		if (model_scale_shift == 0) {
			rotatedx >>= 1;
			rotatedy >>= 1;
			rotatedz >>= 1;
		}
		/* model_scale_shift == 1 -> no scaling */
	} else {
		const int shift = model_scale_shift - 1;
		rotatedx = (int32_t)((uint32_t)rotatedx << shift);
		rotatedy = (int32_t)((uint32_t)rotatedy << shift);
		rotatedz = (int32_t)((uint32_t)rotatedz << shift);
	}

	ember = &objects[new_obj];
	ember->world_x = rotatedx + craft->world_x;
	ember->world_y = rotatedy + craft->world_y;
	ember->world_z = rotatedz + craft->world_z;
	ember->craft_ptr = NULL;

	ember->genus = GENUS_EXPLOSION;
	ember->ship_idx = (uint8_t)(127 + (math2_getrandom() & 1));
	ember->category = 5;
	ember->anim_frame = 2;
	ember->age_ticks = 0;
	ember->death_timer = 0;
	ember->roll = 0;
	/* damage_state = size >> (6 - model_scale_shift): small/tight explosion on
	 * heavily-LOD'd ships, big blast on non-LOD ships. */
	ember->damage_state = (uint8_t)((int32_t)size >> (6 - model_scale_shift));
	ember->current_speed = 0;
	ember->pitch = 0;
	ember->heading = 0;
	ember->orient_dirty = 1;
	ember->move_dirty = 1;

	return new_obj;
}

/* ---------------------------------------------------------------- *
 * STARSHIP_firelasergunner -- turret fire control (@0x508C8)
 * ---------------------------------------------------------------- */

/*
 * Hex-Rays labelled the projectile-parameter reads in this function as
 * word_D4C16[idx] / word_D4BE6[idx] / word_D4C46[idx] / dword_D4C74[idx] --
 * synthesized table bases, not real source symbols. The effective addresses
 * fall inside the projectile parameter tables owned by laser.c, offset by
 * +137 entries:
 *
 *   word_D4BE6[N]            -> projectileweight  [N - 137]   (u16)
 *   word_D4C16[N]            -> projectilevelocity[N - 137]   (u16, current_speed)
 *   word_D4C46[N]            -> projectilelife    [N - 137]   (u16, life factor)
 *   SHIWORD(dword_D4C74[N])  -> launch offset     [N - 137]   (i16, velocity push)
 *
 * Valid N for gunner-fired shots is 138..146 (PROJ_SHIP_* constants below),
 * so the underlying type index is 1..9. Index 0 is unused by this path and
 * 18..23 are padding zeros in the tables. */
enum {
	PROJ_TYPE_SHIFT = 137,
};

/* Warhead slot metadata lives in laser.c. We access it via the WarheadRecord
 * struct from laser.h. */

// FUNCTION: TIE95 0x53EAC
void starship_firelasergunner(uint16_t craft_obj_idx, uint16_t weapon_slot_idx, uint16_t target_ref) {
	uint16_t species_idx;
	uint8_t mesh_idx;
	uint8_t link_byte;
	FlightObject* craft;
	uint8_t charge_now;
	uint8_t divisor;
#ifdef TIE_MODERN
	TieTurretTimingState* cooldown;
#endif
	uint32_t numerator;
	uint32_t decrement;
	int32_t craft_wx;
	int32_t craft_wy;
	int32_t craft_wz;
	ShipModelMesh* mesh;
	int32_t gun_wx;
	int32_t gun_wz;
	int32_t gun_wy;
	int32_t target_wx;
	int32_t target_wy;
	int32_t target_wz;
	int16_t ammo;
	int32_t aim_wx;
	int32_t aim_wy;
	int32_t aim_wz;
	int16_t heading;
	int16_t pitch;
	uint16_t new_obj;
	FlightObject* laser;
	int is_turbo;
	unsigned int b;
	uint16_t projectile_idx;
	uint16_t proj_type;
	int32_t push;
	unsigned int warhead_slot;

	if (craftptr->status_flags == 0)
		return;

	species_idx = craftptr->species_idx;
	mesh_idx = spec_data[species_idx].hp[weapon_slot_idx].component;
	link_byte = (uint8_t)spec_data[species_idx].hp[weapon_slot_idx].link;

	if (!craftptr->mesh_component_hp[mesh_idx])
		return;

	craft = &objects[craft_obj_idx];

	/* Charge / cooldown tick. Rate tiered on the craft's skill_value:
	 *   >= 0xAAAA -> frameticks / 2  (fast)
	 *   [0x5555, 0xAAAA) -> frameticks >> 2  (medium)
	 *   < 0x5555  -> frameticks / 6  (slow) */
	charge_now = (uint8_t)(craftptr->weapon_slots[weapon_slot_idx].charge & 0x7F);
	divisor = craftptr->skill_value >= 0xAAAAu ? 2u : craftptr->skill_value >= 0x5555u ? 4u : 6u;
#ifdef TIE_MODERN
	cooldown = TieFlightTimingState_Turret(craft_obj_idx, weapon_slot_idx, craft->idnumber, divisor);
	/* Retain sub-unit cooldown time so a four-tick frame can advance
	 * the six-tick tier instead of repeatedly truncating to zero. */
	numerator = frameticks + cooldown->remainder;
	decrement = numerator / divisor;
	cooldown->remainder = (uint8_t)(numerator % divisor);
#else
	decrement = frameticks / divisor;
#endif

	if (charge_now > decrement) {
		craftptr->weapon_slots[weapon_slot_idx].charge -= (uint8_t)decrement;
		return;
	}

	/* Ready to fire: reset cooldown (preserving bit 7). */
	craftptr->weapon_slots[weapon_slot_idx].charge =
		(uint8_t)((craftptr->weapon_slots[weapon_slot_idx].charge & 0x80) | 0x3B);
#ifdef TIE_MODERN
	cooldown->remainder = 0;
#endif

	craft_wx = craft->world_x;
	craft_wy = craft->world_y;
	craft_wz = craft->world_z;

	mesh = NULL;
	if (!TIE_FLIGHT_TIE98) {
		draw_lockshipfileptrs(craft->ship_idx);
		mesh = &componentblockptr[mesh_idx];
	}

	if (TIE_FLIGHT_TIE98) {
		/* The OPT rotation helper uses (X, -Y, Z); PAI uses
		 * (side, up, forward). */
		int point_x;
		int point_forward;
		int point_up;
		if (link_byte != 0xFF && _date.subsec < 118) {
			int type;
			modelmesh_gethardpoint(craft->ship_idx, mesh_idx, link_byte, &type, &point_x, &point_forward,
								   &point_up);
			(void)type;
			if (craft->ship_idx == 53) {
				point_x /= 2;
				point_forward /= 2;
				point_up /= 2;
			}
		} else {
			const HardpointPos* hp = &spec_data[species_idx].hp[weapon_slot_idx];
			point_x = hp->x;
			point_forward = hp->z;
			point_up = hp->y;
		}

		if (modelmesh_gettype(craft->ship_idx, mesh_idx) == TIE_MESH_ROTARY_GUN_TURRET) {
			if (craft->ship_idx == 53) {
				point_x *= 2;
				point_forward *= 2;
				point_up *= 2;
			}
			modelmesh_applyanimatedmeshrotationtopoint((int16_t)(craftptr->mesh_rotation[mesh_idx] << 8),
													   craft->ship_idx, mesh_idx, point_x, point_forward,
													   point_up, &point_x, &point_forward, &point_up);
			if (craft->ship_idx == 53) {
				point_x /= 2;
				point_forward /= 2;
				point_up /= 2;
			}
		}
		pai_calcrotatedpoint(craft, point_x, point_up, point_forward);
	} else {
		int16_t hp_side;
		int16_t hp_fwd_neg;
		int16_t hp_up;
		if (link_byte == 0xFF || _date.subsec >= 118) {
			const HardpointPos* hp = &spec_data[species_idx].hp[weapon_slot_idx];
			hp_side = hp->x;
			hp_fwd_neg = hp->z;
			hp_up = hp->y;
		} else {
			/* BSP hardpoint walk.
			 *
			 * The Watcom expression is:
			 *   &mesh->center_side + 6*link_byte + render_offset + X + Y + 1
			 * where
			 *   X = u16 at (mesh + 4 + render_offset)
			 *       (polygon header start, 4 bytes into the LOD record at
			 *        mesh + render_offset)
			 *   Y = u8 at (mesh + 4 + render_offset + X)
			 *       (skip byte one past the polygon header)
			 *
			 * Translated literally so the offsets match the binary; the +0x10
			 * (&mesh->center_side) is just the compiler's chosen anchor. The
			 * mesh+4 base is byte arithmetic only -- it intentionally walks
			 * past the mesh's named fields into the appended LOD/poly data. */
			const uint8_t* mesh_bytes = (const uint8_t*)mesh;
			uint16_t poly_off;
			uint8_t skip_byte;
			const uint8_t* base;
			const uint8_t* coord;
			int16_t coord_value;

			memcpy(&poly_off, mesh_bytes + 4 + mesh->render_offset, sizeof poly_off);
			skip_byte = mesh_bytes[4 + mesh->render_offset + poly_off];
			base = mesh_bytes + 0x10 + mesh->render_offset + poly_off + skip_byte + 1 + 6 * link_byte;

			coord = base;
			while (coord[1] == 0x7F)
				coord -= 3 * (int)coord[0];
			memcpy(&coord_value, coord, sizeof coord_value);
			hp_side = (int16_t)(coord_value >> 1);
			coord = base + 2;
			while (coord[1] == 0x7F)
				coord -= 3 * (int)coord[0];
			memcpy(&coord_value, coord, sizeof coord_value);
			hp_fwd_neg = (int16_t)(-(coord_value >> 1));
			coord = base + 4;
			while (coord[1] == 0x7F)
				coord -= 3 * (int)coord[0];
			memcpy(&coord_value, coord, sizeof coord_value);
			hp_up = (int16_t)(coord_value >> 1);
		}

		if (mesh->mesh_type == TIE_MESH_ROTARY_GUN_TURRET) {
			fview_comprotatepoint((int16_t)(craftptr->mesh_rotation[mesh_idx] << 8), mesh, hp_side,
								  (int16_t)(-hp_fwd_neg), hp_up);
			pai_calcrotatedpoint(craft, (int16_t)rotatedx, (int16_t)rotatedz, (int16_t)(-rotatedy));
		} else {
			pai_calcrotatedpoint(craft, hp_side, hp_up, hp_fwd_neg);
		}
	}

	if (!TIE_FLIGHT_TIE98 && objectblockptr->model_scale_shift) {
		const int shift = objectblockptr->model_scale_shift;
		rotatedx = (int32_t)((uint32_t)rotatedx << shift);
		rotatedy = (int32_t)((uint32_t)rotatedy << shift);
		rotatedz = (int32_t)((uint32_t)rotatedz << shift);
	}

	gun_wx = rotatedx + craft_wx;
	gun_wz = rotatedz + craft_wz;
	gun_wy = rotatedy + craft_wy;
	create_getworldposition(target_ref, 0);
	target_wx = worldlocx;
	target_wy = worldlocy;
	target_wz = worldlocz;

	/* Range gate: ~131072 world units. */
	{
		const int32_t tdx = target_wx - gun_wx;
		const int32_t tdy = target_wy - gun_wy;
		const int32_t tdz = target_wz - gun_wz;
		if ((uint32_t)collide_roughdistance3d(tdx, tdy, tdz) > 0x20000u)
			return;
	}

	/* LOS test: temporarily repurpose the laser / laser*old globals to
	 * represent the (gun -> target) segment and ask starship_checkstarshiphit
	 * whether our own hull blocks the shot. */
	laserx = target_wx;
	laserxold = gun_wx;
	lasery = target_wy;
	laserz = target_wz;
	laseryold = gun_wy;
	laserzold = gun_wz;
	if (starship_checkstarshiphit(craft_obj_idx, craft_obj_idx))
		return;

	ammo = craftptr->weapon_slots[weapon_slot_idx].ammo;

	/* Lead moving targets from their FlightObject history. Static targets
	 * use the position returned by create_getworldposition directly. */
	aim_wx = target_wx;
	aim_wy = target_wy;
	aim_wz = target_wz;
	if (target_ref < OBJ_REF_STATIC_BASE) {
		int32_t lead_scaled;
		uint16_t lead_jitter;
		uint16_t lead_fraction;

		trig2_ctop(target_wx - gun_wx, target_wy - gun_wy, target_wz - gun_wz);
		lead_scaled = (int32_t)framerate * trig2_polardistance;
		trig2_polardistance = (ammo != 0) ? (lead_scaled >> 15) : (lead_scaled >> 14);

		lead_jitter = (uint16_t)((math2_getrandom() & 3) + (int16_t)trig2_polardistance - 1);
		if (objects[target_ref].current_speed == 0)
			lead_jitter = 0;
		lead_fraction = math2_fraction(lead_jitter, craftptr->skill_value);
		aim_wx += (int32_t)lead_fraction * (objects[target_ref].world_x - objects[target_ref].world_x_prev);
		aim_wy += (int32_t)lead_fraction * (objects[target_ref].world_y - objects[target_ref].world_y_prev);
		aim_wz += (int32_t)lead_fraction * (objects[target_ref].world_z - objects[target_ref].world_z_prev);
	}

	trig2_ctop(aim_wx - gun_wx, aim_wy - gun_wy, aim_wz - gun_wz);
	heading = trig2_xyangle;
	pitch = trig2_zangle;
	(void)math2_getrandom(); /* binary burns one RNG value for parity */

	new_obj = create_findslot(7);
	if (new_obj == 0xFFFF)
		return;

	laser = &objects[new_obj];
	laser->category = 1;
	laser->genus = GENUS_PROJECTILE_NPC;
	laser->side = craft->side;

	/* is_turbo: one of the species' laser banks fires type 145 or 146. */
	is_turbo = 0;
	for (b = 0; b < 2; ++b) {
		if (weapon_slot_idx >= spec_data[species_idx].laser_start[b] &&
			weapon_slot_idx <= spec_data[species_idx].laser_end[b]) {
			const uint8_t t = spec_data[species_idx].laser_type[b];
			if (t == 145 || t == 146)
				is_turbo = 1;
		}
	}

	if (ammo) {
		projectile_idx = (uint16_t)(PROJ_SHIP_AMMO_LASER + (is_turbo ? 1 : 0));
	} else if (craft->side && craft->side != 2) {
		projectile_idx = is_turbo ? PROJ_SHIP_EMPIRE_TURBO : PROJ_SHIP_EMPIRE_LASER;
	} else {
		projectile_idx = is_turbo ? PROJ_SHIP_REBEL_TURBO : PROJ_SHIP_REBEL_LASER;
	}
	proj_type = (uint16_t)(projectile_idx - PROJ_TYPE_SHIFT);
	laser->ship_idx = (uint8_t)projectile_idx;
	laser->age_ticks = 1;
	laser->self_idx = (int16_t)craft_obj_idx;
	laser->ship_type_override = craft->ship_idx;
	laser->pitch = pitch;
	laser->heading = heading;
	laser->current_speed = (int16_t)projectilevelocity[proj_type];
	laser->collision_radius = (int16_t)projectileweight[proj_type];
	laser->roll = 0;
	laser->orient_dirty = 1;
	laser->move_dirty = 1;
	laser->death_timer = (int16_t)(708 * projectilelife[proj_type]);

	fview_calcrotatemove(pitch, heading, laser);

	/* Apply craft-velocity push to the gun position before placing the
	 * bolt -- gives a "fired from a moving platform" trail. The push
	 * factor is the HIWORD of dword_D4C74[projectile_idx], which aliases
	 * the projectile launch-offset table. */
	push = (int16_t)TIE_FLIGHT_EDITION(projectilelength, tie98_projectilelength)[proj_type];
	laser->world_x_prev = gun_wx;
	laser->world_y_prev = gun_wy;
	laser->world_z_prev = gun_wz;
	laser->world_x = ((push * craftmoveX) >> 15) + gun_wx;
	laser->world_y = ((push * craftmoveY) >> 15) + gun_wy;
	laser->world_z = ((push * craftmoveZ) >> 15) + gun_wz;

	/* Install the warhead-record target so MOVE/COLLIDE can look up who
	 * fired and what to track. Warhead slots start at FlightObject
	 * NUM_CRAFTS (32 retail / 28 demo). */
	warhead_slot = (unsigned int)(new_obj - NUM_CRAFTS);
	warheads[warhead_slot].homing_tier = 0;
	warheads[warhead_slot].target_obj = target_ref;
	laser->craft_ptr = (CraftData*)&warheads[warhead_slot];

	fsfx_triggerlasersfx(new_obj);
}
