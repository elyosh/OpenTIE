#include "tie_runtime/snapshot/snapshot_hud.h"

#include "tie/fediskio.h"
#include "tie/festring.h"
#include "tie/gate.h"
#include "tie/logbuf2.h"
#include "tie/modelmesh.h"
#include "tie/msg.h"
#include "tie/pai.h"
#include "tie/panel.h"
#include "tie/render_scene_tie98.h"
#include "tie/rtsvga2.h"
#include "tie/spec.h"
#include "tie/static.h"
#include "tie/tie.h"
#include "tie/transfm2.h"
#include "tie/user.h"
#include "tie_runtime/runtime/profile.h"
#include "tie_runtime/snapshot/snapshot_internal.h"

#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

static uint8_t s_pip_valid;
static uint16_t s_pip_target_slot;
static int32_t s_pip_back_step_x;
static int32_t s_pip_back_step_y;
static int32_t s_pip_back_step_z;
static int32_t s_pip_basis_A1, s_pip_basis_A2, s_pip_basis_A3;
static int32_t s_pip_basis_B1, s_pip_basis_B2, s_pip_basis_B3;
static int32_t s_pip_basis_C1, s_pip_basis_C2, s_pip_basis_C3;
static uint8_t s_pip_subsys_valid;
static uint16_t s_pip_subsys_target_slot;
static int32_t s_pip_subsys_offset_x;
static int32_t s_pip_subsys_offset_y;
static int32_t s_pip_subsys_offset_z;
static uint8_t s_left_blip_color[PANEL_NUM_BLIPS];
static uint8_t s_right_blip_color[PANEL_NUM_BLIPS];
static int16_t s_left_blip_offset_x[PANEL_NUM_BLIPS];
static int16_t s_left_blip_offset_y[PANEL_NUM_BLIPS];
static int16_t s_right_blip_offset_x[PANEL_NUM_BLIPS];
static int16_t s_right_blip_offset_y[PANEL_NUM_BLIPS];
static uint8_t s_bracket_radar;
static int16_t s_bracket_offset_x;
static int16_t s_bracket_offset_y;

void TieHudSnapshot_RecordPipCamera(uint16_t target_slot) {
	s_pip_back_step_x = worldlocx - camera.x;
	s_pip_back_step_y = worldlocy - camera.y;
	s_pip_back_step_z = worldlocz - camera.z;
	s_pip_target_slot = target_slot;
	s_pip_basis_A1 = worldeyeA1;
	s_pip_basis_A2 = worldeyeA2;
	s_pip_basis_A3 = worldeyeA3;
	s_pip_basis_B1 = worldeyeB1;
	s_pip_basis_B2 = worldeyeB2;
	s_pip_basis_B3 = worldeyeB3;
	s_pip_basis_C1 = worldeyeC1;
	s_pip_basis_C2 = worldeyeC2;
	s_pip_basis_C3 = worldeyeC3;
	s_pip_valid = 1;
}

void TieHudSnapshot_RecordPipSubsystem(uint16_t target_slot) {
	s_pip_subsys_target_slot = target_slot;
	s_pip_subsys_offset_x = rotatedx;
	s_pip_subsys_offset_y = rotatedy;
	s_pip_subsys_offset_z = rotatedz;
	s_pip_subsys_valid = 1;
}

void TieHudSnapshot_RecordRadarBlip(bool forward, int index, uint8_t color, int16_t offset_x,
									int16_t offset_y) {
	if (index < 0 || index >= PANEL_NUM_BLIPS)
		return;
	uint8_t* colors = forward ? s_left_blip_color : s_right_blip_color;
	int16_t* offsets_x = forward ? s_left_blip_offset_x : s_right_blip_offset_x;
	int16_t* offsets_y = forward ? s_left_blip_offset_y : s_right_blip_offset_y;
	colors[index] = color;
	offsets_x[index] = offset_x;
	offsets_y[index] = offset_y;
}

void TieHudSnapshot_RecordRadarBracket(bool forward, int16_t offset_x, int16_t offset_y) {
	s_bracket_radar = forward ? 0u : 1u;
	s_bracket_offset_x = offset_x;
	s_bracket_offset_y = offset_y;
}

void TieHudSnapshot_RecordLaserCharge(uint16_t index, int16_t led_count, uint8_t filled_frame) {
	if (index >= TIE_MAX_HUD_INSTRUMENTS)
		return;
	TieHudInstrument* instrument = &TieSnapshotBuilder_HudMut()->instruments[index];
	instrument->value = led_count;
	instrument->color = filled_frame;
}

void TieHudSnapshot_RecordInstrumentDisplay(uint16_t index, int16_t value, uint8_t color, uint8_t digits) {
	if (index >= TIE_MAX_HUD_INSTRUMENTS)
		return;
	TieHudInstrument* instrument = &TieSnapshotBuilder_HudMut()->instruments[index];
	instrument->value = value;
	instrument->color = color;
	instrument->digits = digits;
}

static void TieHudSnapshot_CopyFestringRemapped(char* dst, size_t dst_size, const uint8_t* src) {
	if (dst_size == 0)
		return;
	size_t out = 0;
	while (*src && out + 1 < dst_size) {
		uint8_t ch = *src++;
		if (ch == 0xFEu) {
			if (!*src || out + 2 >= dst_size)
				break;
			dst[out++] = (char)ch;
			uint8_t color = *src++;
			dst[out++] = (char)((color >= 0x40u) ? color_remap_table[color] : color);
		} else {
			dst[out++] = (char)ch;
		}
	}
	dst[out] = '\0';
}

static int32_t TieHudSnapshot_TargetBoxGeometry(FlightObject* object, int mesh_index,
												int32_t center_offset[3]) {
	center_offset[0] = 0;
	center_offset[1] = 0;
	center_offset[2] = 0;
	const uint16_t spec_index = object->craft_ptr->species_idx;
	const bool component_eligible = object->genus != 0 &&
									(object->genus != 3 || spec_data[spec_index].max_speed != 0) &&
									(object->genus != 1 || object->ship_idx == 19 || object->ship_idx == 20);
	if (!component_eligible)
		return species_table[object->ship_idx].bound_hwidth;
	if (mesh_index == -1)
		return ((spec_data[spec_index].bound_width + spec_data[spec_index].bound_height +
				 spec_data[spec_index].bound_depth) /
				3)
			   << spec_data[spec_index].model_scale_shift;
	pai_RotateLocalVectorToWorldScratch(object, modelmesh_getcenterx(object->ship_idx, mesh_index),
										modelmesh_getcenterz(object->ship_idx, mesh_index),
										-modelmesh_getcentery(object->ship_idx, mesh_index));
	center_offset[0] = rotatedx;
	center_offset[1] = rotatedy;
	center_offset[2] = rotatedz;
	return modelmesh_getcomponentmaxextent(object->ship_idx, mesh_index);
}

static void TieHudSnapshot_CaptureHud(void) {
	TieHudState* hud = TieSnapshotBuilder_HudMut();

	/* Player-craft summary — only meaningful when a flight session
	 * has run create_createhyperin (which seats player_craft). */
	CraftData* pc = pstate.player_craft;
	if (pc) {
		hud->hull_damage = (int16_t)pc->hull_damage;
		hud->hull_max = (int16_t)pc->hull_max;
		hud->installed_subsystems = pc->installed_subsystems;
		hud->working_subsystems = pc->working_subsystems;
		hud->subsystem_active = pc->subsystem_active;
		hud->status_flags = pc->status_flags;
		hud->weapon_group_cnt = pc->weapon_group_cnt;
		hud->beam_type = pc->beam_type;
		hud->laser_power = pc->laser_power;
		hud->shield_power = pc->shield_power;
		hud->beam_power = pc->beam_power;
		hud->ion_drained = (uint8_t)(pc->ion_drain_timer != 0);
		hud->throttle_speed = pc->throttle_speed;
	}
	/* Targeting. target_obj_idx is one of:
	 *   0xFFFF              — none
	 *   < 0x3800            — dynamic-object slot (only valid when
	 *                         objects[].idnumber != 0 && craft_ptr;
	 *                         a stale value at pre-mission time can
	 *                         leave craft_ptr NULL → panel_buildobjectname
	 *                         would crash dereferencing cp->species_idx)
	 *   >= 0x3800 && < 0xFFFF — static / waypoint reference (always
	 *                           safe; panel_buildobjectname's branches
	 *                           handle it without dereferencing craft data) */
	const uint16_t tslot = pstate.target_obj_idx;
	hud->target_obj_slot = tslot;
	hud->target_name[0] = '\0';

	/* target_box_engine_ok: 1995 gates including pilotview ∈ {0,19}.
	 * target_box_inputs_ok: 1998 gates (drops pilotview).
	 * target_bound_hwidth:      1995-look box, avg-extent (spec_data Path A).
	 * target_bound_hwidth_1998: 1998-look component or whole-object extent.
	 * Published here, not in user_targetonscreen, so values survive pause. */
	if (tslot != 0xFFFFu && !replayviewmode && pstate.radar_enable) {
		int32_t bound_pre = 0;
		int32_t bound_pre_1998 = 0;
		bool have_bound = false;
		hud->target_box_center_offset_1998[0] = 0;
		hud->target_box_center_offset_1998[1] = 0;
		hud->target_box_center_offset_1998[2] = 0;
		if (tslot >= NUM_ACTIVE_CRAFT_SLOTS) {
			int species = (tslot >= 0x3800u) ? staticobjects[tslot - 14336].species : objects[tslot].ship_idx;
			bound_pre = species_table[species].bound_hwidth;
			bound_pre_1998 = bound_pre;
			have_bound = true;
		} else if (tslot < NUM_OBJECTS && objects[tslot].craft_ptr != NULL) {
			FlightObject* target = &objects[tslot];
			int sp = target->craft_ptr->species_idx;
			bound_pre = (((int16_t)(spec_data[sp].bound_height + spec_data[sp].bound_depth +
									spec_data[sp].bound_width) /
						  3)
						 << spec_data[sp].model_scale_shift);
			bound_pre_1998 = bound_pre;
			if (TieProfile_UsesTie98Logic()) {
				bound_pre_1998 = TieHudSnapshot_TargetBoxGeometry(target, pstate.radar_target1,
																  hud->target_box_center_offset_1998);
			}

			have_bound = true;
		}
		if (have_bound) {
			hud->target_bound_hwidth = (uint16_t)bound_pre;
			hud->target_bound_hwidth_1998 = (uint16_t)bound_pre_1998;
			hud->target_box_inputs_ok = 1;
			if (camera.pilotview == 0 || camera.pilotview == 19) {
				hud->target_box_engine_ok = 1;
			}
		}
	}

	/* PIP subsystem box — publishes from the sticky s_pip_subsys_* cache
	 * filled by the active edition's 3D CRT update. Engine gates from
	 * panel.c:2607/2689: `radar_enable && target_obj_idx < NUM_CRAFTS`,
	 * plus a slot-match check (stale cache if target switched). The PIP
	 * IMAGE persistence at line ~3247 below uses the same sticky-cache
	 * pattern with s_pip_target_slot; this block keeps the box snapshot
	 * fields in lockstep across paused frames. */
	if (s_pip_subsys_valid && pstate.radar_enable && tslot < NUM_CRAFTS &&
		tslot == s_pip_subsys_target_slot) {
		hud->target_subsystem_offset[0] = s_pip_subsys_offset_x;
		hud->target_subsystem_offset[1] = s_pip_subsys_offset_y;
		hud->target_subsystem_offset[2] = s_pip_subsys_offset_z;
		hud->target_subsystem_box_engine_ok = 1;
	}

	bool target_valid = false;
	if (tslot >= OBJ_REF_STATIC_BASE && tslot != 0xFFFFu) {
		target_valid = true;
	} else if (tslot < NUM_OBJECTS && objects[tslot].ship_idx != 0 &&
			   objects[tslot].genus != GENUS_EXPLOSION && objects[tslot].craft_ptr != NULL) {
		/* Projectiles are valid targets even though they have no idnumber. */
		target_valid = true;
	}

	if (target_valid) {
		/* Full panel_buildobjectname output (with 0xFE+color escape
		 * pairs): species short_name in the side's primary color, then
		 * `: FG_NAME [ N]` in the secondary color. Matches what classic
		 * paints at instruments[90] via festring_outstringcenter
		 * (panel.c::panel_updatecmd). The compose_text festring path
		 * consumes 0xFE inline, so a single TieUIText record renders
		 * both color runs.
		 *
		 * Remap each 0xFE-escaped color byte through the engine's
		 * color_remap_table (matches festring_outstring's remap_color
		 * helper). panel_buildobjectname emits LOGICAL color values
		 * from pick_color_primary/secondary (0x45/0x49/0x51/0x55 etc.);
		 * the HD compose pipeline expects POST-remap palette indices,
		 * so we apply the table here once at snapshot time rather
		 * than per-glyph at draw time. Without this fix, the target
		 * name renders in the wrong color in the HD CMD display. */
		panel_buildobjectname(tslot, 3);
		TieHudSnapshot_CopyFestringRemapped(hud->target_name, sizeof hud->target_name,
											(const uint8_t*)tempstring);

		if (tslot < NUM_OBJECTS) {
			hud->target_status = panel_getcraftstatus(tslot);
		}
	}

	/* UI overlays — direct mirrors of panel-module tracking globals.
	 * Bracket is anchor-relative: (radar_idx, offset_x/y) against the
	 * radar disc the targeted blip landed on. */
	hud->bracket_radar_idx = s_bracket_radar;
	hud->bracket_offset_x = s_bracket_offset_x;
	hud->bracket_offset_y = s_bracket_offset_y;
	hud->bracket_present = (uint8_t)(bracketflag != 0);
	hud->blipbox_x = blipboxx;
	hud->blipbox_y = blipboxy;
	hud->blipbox_present = (uint8_t)(blipboxflag != 0);
	hud->lock_present = (uint8_t)(lockflag != 0);

	/* Instrument geometry comes from the loaded panel definition; displayed
	 * scalar values come from the recovered panel's authoritative cache. */
	for (int i = 0; i < TIE_MAX_HUD_INSTRUMENTS; ++i) {
		hud->instruments[i].x = instruments[i].x;
		hud->instruments[i].y = instruments[i].y;
		hud->instruments[i].param1 = instruments[i].param1;
		hud->instruments[i].param2 = instruments[i].param2;
		if (oldinstruments[i] != -2)
			hud->instruments[i].value = oldinstruments[i];
	}
	const uint8_t clock_minutes = mission.train_craft_type ? mtimer_min : _date.minute;
	const uint8_t clock_seconds = mission.train_craft_type ? mtimer_sec : _date.second;
	snprintf(hud->mission_clock_text, sizeof hud->mission_clock_text, "%2u:%02u", (unsigned)clock_minutes,
			 (unsigned)clock_seconds);
	hud->instruments[TIE_HUDI_CLOCK_DIGITS].color = flightResolution == TIE_FLIGHT_RES_VGA ? 0x4D : 0x4E;

	/* Radar blips. The new* lists hold this frame's just-built blip
	 * positions; old* are last frame's, used for dirty-rect erasure
	 * (irrelevant to a renderer that paints from scratch). list size
	 * is signed int16; clamp before indexing. */
	int16_t lc = newleftlistsize;
	if (lc < 0)
		lc = 0;
	if (lc > TIE_MAX_RADAR_BLIPS)
		lc = TIE_MAX_RADAR_BLIPS;
	for (int i = 0; i < lc; ++i) {
		/* Colour read from the preserved-colour mirror —
		 * rtsvga2_drawblipsVGA has already clobbered
		 * newleftbliplist[i].color with a 2-bit status bitmap by the
		 * time we get here. */
		hud->blips_left[i].color = s_left_blip_color[i];
		hud->blips_left[i].radar_idx = 0;
		hud->blips_left[i].offset_x = s_left_blip_offset_x[i];
		hud->blips_left[i].offset_y = s_left_blip_offset_y[i];
	}
	hud->blip_count_left = (uint16_t)lc;

	int16_t rc = newrightlistsize;
	if (rc < 0)
		rc = 0;
	if (rc > TIE_MAX_RADAR_BLIPS)
		rc = TIE_MAX_RADAR_BLIPS;
	for (int i = 0; i < rc; ++i) {
		hud->blips_right[i].color = s_right_blip_color[i];
		hud->blips_right[i].radar_idx = 1;
		hud->blips_right[i].offset_x = s_right_blip_offset_x[i];
		hud->blips_right[i].offset_y = s_right_blip_offset_y[i];
	}
	hud->blip_count_right = (uint16_t)rc;

	/* Engine's classic-px radar disc radius. SVGA = 44 (math2.c:196
	 * `trig2_sinewordmult(44, ...)`), VGA = 18 (radarmax320[1]). */
	hud->radar_classic_radius = tie_is_high_resolution_flight() ? 44u : 18u;
}

static void TieHudSnapshot_CaptureCockpit(void) {
	TieHudState* hud = TieSnapshotBuilder_HudMut();
	CraftData* pc = pstate.player_craft;
	/* ===== Cockpit state ===== */
	TieCockpitState* ck = TieSnapshotBuilder_CockpitMut();
	const uint8_t view_idx = camera.pilotview;
	ck->view_idx = view_idx;
	if (view_idx < PANEL_NUM_VIEWS) {
		const PanelViewDef* vd = &panelviewdefs[view_idx];
		/* Resolve the inherit/mirror chain to find the view whose LFD
		 * is actually loaded (panel_dosetnewpilotview at panel.c:2196).
		 *   flags & 0xC0 == 0xC0  → mirrored:  dest = flags - 0xC0
		 *   flags & 0x80          → inherited: dest = flags - 0x80
		 *   else                  → dest = view_idx
		 * The renderer needs `view_name` to be the LOADED view's name
		 * for KTX2 lookup; the original view_idx still drives view_title
		 * (each inherited slot has its own "LEFT FWD" / "BACK" label) and
		 * the mirror flag so the renderer can U-flip the source bitmap. */
		uint8_t dest = view_idx;
		if (view_idx != 18) {
			uint8_t f = vd->flags;
			if (f >= 0xC0u)
				dest = (uint8_t)(f - 0xC0u);
			else if (f & 0x80u)
				dest = (uint8_t)(f & 0x7Fu);
		}
		const PanelViewDef* dest_vd = &panelviewdefs[dest];
		const PanelViewPtrs* dest_vp = &panelviewptrs[dest];
		ck->panel_loaded = (uint8_t)(dest_vp->handle != 0);
		ck->view_title_visible = (uint8_t)(dest == 17);
		/* yoffset source mirrors panel_dosetnewpilotview's `geom` pick:
		 * mirrored view uses the ORIGINAL view's geom (its own pos/size/
		 * yoffset describing the mirrored rect), inherited uses DEST's. */
		ck->view_yoffset = ((vd->flags & 0xC0u) == 0xC0u) ? vd->yoffset : dest_vd->yoffset;
		const PanelViewDef* geometry = ((vd->flags & 0xC0u) == 0xC0u) ? vd : dest_vd;
		ck->view_x = geometry->pos_x;
		ck->view_y = geometry->pos_y;
		ck->view_width = geometry->width;
		ck->view_depth = geometry->depth;
		memcpy(ck->view_name, dest_vd->name, sizeof ck->view_name);
		memcpy(ck->view_title, vd->title, sizeof ck->view_title);
		ck->inherit_view = (uint8_t)((vd->flags & 0x80u) != 0);
		ck->mirrored_view = (uint8_t)((vd->flags & 0xC0u) == 0xC0u);
	}
	/* Parts atlas basename — derive from the .INT's `parts[0..7]`
	 * (the .PNL filename minus the trailing 'p'), lowercased, so the
	 * HD bundle lookup matches what cockpit_extract emits. Example:
	 * "TIEFTRP\0" → "tieftr". When parts[0] == 0 the cockpit pass
	 * skips parts/layout loading. */
	{
		char buf[10] = { 0 };
		size_t n = 0;
		for (; n < 8 && parts[n]; ++n) {
			char c = parts[n];
			if (c >= 'A' && c <= 'Z')
				c = (char)(c - 'A' + 'a');
			buf[n] = c;
		}
		if (n > 0 && buf[n - 1] == 'p')
			buf[n - 1] = '\0';
		memcpy(ck->parts_basename, buf, sizeof ck->parts_basename);
		ck->parts_shape_count = (uint16_t)parts[9] + parts[10];
	}
	/* Mask variant — mirrors the spec-num switch in panel_update3Dcrt.
	 * Encoding: 0=default, 1=gunboat, 2=tieadv7, 3=tieadv8, 4=spec4,
	 * 5=spec5, 6=missileboat. VGA collapses tieadv7 and tieadv8 onto
	 * variant 2 (the binary has a single mask for both); SVGA splits
	 * them. The renderer treats this as informational (the cutout is
	 * baked into the per-view PNG alpha) — useful when an HD view
	 * asset is missing and the application needs to pick a fallback. */
	uint8_t mv = 0;
	switch (pstate.player_spec_num) {
		case 15:
			mv = 1;
			break;
		case 7:
			mv = 2;
			break;
		case 8:
			mv = (flightResolution == TIE_FLIGHT_RES_VGA) ? 2 : 3;
			break;
		case 4:
			mv = (flightResolution == TIE_FLIGHT_RES_VGA) ? 0 : 4;
			break;
		case 5:
			mv = (flightResolution == TIE_FLIGHT_RES_VGA) ? 0 : 5;
			break;
		case 11:
			mv = 6;
			break;
		default:
			mv = 0;
			break;
	}
	ck->mask_variant = mv;

	/* PIP viewport (instruments[2]) — x/y/w/h give the inset rect on
	 * the cockpit; the HD pre-pass renders the targeted craft there. */
	ck->pip_x = instruments[2].x;
	ck->pip_y = instruments[2].y;
	ck->pip_w = instruments[2].param1;
	ck->pip_h = instruments[2].param2;

	/* PIP camera — captured inside panel_update3Dcrt right after
	 * panel_pointcamera ran. The cache is sticky across ticks; a
	 * stale capture is suppressed by requiring the cached target
	 * slot to still match pstate.target_obj_idx. This keeps the
	 * PIP rendering during in-engine pause (panel_update3Dcrt
	 * doesn't run, but the target slot is frozen) while still
	 * shipping pip_target_present == 0 when the player drops the
	 * target (target_obj_idx → 0xFFFF) or switches to a non-craft
	 * target (panel_update3Dcrt only fires for craft targets, so
	 * the cached slot won't match the new selection). */
	const bool pip_target_matches =
		s_pip_valid && pstate.target_obj_idx != 0xFFFFu && pstate.target_obj_idx == s_pip_target_slot;
	if (pip_target_matches) {
		ck->pip_target_present = 1;
		ck->pip_target_slot = s_pip_target_slot;
		const float m[9] = {
			(float)s_pip_basis_A1 / 32768.0f, (float)s_pip_basis_B1 / 32768.0f,
			(float)s_pip_basis_C1 / 32768.0f, (float)s_pip_basis_A2 / 32768.0f,
			(float)s_pip_basis_B2 / 32768.0f, (float)s_pip_basis_C2 / 32768.0f,
			(float)s_pip_basis_A3 / 32768.0f, (float)s_pip_basis_B3 / 32768.0f,
			(float)s_pip_basis_C3 / 32768.0f,
		};
		TieSnapshotBuilder_Mat3ToQuat(m, ck->pip_cam_ori);
		ck->pip_back_step[0] = s_pip_back_step_x;
		ck->pip_back_step[1] = s_pip_back_step_y;
		ck->pip_back_step[2] = s_pip_back_step_z;
		/* Targeted-subsystem index for the PIP component highlight.
		 * Matches the engine's PIP gate: panel_update3Dcrt OR's
		 * 0x200 into currenttarget when pstate.radar_enable; DRAW_drawcraft
		 * promotes the matching component's drawpol highlightcolor to 2
		 * (only when both `comp_idx == currenttargetcomp` and the
		 * currenttarget 0x200 bit are set — see draw.c:898). For non-
		 * craft targets the engine never enters the PIP-craft path, so
		 * we gate on target_obj_idx < NUM_CRAFTS too. Cap at 40 =
		 * TIE_FLIGHT_MAX_MESHES to keep the renderer's per-mesh array
		 * lookup safe; the engine's `< num_meshes` per-craft check
		 * collapses to <40 in our enumeration since the snapshot
		 * emitter clamps mesh_count to that. */
		ck->pip_subsys_idx = 0xFFu;
		if (pstate.radar_enable && pstate.target_obj_idx < NUM_CRAFTS && pstate.radar_target1 >= 0 &&
			pstate.radar_target1 < 40) {
			ck->pip_subsys_idx = (uint8_t)pstate.radar_target1;
		}
		/* PIP FOVs derived from the engine projection used during
		 * logbuf2_startPIP — same perspFactor / yAspect as the main
		 * view but with pip_w/h instead of screenXRes/Res, producing
		 * the telephoto framing. perspFactor was already restored to
		 * its main-view value by logbuf2_finishPIP (it's a per-
		 * resolution constant), so the math is self-contained. */
		if (perspFactor > 0 && ck->pip_w && ck->pip_h) {
			const float pw_half = (float)ck->pip_w * 0.5f;
			const float ph_half = (float)ck->pip_h * 0.5f;
			const float y_aspect_factor = yAspect ? (float)(uint16_t)yAspect * (1.0f / 65536.0f) : 1.0f;
			ck->pip_fov_h_half_rad = atanf(pw_half / (float)perspFactor);
			ck->pip_fov_v_half_rad = atanf(ph_half / ((float)perspFactor * y_aspect_factor));
		} else {
			ck->pip_fov_h_half_rad = 0.0f;
			ck->pip_fov_v_half_rad = 0.0f;
		}
		/* No consume — s_pip_valid stays set. Stale captures are
		 * filtered out by the target-slot match above. */
	} else {
		ck->pip_target_present = 0;
		ck->pip_target_slot = 0xFFFFu;
		ck->pip_fov_h_half_rad = 0.0f;
		ck->pip_fov_v_half_rad = 0.0f;
		ck->pip_back_step[0] = 0;
		ck->pip_back_step[1] = 0;
		ck->pip_back_step[2] = 0;
		ck->pip_subsys_idx = 0xFFu;
		/* pos / ori already zeroed by begin_tick. */
	}

	/* Resolution duplicate — use the selected flight/frontend output rather
	 * than assuming that Landru owns the active framebuffer. */
	int classic_width;
	int classic_height;
	TieClassicDisplay_Dimensions(&classic_width, &classic_height);
	ck->classic_w = (uint16_t)classic_width;
	ck->classic_h = (uint16_t)classic_height;

	/* MISSILE_LOCK event. panel_updatelasers resets lockflag at
	 * the top of each call and re-sets it when collide_targetinrange
	 * returns true; we observe the per-tick "lock acquired" rising
	 * edge here (post-task-stack, when lockflag is stable for the
	 * tick) and fire a single one-shot. param0 = target slot,
	 * param1 = warhead_mode so the renderer can pick the right
	 * cue (torp vs missile vs ion). */
	static uint8_t s_prev_lockflag = 0;
	if (lockflag && !s_prev_lockflag && pstate.target_obj_idx != 0xFFFFu) {
		const uint16_t tslot = pstate.target_obj_idx;
		int32_t lx = 0, ly = 0, lz = 0;
		if (tslot < NUM_OBJECTS && objects[tslot].idnumber != 0) {
			lx = objects[tslot].world_x;
			ly = objects[tslot].world_y;
			lz = objects[tslot].world_z;
		}
		TieEvent ev = {
			.kind = TIE_EVENT_MISSILE_LOCK,
			.actor_id = (tslot < NUM_OBJECTS) ? objects[tslot].idnumber : 0u,
			.world_pos = { lx, ly, lz },
			.param0 = (int32_t)tslot,
			.param1 = (int32_t)pstate.player_weapon_mode,
		};
		TieSnapshotBuilder_PushEvent(&ev);
	}
	s_prev_lockflag = lockflag;

	/* slam_active drives the speed/throttle grey override
	 * (panel_updatevalue's `idx == 24 || idx == 25` path). */
	hud->slam_active = pc ? (uint8_t)(pc->slam_active != 0) : 0;

	/* Mission-clock text and color are rebuilt above from the same recovered
	 * state used by panel_updateclock. */

	/* Per-target percentages (idx 58 / 61 / 62) + cargo / inspect
	 * state / subsystem focus. Sentinels (0xFF) when no target or
	 * non-craft target. `tslot_tgt` deliberately re-resolves from
	 * pstate (the earlier MISSILE_LOCK block uses its own `tslot`
	 * in a scope that's closed by now). */
	const uint16_t tslot_tgt = pstate.target_obj_idx;
	if (tslot_tgt < NUM_CRAFTS && objects[tslot_tgt].craft_ptr) {
		CraftData* tgt = objects[tslot_tgt].craft_ptr;

		/* shield/hull/sys digits + target_cargo + target_subsystem_text
		 * are written by panel_updatecmd at its paint sites
		 * (panel_updatevalue → hud->instruments[idx].value/color). */
		(void)tgt;
	}

	/* Threat-view text (target_order_text, target_link_*, target_eta_*)
	 * is written by panel_updatethreatname at its paint sites. */

	/* In-flight message banner (msg.c). The engine pre-expands the
	 * template into messagequeue[0].body so the snapshot just copies
	 * the rendered bytes; no application-side template lookup needed. The
	 * band geometry (msgLineTop/Bottom/Right) is set by
	 * msg_messageinit at flight start. When no message is currently
	 * displayed (template_idx == 0xFFFF), the band is still painted
	 * (backcolor + separator + time-warp) but the body emit is
	 * skipped via present=0. */
	/* Training-mission CRT (gate.c:858-985 + 634-665). Renderer keys
	 * on `training.active`; mirrors gate_trainingupdatecrt's reads. */
	if (mission.train_craft_type) {
		hud->training.active = 1;
		hud->training.level = (uint8_t)mission.train_level;
		hud->training.player_spec_num = (uint8_t)pstate.player_spec_num;
		hud->training.timer_min = mtimer_min;
		hud->training.timer_sec = mtimer_sec;
		hud->training.bonus_active = bonus_countdown_active;
		hud->training.gates_remaining = (uint16_t)mission.train_gates_remaining;
		hud->training.gates_passed = (uint16_t)mission.train_gates_passed;
		hud->training.targets_hit = (uint16_t)mission.train_targets;
		hud->training.score = mission.mission_score;
		hud->training.bonus = mission.train_bonus;
	} else {
		hud->training.active = 0;
		hud->training.bonus_active = 0;
	}

	hud->msg_bar.line_top = (uint16_t)msgLineTop;
	hud->msg_bar.line_bottom = (uint16_t)msgLineBottom;
	hud->msg_bar.line_right = (uint16_t)msgLineRight;
	hud->msg_bar.accelerated_time = acceleratedtimesetting;
	if (messagequeue[0].template_idx != 0xFFFF) {
		hud->msg_bar.present = 1;
		hud->msg_bar.msg_type = messagequeue[0].msg_type;
		hud->msg_bar.side = messagequeue[0].side;
		memcpy(hud->msg_bar.body, messagequeue[0].body, sizeof messagequeue[0].body);
		hud->msg_bar.body[sizeof hud->msg_bar.body - 1] = '\0';
	} else {
		hud->msg_bar.present = 0;
	}
}

void TieHudSnapshot_Capture(void) {
	TieHudSnapshot_CaptureHud();
	TieHudSnapshot_CaptureCockpit();
}
