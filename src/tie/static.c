#include "tie/static.h"
#include "tie_runtime/diagnostics/flight_trace.h"
#include "tie_runtime/flight_assets/model_access.h"
#ifdef TIE_MODERN
#include "tie_runtime/snapshot/snapshot.h"
#include "tie_runtime/snapshot/snapshot_internal.h"
#endif
#ifdef TIE_MODERN
#include "tie_runtime/timing/flight_timing.h"
#include "tie_runtime/timing/flight_timing_state.h"
#endif

#include "tie/anim.h"
#include "tie/collide.h"
#include "tie/collide_opt.h"
#include "tie/create.h"
#include "tie/draw.h"
#include "tie/drawpol.h"
#include "tie/edition.h"
#include "tie/fsfx.h"
#include "tie/fview.h"
#include "tie/laser.h"
#include "tie/logbuf2.h" /* pixelsdeep */
#include "tie/math2.h"
#include "tie/modelmesh.h"
#include "tie/pai.h" /* ai.live_target_only */
#include "tie/paifight.h"
#include "tie/render_scene_tie98.h"
#include "tie_runtime/runtime/wide_arithmetic.h"

#include "tie/shipext.h" /* EFGStruct / EAIStruct layout */
#include "tie/tie.h"
#include "tie/transfm2.h"
#include "tie/trig2.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* ============================================================================
 * static_drawstaticobject
 * ----------------------------------------------------------------------------
 * Per-frame render dispatcher for one static slot. Assumes the caller has
 * already stored the object's eye-space position in objecteyex/y/z and the
 * world-to-eye rotation rows in rotworldeye[A-B]{1,2,3}.
 *
 * species.draw_data == NULL  -> BSP mesh via draw_drawcomplexobject when
 *                               anim_frame == 0 (non-zero hides the mesh).
 * species.draw_data != NULL  -> per-frame opcode table driven by anim_frame:
 *                               0xFF.. = sentinel (skip), 0x80.. = billboard,
 *                               < 0x80.. = poly model (common path).
 * ========================================================================== */
// FUNCTION: TIE95 0x54710
// FUNCTION: TIE98 0x487F20
void static_drawstaticobject(uint16_t slot_idx) {
	uint16_t species = staticobjects[slot_idx].species;
	const AnimOp* frame_tab = (const AnimOp*)species_table[staticobjects[slot_idx].species].draw_data;
	uint16_t self_idx = (uint16_t)(slot_idx + OBJ_REF_STATIC_BASE);
	AnimOp frame_code;

	if (frame_tab == NULL) {
		parentobject = self_idx;
		/* Complex mesh: anim_frame != 0 hides the mesh. */
		if (TIE_FLIGHT_TIE98) {
			if (staticobjects[slot_idx].anim_frame != 0)
				return;
			draw_process_object_components_tie98(parentobject);
#ifdef TIE_MODERN
			FlightModel_Draw_Object(TieFlightAssets_StaticRenderObject(slot_idx));
#else
			FlightModel_Draw_Object((FlightObject*)&staticobjects[slot_idx]);
#endif
			return;
		}
		if (staticobjects[slot_idx].anim_frame == 0)
			draw_drawcomplexobject(self_idx);
		return;
	}

	frame_code = frame_tab[staticobjects[slot_idx].anim_frame];
	parentobject = self_idx;

	/* Skip header / jump / reset / delay / kill opcodes; only MESH and
	 * BITMAP opcodes produce output here. */
	if (frame_code >= 0xFF00)
		return;

	if (frame_code < 0x8000) {
		/* Polygon mesh: locate LOD for this eyez and emit via DRAWPOL. */
		int32_t eyez = objecteyez;
		int32_t eyey = objecteyey;
		int32_t eyex = objecteyex;
		if (TIE_FLIGHT_TIE98) {
			/* PORT: TIE98 passes the StaticObject itself as the draw object. */
#ifdef TIE_MODERN
			FlightModel_Draw_Object_Mesh(TieFlightAssets_StaticRenderObject(slot_idx), 0);
#else
			FlightModel_Draw_Object_Mesh((FlightObject*)&staticobjects[slot_idx], 0);
#endif
			return;
		}
		drawpol_drawpolyobject(
			draw_getdetailptr(
				draw_getcomponentptr(xmemhdl_Lock_Handle(species_table[species].model_handle), 0), eyez),
			eyex, eyey, eyez);
		xmemhdl_Unlock_Handle(species_table[species].model_handle);
		return;
	}

	{
		/* Billboard sprite. Reject if behind the camera. */
		int32_t abs_B3;
		int32_t abs_A3;
		int32_t axis_horz, axis_vert;
		int16_t billboard_angle;
		uint32_t sx_raw;
		int32_t sx_hi;
		uint32_t sy_raw;
		int32_t sy_hi;
		int32_t half;
		int32_t y_flipped;

		if (objecteyez < 0)
			return;

		/* Pick the world-to-eye row most orthogonal to the view
		 * direction; its XY projection defines the sprite's billboard
		 * rotation. */
		abs_A3 = rotworldeyeA3;
		abs_B3 = rotworldeyeB3;
		if (abs_A3 < 0)
			abs_A3 = -abs_A3;
		if (abs_B3 < 0)
			abs_B3 = -abs_B3;

		if (abs_A3 < abs_B3) {
			axis_horz = rotworldeyeA1;
			axis_vert = rotworldeyeA2;
		} else {
			axis_horz = rotworldeyeB1;
			axis_vert = rotworldeyeB2;
		}

		if (axis_horz < 0)
			billboard_angle = trig2_arctan(axis_vert, -axis_horz);
		else
			billboard_angle = -trig2_arctan(axis_vert, axis_horz);

		/* Project onto screen, require |high16| <= 1 on both axes
		 * (i.e. within one screen-width of the visible rect). */
		sx_raw = (uint32_t)transfm2_getscreenx(objecteyex, objecteyez);
		sx_hi = (int32_t)sx_raw >> 16;
		if (sx_hi > 0 || sx_hi < -1)
			return;

		sy_raw = (uint32_t)transfm2_getscreeny(objecteyey, objecteyez);
		sy_hi = (int32_t)sy_raw >> 16;
		if (sy_hi > 0 || sy_hi < -1)
			return;

		/* Y flip around the vertical midline: y' = half - (y - half). */
		half = (int32_t)pixelsdeep >> 1;
		y_flipped = half - ((int32_t)sy_raw - half);

		anim_add_bitmap_draw(parentobject, frame_code, 256, (int16_t)sx_raw, (int16_t)y_flipped, objecteyez,
							 billboard_angle);
	}
}

/* ============================================================================
 * static_laserstaticcollide
 * ----------------------------------------------------------------------------
 * Returns 1 if the swept laser segment (laserxold/yold/zold -> laserx/y/z)
 * collides with the static at target_slot; 0 otherwise. On hit fills
 * collidexoff/yoff/zoff with the impact offset in world units.
 *
 * Fast rejection gates:
 *   - ship_class 14 (gate) or 11 (backdrop marker): never hit.
 *   - shooter_self_idx >= target_slot + OBJ_REF_STATIC_BASE: reject (ordering invariant;
 *     prevents statics shooting 'earlier' statics).
 *   - shooter is a FlightObject (< 28) that isn't the focus object and whose
 *     craft.ai_target_ref doesn't match this target: reject.
 *   - Rough-distance reject at |delta| > 0x20000 on either segment endpoint.
 *
 * Small-object path: species.bound_hwidth <= 0x578. Fall back to the sphere
 * test at radius = bound_hwidth*3/8, via craft{x,y,z}{,old} globals.
 *
 * Large-object path: rotate both endpoints into the static's local frame
 * (using heading/pitch/roll) via fview_calcrotatemove / fview_calcrotateorient,
 * then AABB-reject against the mesh bbox, then run collide_checkhitpolygons
 * for a parametric hit fraction.
 * ========================================================================== */
// FUNCTION: TIE95 0x548D4
int16_t static_laserstaticcollide(uint16_t shooter_obj_idx, uint16_t target_slot) {
	uint32_t shooter_self_idx = (uint16_t)objects[shooter_obj_idx].self_idx;
	uint32_t ship_class;
	uint32_t sp_idx;
	SpeciesEntry* species;
	uint32_t bound_hwidth;
	int32_t static_wx;
	int32_t static_wy;
	int32_t static_wz;
	int32_t dx_cur;
	int32_t dy_cur;
	int32_t dz_cur;
	int32_t dx_old;
	int32_t dy_old;
	int32_t dz_old;

	if (shooter_self_idx >= target_slot + OBJ_REF_STATIC_BASE)
		return 0;

	ship_class = staticobjects[target_slot].ship_class;
	if (ship_class == 14)
		return 0;
	if (ship_class == 11)
		return 0;

	/* Focus-object gate: non-player craft only collide with their
	 * assigned link target. Retail uses NUM_CRAFTS (32); the demo
	 * had 28. */
	if (shooter_self_idx < NUM_CRAFTS && shooter_self_idx != pstate.object_idx) {
#ifdef TIE_MODERN
		if (objects[shooter_self_idx].craft_ptr != NULL)
#endif
			if ((uint16_t)objects[shooter_self_idx].craft_ptr->ai_target_ref !=
				target_slot + OBJ_REF_STATIC_BASE)
				return 0;
	}

	sp_idx = staticobjects[target_slot].species;
	create_getworldposition((uint16_t)(target_slot + OBJ_REF_STATIC_BASE), 0);
	static_wx = worldlocx;
	static_wy = worldlocy;
	static_wz = worldlocz;

	dx_cur = laserx - static_wx;
	dy_cur = lasery - static_wy;
	dz_cur = laserz - static_wz;
	if ((uint32_t)collide_roughdistance3d(dx_cur, dy_cur, dz_cur) > 0x20000u)
		return 0;

	dx_old = laserxold - static_wx;
	dy_old = laseryold - static_wy;
	dz_old = laserzold - static_wz;
	if ((uint32_t)collide_roughdistance3d(dx_old, dy_old, dz_old) > 0x20000u)
		return 0;

	species = &species_table[sp_idx];
	bound_hwidth = species->bound_hwidth;

	if (bound_hwidth > 0x578u) {
		/* Large-object path: rotate endpoints into local frame. */
		int32_t x_loc;
		int32_t y_loc;
		int32_t heading;
		int32_t pitch;
		uint16_t roll;
		ShipMeshLOD* component;
		uint8_t* mesh_base;
		const int16_t* bbox;
		int32_t bbox_max_x;
		int32_t bbox_max_z;
		int32_t bbox_max_y;
		int32_t bbox_min_x;
		int32_t bbox_min_z;
		int32_t bbox_min_y;
		uint32_t hit_t;

		gatex1 = dx_cur;
		gatey1 = dy_cur;
		gatez1 = dz_cur;
		gatex2 = dx_old;
		gatey2 = dy_old;
		gatez2 = dz_old;

		heading = staticobjects[target_slot].heading_byte << 8;
		pitch = staticobjects[target_slot].pitch_byte << 8;
		roll = staticobjects[target_slot].roll_byte << 8;
		fview_calcrotatemove(pitch, heading, NULL);
		fview_calcrotateorient(roll, 0, NULL);

		/* Invert the forward basis row to match the static's "look direction". */
		craftf1 = -craftf1;
		craftf2 = -craftf2;
		craftf3 = -craftf3;

		/* Transform the two endpoints into the static's local frame. */
		x_loc = math2_dot3_q15_clamped(craftS1, craftS2, craftS3, gatex1, gatey1, gatez1);
		y_loc = math2_dot3_q15_clamped(craftU1, craftU2, craftU3, gatex1, gatey1, gatez1);
		gatez1 = math2_dot3_q15_clamped(craftf1, craftf2, craftf3, gatex1, gatey1, gatez1);
		gatex1 = x_loc;
		gatey1 = y_loc;

		x_loc = math2_dot3_q15_clamped(craftS1, craftS2, craftS3, gatex2, gatey2, gatez2);
		y_loc = math2_dot3_q15_clamped(craftU1, craftU2, craftU3, gatex2, gatey2, gatez2);
		gatez2 = math2_dot3_q15_clamped(craftf1, craftf2, craftf3, gatex2, gatey2, gatez2);
		gatex2 = x_loc;
		gatey2 = y_loc;

		if (TIE_FLIGHT_TIE98) {
			/* TIE98 tests mesh 0's authored descriptor bounds in OPT axis order
			 * (side, forward, up) before entering the collision tree. */
			const int32_t start_x = gatex2;
			const int32_t start_y = gatez2;
			const int32_t start_z = gatey2;
			const int32_t end_x = gatex1;
			const int32_t end_y = gatez1;
			const int32_t end_z = gatey1;
			const int32_t min_x = modelmesh_getboundsminx((uint8_t)sp_idx, 0);
			const int32_t min_y = modelmesh_getboundsminy((uint8_t)sp_idx, 0);
			const int32_t min_z = modelmesh_getboundsminz((uint8_t)sp_idx, 0);
			const int32_t max_x = modelmesh_getboundsmaxx((uint8_t)sp_idx, 0);
			const int32_t max_y = modelmesh_getboundsmaxy((uint8_t)sp_idx, 0);
			const int32_t max_z = modelmesh_getboundsmaxz((uint8_t)sp_idx, 0);
			if ((start_x < min_x && end_x < min_x) || (start_y < min_y && end_y < min_y) ||
				(start_z < min_z && end_z < min_z) || (start_x > max_x && end_x > max_x) ||
				(start_y > max_y && end_y > max_y) || (start_z > max_z && end_z > max_z))
				return 0;
			return (int16_t)collide_checksweptmodelmeshcollision((uint8_t)sp_idx, 0, start_x, start_y,
																 start_z, end_x, end_y, end_z);
		}

		/* Resolve mesh pointer via the species's model handle. */
		component = draw_getcomponentptr(xmemhdl_Lock_Handle(species->model_handle), 0);
		xmemhdl_Unlock_Handle(species->model_handle);

		/* Follow the component's self-relative offset to the poly-data
		 * header, then skip (PolyMeshHeader + face-color table) to reach the
		 * bounding box: [max_x, max_z, max_y, min_x, min_z, min_y]. */
		mesh_base = (uint8_t*)component + (int16_t)component->offset;
		bbox = (const int16_t*)(mesh_base + mesh_base[4] + 5);

		/* Scale ×2 (legacy bound-box units). */
		gatex1 *= 2;
		gatey1 *= 2;
		gatez1 *= 2;
		gatex2 *= 2;
		gatey2 *= 2;
		gatez2 *= 2;

		bbox_max_x = bbox[0];
		bbox_max_z = bbox[1];
		bbox_max_y = bbox[2];
		bbox_min_x = bbox[3];
		bbox_min_z = bbox[4];
		bbox_min_y = bbox[5];

		/* Swept-segment vs AABB: reject when both endpoints are strictly on
		 * the outside of any single face. */
		if (bbox_max_x > gatex1 && bbox_max_x > gatex2)
			return 0;
		if (bbox_max_y > gatey1 && bbox_max_y > gatey2)
			return 0;
		if (bbox_max_z > gatez1 && bbox_max_z > gatez2)
			return 0;
		if (bbox_min_x < gatex1 && bbox_min_x < gatex2)
			return 0;
		if (bbox_min_y < gatey1 && bbox_min_y < gatey2)
			return 0;
		if (bbox_min_z < gatez1 && bbox_min_z < gatez2)
			return 0;

		hit_t = collide_checkhitpolygons(mesh_base, gatex1, gatey1, gatez1, gatex2, gatey2, gatez2, 0);
		if (hit_t != 0) {
			/* Parametric hit: scale world delta by hit_t / 0x7FFF to report
			 * the impact offset in world units. */
			collidexoff = laserx - laserxold;
			collideyoff = lasery - laseryold;
			collidezoff = laserz - laserzold;
			collidexoff = ((int32_t)hit_t * collidexoff) >> 15;
			collideyoff = ((int32_t)hit_t * collideyoff) >> 15;
			collidezoff = ((int32_t)hit_t * collidezoff) >> 15;
			return 1;
		}
		return 0;
	}

	/* Small-object path: sphere test. */
	craftxold = craftx = worldlocx;
	craftyold = crafty = worldlocy;
	craftzold = craftz = worldlocz;
	return (int16_t)collide_checkboxcollision((int32_t)((bound_hwidth >> 3) + (bound_hwidth >> 2)));
}

/* ============================================================================
 * static_laserhitstatic
 * ----------------------------------------------------------------------------
 * Apply a laser hit's effect on the static at target_slot. Invoked from
 * COLLIDE_collisions after static_laserstaticcollide reported a hit.
 *
 * The projectile FlightObject at proj_idx is mutated in-place into an
 * impact-effect sprite (genus 13 / category 5). Explosion variant depends on
 * static class and projectile weapon:
 *   - ship_class 10 (deflector): no damage; explosion 131 (deflect ring).
 *   - projectile ship_idx 141/142 (ion/disruptor): zero hp + ion counter;
 *     explosion 131.
 *   - otherwise: normal kill. Species 77 (rebel mine turret) fires a
 *     retaliatory laser. Link-flag FGs tick mission_linked_data[lc]
 *     (saturated at 0xFF). Species is zeroed and kill credit goes to the
 *     shooter.
 * ========================================================================== */
// FUNCTION: TIE95 0x54F6C
void static_laserhitstatic(uint16_t proj_idx, uint16_t target_slot) {
	uint16_t fg_idx;
	uint16_t explosion_ship_idx;

	if (staticobjects[target_slot].ship_class == 10) {
		/* Gate/deflector: no damage. Ion/disruptor (141/142) shows the
		 * 132 sparkle variant, conventional shots show 131. */
		if (objects[proj_idx].ship_idx == 141 || objects[proj_idx].ship_idx == 142)
			explosion_ship_idx = 132;
		else
			explosion_ship_idx = 131;
	} else if (objects[proj_idx].ship_idx == 141 || objects[proj_idx].ship_idx == 142) {
		/* Ion/disruptor on a regular static: offline-kill and tally the
		 * ion-disable counter. Retail uses 132 here too. */
		staticobjects[target_slot].status_flags = 0;
		fgstatus[staticobjects[target_slot].fg_idx].counts[FG_COUNT_DISABLED]++;
		explosion_ship_idx = 132;
	} else {
		/* Conventional kill. */
		fg_idx = staticobjects[target_slot].fg_idx;
		TIE_FLIGHT_TRACE_FG_EXIT((uint16_t)(target_slot + OBJ_REF_STATIC_BASE), TIE_TRACE_EXIT_DESTROYED);
		fgstatus[fg_idx].counts[FG_COUNT_DESTROYED]++;
		explosion_ship_idx = 129;

		if (staticobjects[target_slot].species == 77) {
			/* Rebel mine turret: fire retaliation at the shooter. */
			laser_createprojectilefromstatic(target_slot, (uint16_t)objects[proj_idx].self_idx);
		}

		if (fg_array[fg_idx].link_flag) {
			uint8_t lc = fg_array[fg_idx].link_code;
			if (++mission.mission_linked_data[lc] == 0)
				mission.mission_linked_data[lc] = 0xFF;
		}

		staticobjects[target_slot].species = 0; /* free the slot */
		collide_updatekills((uint16_t)objects[proj_idx].self_idx, 0xFFFFu, 1);
	}

	TIE_FLIGHT_TRACE_EXPLOSION(proj_idx, (uint8_t)explosion_ship_idx);

	/* Convert the projectile into an impact-effect sprite. Retail
	 * STATIC_laserhitstatic leaves field_54 (craft_ptr) untouched —
	 * the slot keeps pointing at &warheads[wh_idx] even after the
	 * conversion, and downstream warhead-slot iterators (e.g.
	 * PAIORDER_avoidhitorder) read that pointer unconditionally.
	 *
	 * Retail only skips the worldloc fetch for the deflector flash (131)
	 * and ion sparkle (132); the copy below still runs, so those
	 * sprites take whatever position the last fetch left in worldloc. */
	if (explosion_ship_idx != 131 && explosion_ship_idx != 132) {
		create_getworldposition((uint16_t)(target_slot + OBJ_REF_STATIC_BASE), 0);
		objects[proj_idx].world_x = worldlocx;
		objects[proj_idx].world_y = worldlocy;
		objects[proj_idx].world_z = worldlocz;
	} else {
		objects[proj_idx].world_x = worldlocx;
		objects[proj_idx].world_y = worldlocy;
		objects[proj_idx].world_z = worldlocz;
	}

	/* If the projectile is itself a warhead type (per the byte_C5463
	 * table — 143/144/148-154), upgrade the impact to the 129 chunk
	 * variant so capital-ship/static hits look like real explosions
	 * rather than tiny flashes. */
	if (projectile_is_warhead_type[objects[proj_idx].ship_idx - WEAPON_SPECIES_BASE])
		explosion_ship_idx = 129;

	objects[proj_idx].ship_idx = (uint8_t)explosion_ship_idx;
	objects[proj_idx].age_ticks = 0;
	objects[proj_idx].death_timer = 0;
	objects[proj_idx].heading = 0;
	objects[proj_idx].roll = 0;
	objects[proj_idx].genus = GENUS_EXPLOSION;
	objects[proj_idx].category = 5;
	objects[proj_idx].anim_frame = 2;
	objects[proj_idx].current_speed = 0;
	objects[proj_idx].pitch = objects[proj_idx].damage_state = 0;
	objects[proj_idx].orient_dirty = 1;
	objects[proj_idx].move_dirty = 1;

	/* EXPLOSION event for a projectile-versus-static impact. The
	 * deflector flash (131) and ion sparkle (132) variants also fire
	 * the event; the renderer picks the right effect from
	 * param0 = explosion_ship_idx. */
#ifdef TIE_MODERN
	{
		TieEvent ev = {
			.kind     = TIE_EVENT_EXPLOSION,
			.actor_id = objects[proj_idx].idnumber,
			.world_pos = {
				objects[proj_idx].world_x,
				objects[proj_idx].world_y,
				objects[proj_idx].world_z,
			},
			.param0   = (int32_t)explosion_ship_idx,
			.param1   = 0,
		};
		TieSnapshotBuilder_PushEvent(&ev);
	}
#endif

	/* Both deflector (131) and ion (132) flashes use the dedicated zap
	 * SFX 25; everything else picks one of the 4 generic explosion
	 * sounds at random (19-22). */
	if (explosion_ship_idx == 131 || explosion_ship_idx == 132)
		fsfx_triggersfx(25, proj_idx);
	else
		fsfx_triggersfx((uint16_t)((math2_getrandom() & 3) + 19), proj_idx);
}

/* Update one mine turret. After its fractional cooldown, it selects a live
 * in-range target, computes lead and scatter, offsets the barrel by turret
 * orientation, and spawns a homing projectile. */
// FUNCTION: TIE95 0x55278
void static_updatemineguns(uint16_t slot_idx) {
#ifdef TIE_MODERN
	int32_t half_ticks;
	TieStaticWeaponTimingState* high_rate;
#endif
	int32_t sx;
	int32_t sy;
	int32_t sz;
	uint16_t fg_idx;
	uint16_t tgt;
	int32_t tx;
	int32_t ty;
	int32_t tz;
	int32_t aim_x;
	int32_t aim_y;
	int32_t aim_z;
	uint16_t heading;
	uint16_t pitch;
	uint16_t off;
	uint16_t inv_dist;
	uint16_t hit_prob;
	uint16_t proj_slot;
	uint16_t proj_ship;
	uint16_t wh_idx;
	uint8_t cooldown;

	if (staticobjects[slot_idx].status_flags == 0)
		return;

#ifdef TIE_MODERN
	high_rate = NULL;
	if (TieFlightTiming_IsHighRate()) {
		uint16_t numerator;

		high_rate = TieFlightTimingState_StaticWeapon(slot_idx, staticobjects[slot_idx].idnumber);
		numerator = (uint16_t)(frameticks + high_rate->remainder);
		half_ticks = numerator / 2;
		high_rate->remainder = (uint8_t)(numerator % 2u);
	} else
		half_ticks = frameticks / 2;
#endif

	cooldown = staticobjects[slot_idx].mine_cooldown;
#ifdef TIE_MODERN
	if (cooldown > half_ticks) {
		staticobjects[slot_idx].mine_cooldown = (uint8_t)(cooldown - half_ticks);
#else
	if (cooldown > frameticks / 2) {
		staticobjects[slot_idx].mine_cooldown = (uint8_t)(cooldown - frameticks / 2);
#endif
		return;
	}

	/* 236-tick reload between shots. */
	staticobjects[slot_idx].mine_cooldown = 236;
#ifdef TIE_MODERN
	if (high_rate)
		high_rate->remainder = 0;
#endif

	sx = staticobjects[slot_idx].world_x;
	sy = staticobjects[slot_idx].world_y;
	sx <<= 8;
	sy <<= 8;
	sz = staticobjects[slot_idx].world_z;
	sz <<= 8;
	shooterx = sx;
	shootery = sy;
	shooterz = sz;

	fg_idx = staticobjects[slot_idx].fg_idx;
	/* Species 76 turrets only engage live, status-flagged targets. */
	ai.live_target_only = staticobjects[slot_idx].species == 76;

	/* Target selection: primary/secondary pair first, extended pair fallback. */
	tgt = paifight_findgunnertargetingroup(fg_array[fg_idx].ai[0].pri_type, fg_array[fg_idx].ai[0].pri_id,
										   fg_array[fg_idx].ai[0].pri_sec_op, fg_array[fg_idx].ai[0].sec_type,
										   fg_array[fg_idx].ai[0].sec_id);
	if (tgt == 0xFFFF)
		tgt = paifight_findgunnertargetingroup(
			fg_array[fg_idx].ai[0].target_type[0], fg_array[fg_idx].ai[0].target_id[0],
			fg_array[fg_idx].ai[0].target_op, fg_array[fg_idx].ai[0].target_type[1],
			fg_array[fg_idx].ai[0].target_id[1]);
	if (tgt == 0xFFFF)
		return;

	create_getworldposition(tgt, 0);
	tx = worldlocx;
	ty = worldlocy;
	tz = worldlocz;
	if (collide_roughdistance3d(tx - sx, ty - sy, tz - sz) >= 0x10000u)
		return;

	if (tgt < 0x3800) {
		uint16_t lead;

		/* Lead the target by its per-tick motion over the flight time. */
		trig2_ctop(objects[tgt].world_x - sx, objects[tgt].world_y - sy, objects[tgt].world_z - sz);
		trig2_polardistance *= framerate;
		if (staticobjects[slot_idx].species == 76)
			trig2_polardistance >>= 15;
		else
			trig2_polardistance >>= 14;
		lead = (uint16_t)trig2_polardistance + (math2_getrandom() & 3) - 1;
		aim_x = (objects[tgt].world_x - objects[tgt].world_x_prev) * lead + objects[tgt].world_x;
		aim_y = (objects[tgt].world_y - objects[tgt].world_y_prev) * lead + objects[tgt].world_y;
		aim_z = (objects[tgt].world_z - objects[tgt].world_z_prev) * lead + objects[tgt].world_z;
	} else {
		aim_x = tx;
		aim_y = ty;
		aim_z = tz;
	}
	trig2_ctop(aim_x - sx, aim_y - sy, aim_z - sz);
	heading = trig2_xyangle;
	pitch = trig2_zangle;

	/* Offset the muzzle toward the aim direction. */
	off = staticobjects[slot_idx].species <= 76 ? 150 : 170;
	if (pitch < 0x2000) {
		sz += off;
	} else if (pitch > 0x6000) {
		/* Off-zenith turrets can't fire straight up. */
		if (staticobjects[slot_idx].species >= 77)
			return;
		sz -= off;
	} else if (heading < 0x2000 || heading > 0xE000) {
		sy += off;
	} else if (heading < 0x6000) {
		sx += off;
	} else if (heading < 0xA000) {
		sy -= off;
	} else {
		sx -= off;
	}

	/* Accuracy falls off with distance and with fast-moving targets. */
	inv_dist = ~(trig2_polardistance >= 0x10000 ? 0xFFFF : (uint16_t)trig2_polardistance);
	hit_prob = math2_fraction(inv_dist, tgt >= 0x3800 ? 0xFFFF
										: (uint16_t)objects[tgt].current_speed < 188
											? 0xFFFF
											: 0xFFFF - (((uint16_t)objects[tgt].current_speed - 188u) << 7));
	if ((uint16_t)math2_getrandom() > hit_prob) {
		/* Miss: scatter both aim angles by up to ~4/256 of a turn. */
		int16_t scatter;

		scatter = (math2_getrandom() + 0x300) & 0x3FF;
		if ((uint16_t)math2_getrandom() >= 0x8000)
			scatter = -scatter;
		heading += scatter;
		scatter = (math2_getrandom() + 0x300) & 0x3FF;
		if ((uint16_t)math2_getrandom() >= 0x8000) {
			pitch -= scatter;
			if (pitch & 0x8000)
				pitch = 0;
		} else {
			pitch += scatter;
			if (pitch & 0x8000)
				pitch = 0x7FFF;
		}
	}

	proj_slot = create_findslot(7);
	if (proj_slot == 0xFFFF)
		return;

	objects[proj_slot].category = 1;
	objects[proj_slot].genus = GENUS_PROJECTILE_NPC;
	if (staticobjects[slot_idx].species == 76)
		proj_ship = 142;
	else if ((int8_t)fg_array[fg_idx].side == 1 || (int8_t)fg_array[fg_idx].side == 4)
		proj_ship = 140;
	else
		proj_ship = 138;
	objects[proj_slot].ship_idx = (uint8_t)proj_ship;
	objects[proj_slot].age_ticks = 1;
	objects[proj_slot].self_idx = (int16_t)(slot_idx + OBJ_REF_STATIC_BASE);
	objects[proj_slot].ship_type_override = 0;
	objects[proj_slot].side = fg_array[staticobjects[slot_idx].fg_idx].side;
	objects[proj_slot].heading = heading;
	objects[proj_slot].orient_dirty = 1;
	objects[proj_slot].move_dirty = 1;
	objects[proj_slot].current_speed = (int16_t)projectilevelocity[proj_ship - WEAPON_SPECIES_BASE];
	objects[proj_slot].collision_radius = (int16_t)projectileweight[proj_ship - WEAPON_SPECIES_BASE];
	objects[proj_slot].roll = 0;
	objects[proj_slot].death_timer = (int16_t)(236 * projectilelife[proj_ship - WEAPON_SPECIES_BASE]);
	objects[proj_slot].pitch = pitch;

	fview_calcrotatemove(pitch, heading, &objects[proj_slot]);

	/* Step from the muzzle to the projectile model origin. */
	objects[proj_slot].world_x_prev = sx;
	objects[proj_slot].world_y_prev = sy;
	objects[proj_slot].world_z_prev = sz;
	sx += (craftmoveX * (int16_t)TIE_FLIGHT_EDITION(
							projectilelength, tie98_projectilelength)[proj_ship - WEAPON_SPECIES_BASE]) >>
		  15;
	sy += (craftmoveY * (int16_t)TIE_FLIGHT_EDITION(
							projectilelength, tie98_projectilelength)[proj_ship - WEAPON_SPECIES_BASE]) >>
		  15;
	sz += (craftmoveZ * (int16_t)TIE_FLIGHT_EDITION(
							projectilelength, tie98_projectilelength)[proj_ship - WEAPON_SPECIES_BASE]) >>
		  15;
	objects[proj_slot].world_x = sx;
	objects[proj_slot].world_y = sy;
	objects[proj_slot].world_z = sz;

	fsfx_triggerlasersfx(proj_slot);

	/* Register the homing target in the projectile's warhead slot. */
	wh_idx = proj_slot - NUM_CRAFTS;
	warheads[wh_idx].homing_tier = 0;
	warheads[wh_idx].target_obj = tgt;
	objects[proj_slot].craft_ptr = (CraftData*)&warheads[wh_idx];
}
