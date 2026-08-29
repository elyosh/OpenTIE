#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "tie/static.h"
#include "tie_runtime/snapshot/snapshot.h"
#include "tie_runtime/snapshot/snapshot_internal.h"
#include "tie_runtime/timing/flight_timing.h"
#include "tie_runtime/diagnostics/flight_trace.h"
#include "tie_runtime/timing/flight_timing_state.h"

#include "anim.h"
#include "tie/collide.h"
#include "tie/collide_opt.h"
#include "tie/create.h"
#include "tie/draw.h"
#include "tie/drawpol.h"
#include "tie/fsfx.h"
#include "tie/fview.h"
#include "tie/laser.h"
#include "tie/logbuf2.h" /* pixelsdeep */
#include "tie/math2.h"
#include "tie/modelmesh.h"
#include "tie/pai.h" /* ai.live_target_only */
#include "tie/paifight.h"
#include "tie/render_scene_tie98.h"

#include "tie/shipext.h" /* EFGStruct / EAIStruct layout */
#include "tie/tie.h"
#include "tie/transfm2.h"
#include "tie/trig2.h"

/* ------------------------------------------------------------------
 * Local helpers
 * ------------------------------------------------------------------ */

/* Model handles are direct pointers and require no locking. */
static inline void* xmemhdl_lock(void* handle) { return handle; }
static inline void xmemhdl_unlock(void* handle) { (void)handle; }

// PORT: TIE98 aliases StaticObject storage through its unified render-object layout.
static FlightObject static_render_objects_tie98[NUM_STATIC_OBJECTS];

static FlightObject* static_get_render_object_tie98(uint16_t slot_idx) {
	const StaticObject* source = &staticobjects[slot_idx];
	FlightObject* object = &static_render_objects_tie98[slot_idx];
	memset(object, 0, sizeof *object);
	object->genus = source->ship_class;
	object->ship_idx = source->species;
	object->world_x = (int32_t)source->world_x << 8;
	object->world_y = (int32_t)source->world_y << 8;
	object->world_z = (int32_t)source->world_z << 8;
	object->self_idx = (int16_t)(slot_idx + OBJ_REF_STATIC_BASE);
	return object;
}

/* Watcom clamp to ±(2^30 - 0x10000). Matches the `cmp eax, 0x40000000; mov
 * eax, 0x3FFF0000` / `cmp eax, -0x40000000; mov eax, -0x3FFF0000` idiom
 * emitted for saturating Q30 multiplies before the >>15 shift. */
static inline int32_t clamp_q30(int32_t v) {
	if (v >= 0x40000000)
		return 0x3FFF0000;
	if (v <= -0x40000000)
		return -0x3FFF0000;
	return v;
}

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
// FUNCTION: TIE 0x54710
void static_drawstaticobject(uint16_t slot_idx) {
	uint16_t self_idx = (uint16_t)(slot_idx + OBJ_REF_STATIC_BASE);
	StaticObject* so = &staticobjects[slot_idx];
	uint8_t sp_idx = so->species;
	const AnimOp* frame_tab = (const AnimOp*)species_table[sp_idx].draw_data;

	parentobject = self_idx;

	if (!frame_tab) {
		if (TieProfile_UsesTie98Logic())
			return;
		/* Complex BSP mesh: anim_frame != 0 hides the mesh. */
		if (so->anim_frame == 0)
			draw_drawcomplexobject(self_idx);
		return;
	}

	AnimOp frame_code = frame_tab[so->anim_frame];

	/* Skip header / jump / reset / delay / kill opcodes; only MESH and
	 * BITMAP opcodes produce output here. */
	if (!animop_is_mesh(frame_code) && !animop_is_bitmap(frame_code))
		return;

	if (animop_is_bitmap(frame_code)) {
		/* Billboard sprite. Reject if behind the camera. */
		if (objecteyez < 0)
			return;

		/* Pick the world-to-eye row most orthogonal to the view
		 * direction; its XY projection defines the sprite's billboard
		 * rotation. */
		int32_t abs_A3 = (rotworldeyeA3 < 0) ? -rotworldeyeA3 : rotworldeyeA3;
		int32_t abs_B3 = (rotworldeyeB3 < 0) ? -rotworldeyeB3 : rotworldeyeB3;
		int32_t axis_horz, axis_vert;
		if (abs_A3 >= abs_B3) {
			axis_horz = rotworldeyeB1;
			axis_vert = rotworldeyeB2;
		} else {
			axis_horz = rotworldeyeA1;
			axis_vert = rotworldeyeA2;
		}

		int16_t billboard_angle;
		if (axis_horz >= 0)
			billboard_angle = (int16_t)(-(int32_t)trig2_arctan(axis_vert, axis_horz));
		else
			billboard_angle = trig2_arctan(axis_vert, -axis_horz);

		/* Project onto screen, require |high16| <= 1 on both axes
		 * (i.e. within one screen-width of the visible rect). */
		uint32_t sx_raw = (uint32_t)transfm2_getscreencoordx(objecteyex, objecteyez);
		int32_t sx_hi = (int32_t)sx_raw >> 16;
		if (sx_hi > 0 || sx_hi < -1)
			return;

		uint32_t sy_raw = (uint32_t)transfm2_getscreencoordy(objecteyey, objecteyez);
		int32_t sy_hi = (int32_t)sy_raw >> 16;
		if (sy_hi > 0 || sy_hi < -1)
			return;

		/* Y flip around the vertical midline: y' = half - (y - half). */
		int32_t half = (int32_t)pixelsdeep >> 1;
		int32_t y_flipped = half - ((int32_t)sy_raw - half);

		anim_add_bitmap_draw(parentobject, frame_code, 256, (int16_t)sx_raw, (int16_t)y_flipped, objecteyez,
							 billboard_angle);
		return;
	}

	if (TieProfile_UsesTie98Logic())
		return;

	/* Polygon mesh: locate LOD for this eyez and emit via DRAWPOL. */
	int32_t eyex = objecteyex, eyey = objecteyey, eyez = objecteyez;
	void* handle = species_table[sp_idx].model_handle;
	/* Skip the 2-byte file-size prefix — matches retail's v48=a1+2. */
	ShipModelData* model = (ShipModelData*)((uint8_t*)xmemhdl_lock(handle) + 2);
	ShipMeshLOD* component = draw_getcomponentptr(model, 0);
	const uint16_t* lod = draw_getdetailptr(component, eyez);
	drawpol_drawpolyobject(lod, eyex, eyey, eyez);
	xmemhdl_unlock(handle);
}

// FUNCTION: TIE98 0x487F20
void static_drawstaticobject_tie98(uint16_t slot_idx) {
	StaticObject* object = &staticobjects[slot_idx];
	const AnimOp* draw_data = (const AnimOp*)species_table[object->species].draw_data;
	parentobject = (uint16_t)(slot_idx + OBJ_REF_STATIC_BASE);

	if (draw_data != NULL) {
		const AnimOp frame = draw_data[object->anim_frame];
		if (!animop_is_mesh(frame) && !animop_is_bitmap(frame))
			return;
		if (animop_is_bitmap(frame)) {
			static_drawstaticobject(slot_idx);
			return;
		}
		FlightModel_Draw_Object_Mesh(static_get_render_object_tie98(slot_idx), 0);
		return;
	}

	if (object->anim_frame == 0) {
		draw_process_object_components_tie98((uint16_t)(slot_idx + OBJ_REF_STATIC_BASE));
		FlightModel_Draw_Object(static_get_render_object_tie98(slot_idx));
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
 * (using pitch/yaw/roll) via fview_calcrotatemove / fview_calcrotateorient,
 * then AABB-reject against the mesh bbox, then run collide_checkhitpolygons
 * for a parametric hit fraction.
 * ========================================================================== */
// FUNCTION: TIE 0x548D4
int16_t static_laserstaticcollide(uint16_t shooter_obj_idx, uint16_t target_slot) {
	uint16_t target_self_idx = (uint16_t)(target_slot + OBJ_REF_STATIC_BASE);
	uint16_t shooter_self_idx = (uint16_t)objects[shooter_obj_idx].self_idx;

	if (shooter_self_idx >= target_self_idx)
		return 0;

	StaticObject* so = &staticobjects[target_slot];
	uint8_t ship_class = so->ship_class;
	if (ship_class == 14)
		return 0;
	if (ship_class == 11)
		return 0;

	/* Focus-object gate: non-player craft only collide with their
	 * assigned link target. Retail uses NUM_CRAFTS (32); the demo
	 * had 28. */
	if (shooter_self_idx < NUM_CRAFTS && shooter_self_idx != pstate.object_idx) {
		CraftData* craft = objects[shooter_self_idx].craft_ptr;
		if (craft && (uint16_t)craft->ai_target_ref != target_self_idx)
			return 0;
	}

	uint8_t sp_idx = so->species;
	create_getworldposition(target_self_idx, 0);
	int32_t static_wx = worldlocx;
	int32_t static_wy = worldlocy;
	int32_t static_wz = worldlocz;

	int32_t dx_cur = laserx - static_wx;
	int32_t dy_cur = lasery - static_wy;
	int32_t dz_cur = laserz - static_wz;
	if ((uint32_t)collide_roughdistance3d(dx_cur, dy_cur, dz_cur) > 0x20000u)
		return 0;

	int32_t dx_old = laserxold - static_wx;
	int32_t dy_old = laseryold - static_wy;
	int32_t dz_old = laserzold - static_wz;
	if ((uint32_t)collide_roughdistance3d(dx_old, dy_old, dz_old) > 0x20000u)
		return 0;

	uint16_t bound_hwidth = species_table[sp_idx].bound_hwidth;

	if (bound_hwidth <= 0x578u) {
		/* Small-object path: sphere test. */
		craftx = craftxold = static_wx;
		crafty = craftyold = static_wy;
		craftz = craftzold = static_wz;
		return (int16_t)collide_checkboxcollision((uint32_t)((bound_hwidth >> 3) + (bound_hwidth >> 2)));
	}

	/* Large-object path: rotate endpoints into local frame. */
	gatex1 = dx_cur;
	gatey1 = dy_cur;
	gatez1 = dz_cur;
	gatex2 = dx_old;
	gatey2 = dy_old;
	gatez2 = dz_old;

	fview_calcrotatemove((int16_t)((uint16_t)so->yaw_byte << 8), (int16_t)((uint16_t)so->pitch_byte << 8),
						 NULL);
	fview_calcrotateorient((int16_t)((uint16_t)so->roll_byte << 8), 0, NULL);

	/* Invert the forward basis row to match the static's "look direction". */
	craftf1 = -craftf1;
	craftf2 = -craftf2;
	craftf3 = -craftf3;

	/* Transform the two endpoints into the static's local frame and
	 * saturate to ±Q30 before the final >>15 normalisation. */
	int32_t x_loc1 = clamp_q30(gatez1 * craftS3 + gatey1 * craftS2 + gatex1 * craftS1) >> 15;
	int32_t y_loc1 = clamp_q30(gatez1 * craftU3 + gatey1 * craftU2 + gatex1 * craftU1) >> 15;
	int32_t z_loc1 = clamp_q30(gatez1 * craftf3 + gatey1 * craftf2 + gatex1 * craftf1) >> 15;

	int32_t x_loc2 = clamp_q30(gatez2 * craftS3 + gatey2 * craftS2 + gatex2 * craftS1) >> 15;
	int32_t y_loc2 = clamp_q30(gatez2 * craftU3 + gatey2 * craftU2 + gatex2 * craftU1) >> 15;
	int32_t z_loc2 = clamp_q30(gatez2 * craftf3 + gatey2 * craftf2 + gatex2 * craftf1) >> 15;

	if (TieProfile_UsesTie98Logic()) {
		/* TIE98 tests mesh 0's authored descriptor bounds in OPT axis order
		 * (side, forward, up) before entering the collision tree. */
		const int32_t start_x = x_loc2;
		const int32_t start_y = z_loc2;
		const int32_t start_z = y_loc2;
		const int32_t end_x = x_loc1;
		const int32_t end_y = z_loc1;
		const int32_t end_z = y_loc1;
		const int32_t min_x = modelmesh_getboundsminx(sp_idx, 0);
		const int32_t min_y = modelmesh_getboundsminy(sp_idx, 0);
		const int32_t min_z = modelmesh_getboundsminz(sp_idx, 0);
		const int32_t max_x = modelmesh_getboundsmaxx(sp_idx, 0);
		const int32_t max_y = modelmesh_getboundsmaxy(sp_idx, 0);
		const int32_t max_z = modelmesh_getboundsmaxz(sp_idx, 0);
		if ((start_x < min_x && end_x < min_x) || (start_y < min_y && end_y < min_y) ||
			(start_z < min_z && end_z < min_z) || (start_x > max_x && end_x > max_x) ||
			(start_y > max_y && end_y > max_y) || (start_z > max_z && end_z > max_z))
			return 0;
		return (int16_t)collide_checksweptmodelmeshcollision(sp_idx, 0, start_x, start_y, start_z, end_x,
															 end_y, end_z);
	}

	/* Rewrite the globals and scale ×2 (legacy bound-box units). */
	gatex1 = x_loc1 * 2;
	gatey1 = y_loc1 * 2;
	gatez1 = z_loc1 * 2;
	gatex2 = x_loc2 * 2;
	gatey2 = y_loc2 * 2;
	gatez2 = z_loc2 * 2;

	/* Resolve mesh pointer via the species's model handle. */
	void* handle = species_table[sp_idx].model_handle;
	/* Skip the 2-byte file-size prefix — matches retail's v48=a1+2. */
	ShipModelData* model = (ShipModelData*)((uint8_t*)xmemhdl_lock(handle) + 2);
	ShipMeshLOD* component = draw_getcomponentptr(model, 0);
	xmemhdl_unlock(handle);

	/* From the component LOD header, follow the self-relative offset at
	 * +4 (read as the top 16 bits of an unaligned dword at +2) to the
	 * poly-data header, then skip (PolyMeshHeader + face-color table) to
	 * reach the bounding box: [max_x, max_z, max_y, min_x, min_z, min_y]. */
	uint8_t* comp_base = (uint8_t*)component;
	int32_t lod_dword = *(const int32_t*)(comp_base + 2);
	uint8_t* mesh_base = comp_base + (lod_dword >> 16);
	uint8_t num_faces = mesh_base[4]; /* PolyMeshHeader.numfaces */
	const int16_t* bbox = (const int16_t*)(mesh_base + 5 + num_faces);

	int16_t bbox_max_x = bbox[0];
	int16_t bbox_max_z = bbox[1];
	int16_t bbox_max_y = bbox[2];
	int16_t bbox_min_x = bbox[3];
	int16_t bbox_min_z = bbox[4];
	int16_t bbox_min_y = bbox[5];

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

	uint32_t hit_t = collide_checkhitpolygons(mesh_base, gatex1, gatey1, gatez1, gatex2, gatey2, gatez2, 0);
	if (hit_t == 0)
		return 0;

	/* Parametric hit: scale world delta by hit_t / 0x7FFF to report the
	 * impact offset in world units. */
	collidexoff = ((laserx - laserxold) * (int32_t)hit_t) >> 15;
	collideyoff = ((lasery - laseryold) * (int32_t)hit_t) >> 15;
	collidezoff = ((laserz - laserzold) * (int32_t)hit_t) >> 15;
	return 1;
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
 * Returns the FSFX trigger result (caller ignores).
 * ========================================================================== */
// FUNCTION: TIE 0x54F6C
int16_t static_laserhitstatic(uint16_t proj_idx, uint16_t target_slot) {
	StaticObject* so = &staticobjects[target_slot];
	int16_t explosion_ship_idx;
	uint8_t proj_ship = objects[proj_idx].ship_idx;

	if (so->ship_class == 10) {
		/* Gate/deflector: no damage. Ion/disruptor (141/142) shows the
		 * 132 sparkle variant, conventional shots show 131. */
		explosion_ship_idx = (proj_ship == 141 || proj_ship == 142) ? 132 : 131;
	} else if (proj_ship == 141 || proj_ship == 142) {
		/* Ion/disruptor on a regular static: offline-kill and tally the
		 * ion-disable counter. Retail uses 132 here too. */
		so->status_flags = 0;
		fgstatus[so->fg_idx].cond[7].detail++;
		explosion_ship_idx = 132;
	} else {
		/* Conventional kill. */
		uint8_t fg_idx = so->fg_idx;
		TIE_FLIGHT_TRACE_FG_EXIT((uint16_t)(target_slot + OBJ_REF_STATIC_BASE),
							 TIE_TRACE_EXIT_DESTROYED);
		fgstatus[fg_idx].cond[1].count++;
		explosion_ship_idx = 129;

		if (so->species == 77) {
			/* Rebel mine turret: fire retaliation at the shooter. */
			laser_createprojectilefromstatic(target_slot, (uint16_t)objects[proj_idx].self_idx);
		}

		if (fg_array[fg_idx].link_flag) {
			uint8_t lc = fg_array[fg_idx].link_code;
			uint8_t nld = (uint8_t)(mission.mission_linked_data[lc] + 1);
			mission.mission_linked_data[lc] = nld;
			if (nld == 0)
				mission.mission_linked_data[lc] = 0xFFu;
		}

		so->species = 0; /* free the slot */
		collide_updatekills((uint16_t)objects[proj_idx].self_idx, 0xFFFFu);
	}

	TIE_FLIGHT_TRACE_EXPLOSION(proj_idx, (uint8_t)explosion_ship_idx);

	/* Convert the projectile into an impact-effect sprite. Retail
	 * STATIC_laserhitstatic leaves field_54 (craft_ptr) untouched —
	 * the slot keeps pointing at &warheads[wh_idx] even after the
	 * conversion, and downstream warhead-slot iterators (e.g.
	 * PAIORDER_avoidhitorder) read that pointer unconditionally.
	 *
	 * Skip the worldloc fetch for both deflector flashes (131) and ion
	 * sparkles (132): those keep the projectile's existing position so
	 * the sprite plays where the laser actually struck, not at the
	 * static's centre. */
	if (explosion_ship_idx != 131 && explosion_ship_idx != 132)
		create_getworldposition((uint16_t)(target_slot + OBJ_REF_STATIC_BASE), 0);
	objects[proj_idx].world_x = worldlocx;
	objects[proj_idx].world_y = worldlocy;
	objects[proj_idx].world_z = worldlocz;

	/* If the projectile is itself a warhead type (per the byte_C5463
	 * table — 143/144/148-154), upgrade the impact to the 129 chunk
	 * variant so capital-ship/static hits look like real explosions
	 * rather than tiny flashes. */
	{
		unsigned int idx = (unsigned int)proj_ship - WEAPON_SPECIES_BASE;
		if (idx < WARHEAD_TYPE_COUNT && projectile_is_warhead_type[idx])
			explosion_ship_idx = 129;
	}

	objects[proj_idx].ship_idx = (uint8_t)explosion_ship_idx;
	objects[proj_idx].anim_frame = 2;
	objects[proj_idx].genus = GENUS_EXPLOSION;
	objects[proj_idx].damage_state = 0;
	objects[proj_idx].category = 5;
	objects[proj_idx].age_ticks = 0;
	objects[proj_idx].roll = 0;
	objects[proj_idx].current_speed = 0;
	objects[proj_idx].death_timer = 0;
	objects[proj_idx].heading = 0;
	objects[proj_idx].pitch = 0;
	objects[proj_idx].orient_dirty = 1;
	objects[proj_idx].move_dirty = 1;

	/* EXPLOSION event for a projectile-versus-static impact. The
	 * deflector flash (131) and ion sparkle (132) variants also fire
	 * the event; the renderer picks the right effect from
	 * param0 = explosion_ship_idx. */
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

	/* Both deflector (131) and ion (132) flashes use the dedicated zap
	 * SFX 25; everything else picks one of the 4 generic explosion
	 * sounds at random (19-22). */
	uint16_t sfx_id;
	if (explosion_ship_idx == 131 || explosion_ship_idx == 132)
		sfx_id = 25;
	else
		sfx_id = (uint16_t)((math2_getrandom() & 3) + 19);
	return (int16_t)fsfx_triggersfx(sfx_id, proj_idx);
}

/* Update one mine turret. After its fractional cooldown, it selects a live
 * in-range target, computes lead and scatter, offsets the barrel by turret
 * orientation, and spawns a homing projectile. */
// FUNCTION: TIE 0x55278
int16_t static_updatemineguns(uint16_t slot_idx) {
	StaticObject* so = &staticobjects[slot_idx];

	if (so->status_flags == 0)
		return 0;

	int32_t half_ticks;
	TieStaticWeaponTimingState* high_rate = NULL;
	if (TieFlightTiming_IsHighRate()) {
		high_rate = TieFlightTimingState_StaticWeapon(slot_idx, so->idnumber);
		const uint16_t numerator = (uint16_t)(frameticks + high_rate->remainder);
		half_ticks = numerator / 2u;
		high_rate->remainder = (uint8_t)(numerator % 2u);
	} else {
		half_ticks = (int32_t)(frameticks / 2u);
	}
	/* mine_cooldown is uint8_t: the "236" reset value is stored as 0xEC
	 * and read back unsigned. The decompiler renders the reset as "-20"
	 * because the compiler encodes the byte store via the signed form. */
	int32_t cooldown = (int32_t)so->mine_cooldown;

	if (cooldown > half_ticks) {
		so->mine_cooldown = (uint8_t)(cooldown - half_ticks);
		return (int16_t)(cooldown - half_ticks);
	}

	so->mine_cooldown = 236u;
	if (high_rate)
		high_rate->remainder = 0;

	int32_t sx_w = (int32_t)so->world_x << 8;
	int32_t sy_w = (int32_t)so->world_y << 8;
	int32_t sz_w = (int32_t)so->world_z << 8;
	shooterx = sx_w;
	shootery = sy_w;
	shooterz = sz_w;

	uint16_t fg_idx = so->fg_idx;
	uint8_t sp_idx = so->species;
	/* In the binary this is ai.live_target_only — a PAI scanner gate shared with the
	 * target-select helpers. Mine turrets set it so the scanner requires
	 * live/status-flagged targets for the current shot. */
	ai.live_target_only = (uint8_t)(sp_idx == 76);

	/* Target selection: extended quad first, primary pair fallback. */
	const EAIStruct* ai0 = &fg_array[fg_idx].ai[0];
	uint16_t tgt = paifight_findgunnertargetingroup(ai0->pri_type, ai0->pri_id, ai0->pri_sec_op,
													ai0->sec_type, ai0->sec_id);
	if (tgt == 0xFFFFu)
		tgt = paifight_findgunnertargetingroup(ai0->target_type[0], ai0->target_id[0], ai0->target_op,
											   ai0->target_type[1], ai0->target_id[1]);
	if (tgt == 0xFFFFu)
		return 0;

	create_getworldposition(tgt, 0);
	int32_t tx = worldlocx;
	int32_t ty = worldlocy;
	int32_t tz = worldlocz;

	uint32_t dist = (uint32_t)collide_roughdistance3d(tx - sx_w, ty - sy_w, tz - sz_w);
	if (dist >= 0x10000u)
		return 0;

	int32_t aim_world_x = tx;
	int32_t aim_world_y = ty;
	int32_t aim_world_z = tz;
	if (tgt < OBJ_REF_STATIC_BASE) {
		trig2_ctop(tx - sx_w, ty - sy_w, tz - sz_w);

		/* Lead time = polardistance * framerate >> (15 or 14). */
		int32_t lead_raw = trig2_polardistance * (int32_t)framerate;
		int32_t shift = (sp_idx == 76) ? 15 : 14;
		int16_t lead_frames = (int16_t)(lead_raw >> shift);
		uint16_t lead_with_jitter =
			(uint16_t)(((uint16_t)math2_getrandom() & 3u) + (uint16_t)lead_frames - 1u);

		aim_world_x += (int32_t)lead_with_jitter * (objects[tgt].world_x - objects[tgt].world_x_prev);
		aim_world_y += (int32_t)lead_with_jitter * (objects[tgt].world_y - objects[tgt].world_y_prev);
		aim_world_z += (int32_t)lead_with_jitter * (objects[tgt].world_z - objects[tgt].world_z_prev);
	}
	trig2_ctop(aim_world_x - sx_w, aim_world_y - sy_w, aim_world_z - sz_w);

	int16_t aim_xy = trig2_xyangle;
	int16_t aim_z = trig2_zangle;
	uint16_t off = (sp_idx > 0x4Cu) ? (uint16_t)170 : (uint16_t)150;

	/* Barrel offset by aim octant (6 axis-aligned directions). */
	uint16_t uzang = (uint16_t)aim_z;
	uint16_t uxyang = (uint16_t)aim_xy;
	if (uzang < 0x2000u) {
		sz_w += off;
	} else if (uzang > 0x6000u) {
		/* Off-zenith turrets (species >= 0x4D) can't fire straight up. */
		if (sp_idx >= 0x4Du)
			return 0;
		sz_w -= off;
	} else if (uxyang >= 0x2000u && uxyang <= 0xE000u) {
		if (uxyang >= 0x6000u) {
			if (uxyang >= 0xA000u)
				sx_w -= off;
			else
				sy_w -= off;
		} else {
			sx_w += off;
		}
	} else {
		sy_w += off;
	}

	/* Accuracy: wider scatter when target is moving fast. */
	int16_t dist_clamped = (trig2_polardistance < 0x10000) ? (int16_t)trig2_polardistance : (int16_t)-1;
	uint16_t target_speed = tgt < OBJ_REF_STATIC_BASE ? (uint16_t)objects[tgt].current_speed : 0;
	uint16_t inv_dist = (uint16_t)~dist_clamped;
	uint16_t speed_factor;
	if (target_speed >= 0xBCu)
		speed_factor = (uint16_t)(0xFFFFu - ((target_speed - 188u) << 7));
	else
		speed_factor = 0xFFFFu;

	uint16_t hit_prob = math2_fraction(inv_dist, speed_factor);
	if ((uint16_t)math2_getrandom() > hit_prob) {
		/* Miss: perturb both aim angles by ±~3/256 of a full rotation. */
		uint32_t rxy = (uint32_t)(uint16_t)math2_getrandom();
		rxy = (rxy & 0xFFFF00FFu) | ((((rxy >> 8) + 3u) & 3u) << 8);
		int32_t scatter_xy = (int32_t)(int16_t)(uint16_t)rxy;
		if ((uint16_t)math2_getrandom() >= 0x8000u)
			scatter_xy = -scatter_xy;

		uint32_t rz = (uint32_t)(uint16_t)math2_getrandom();
		rz = (rz & 0xFFFF00FFu) | ((((rz >> 8) + 3u) & 3u) << 8);
		int16_t scatter_z = (int16_t)rz;

		aim_xy = (int16_t)(aim_xy + scatter_xy);
		if ((uint16_t)math2_getrandom() < 0x8000u) {
			aim_z = (int16_t)(aim_z + scatter_z);
			if (aim_z < 0)
				aim_z = 0x7FFF;
		} else {
			aim_z = (int16_t)(aim_z - scatter_z);
			if (aim_z < 0)
				aim_z = 0;
		}
	}

	uint16_t proj_slot = create_findslot(7u);
	if (proj_slot == 0xFFFFu)
		return 0;

	/* Populate the projectile FlightObject. */
	FlightObject* p = &objects[proj_slot];
	p->category = 1;
	p->genus = GENUS_PROJECTILE_NPC;

	uint8_t proj_ship;
	if (sp_idx == 76) {
		proj_ship = 142;
	} else {
		uint8_t ver = fg_array[fg_idx].version;
		proj_ship = (ver == 1 || ver == 4) ? (uint8_t)140 : (uint8_t)138;
	}
	p->ship_idx = proj_ship;
	p->age_ticks = 1;
	p->self_idx = (int16_t)(slot_idx + OBJ_REF_STATIC_BASE);
	p->ship_type_override = 0;
	p->side = fg_array[so->fg_idx].side;
	p->pitch = aim_xy;
	p->orient_dirty = 1;
	p->move_dirty = 1;

	/* Projectile-type tables are indexed by (ship_idx - 137). */
	int ptype = (int)proj_ship - 137;
	p->current_speed = (int16_t)projectilevelocity[ptype];
	p->collision_radius = (int16_t)projectileweight[ptype];
	p->roll = 0;
	p->death_timer = (int16_t)(236 * projectilelife[ptype]);
	p->heading = aim_z;

	fview_calcrotatemove(aim_z, aim_xy, p);

	/* Step from the muzzle to the projectile model origin. */
	int32_t plen = (int32_t)TieProjectileLaunchOffset_Get((unsigned int)ptype);
	p->world_x_prev = sx_w;
	p->world_y_prev = sy_w;
	p->world_z_prev = sz_w;
	p->world_x = ((plen * craftmoveX) >> 15) + sx_w;
	p->world_y = ((plen * craftmoveY) >> 15) + sy_w;
	p->world_z = ((plen * craftmoveZ) >> 15) + sz_w;

	fsfx_triggerlasersfx(proj_slot);

	/* Register the homing target in the projectile's warhead slot. */
	uint16_t wh_idx = (uint16_t)(proj_slot - NUM_CRAFTS);
	warheads[wh_idx].homing_tier = 0;
	warheads[wh_idx].target_obj = tgt;
	p->craft_ptr = (CraftData*)&warheads[wh_idx];

	return 0;
}
