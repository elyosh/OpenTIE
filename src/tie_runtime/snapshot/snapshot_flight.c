#include "tie_runtime/snapshot/snapshot_flight.h"

#include "tie/anim.h"
#include "tie/backdrp2.h"
#include "tie/drawpol.h"
#include "tie/fediskio.h"
#include "tie/gate.h"
#include "tie/logbuf2.h"
#include "tie/modelmesh.h"
#include "tie/move.h"
#include "tie/render_scene_tie98.h"
#include "tie/rtsvga2.h"
#include "tie/spec.h"
#include "tie/species.h"
#include "tie/static.h"
#include "tie/tie.h"
#include "tie/transfm2.h"
#include "tie_runtime/display/classic_display.h"
#include "tie_runtime/runtime/exports.h"
#include "tie_runtime/runtime/profile.h"
#include "tie_runtime/snapshot/snapshot_billboards.h"
#include "tie_runtime/snapshot/snapshot_internal.h"

#include <math.h>
#include <string.h>

/* Capture the world-to-eye basis immediately after camera setup, before
 * per-object rotations mutate the rasterizer globals. */
static int32_t s_cam_basis_A1, s_cam_basis_A2, s_cam_basis_A3;
static int32_t s_cam_basis_B1, s_cam_basis_B2, s_cam_basis_B3;
static int32_t s_cam_basis_C1, s_cam_basis_C2, s_cam_basis_C3;
static uint8_t s_cam_basis_valid;

void TieFlightSnapshot_RecordCameraBasis(void) {
	s_cam_basis_A1 = worldeyeA1;
	s_cam_basis_A2 = worldeyeA2;
	s_cam_basis_A3 = worldeyeA3;
	s_cam_basis_B1 = worldeyeB1;
	s_cam_basis_B2 = worldeyeB2;
	s_cam_basis_B3 = worldeyeB3;
	s_cam_basis_C1 = worldeyeC1;
	s_cam_basis_C2 = worldeyeC2;
	s_cam_basis_C3 = worldeyeC3;
	s_cam_basis_valid = 1;
}

/* Q1.15 unit-vector component → float in [-1, 1]. 32768 is the
 * canonical Watcom unit (sintable amplitude); int16 minimum -32768
 * maps to -1.0 exactly. */
#define TIE_SNAP_Q15_TO_F(x) ((float)(x) * (1.0f / 32768.0f))

/* One native world unit is approximately 2.44 cm — the
 * HUD range readout (panel_outputdistance) displays km as
 * raw*161>>16 per km/100, i.e. 1 km ~= 40706 raw units, matching the
 * ~40.96 units/m OPT vertex convention. Common Aeron flight models use
 * meters and are instanced at 1/1600; the classic model path remains at
 * its recovered OPT-unit scale. Snapshot positions preserve these native
 * integers and the renderer rebases them before float conversion. */

static uint16_t TieFlightSnapshot_FlightFlags(const FlightObject* obj) {
	uint16_t f = 0;
	if (obj->death_timer != 0)
		f |= TIE_FOBJ_DESTROYED;
	/* Hyperspace/eject markers tracked at the player level via the
	 * hyperspaceflag global and player_ejected; mirror those onto the
	 * player's slot so a renderer can flip cockpit shaders. */
	if (obj == pstate.player) {
		if (hyperspaceflag)
			f |= TIE_FOBJ_HYPER;
		if (player_ejected)
			f |= TIE_FOBJ_EJECTING;
	}
	return f;
}

/* Reconstructs the proper (side, stored-forward, up) craft basis produced by
 * fview_newcalcrotate for static objects. */
static void TieFlightSnapshot_ByteEulersToMat(uint8_t roll_byte, uint8_t yaw_byte, uint8_t pitch_byte,
											  float m[9]) {
	/* int16 angle = byte << 8; 65536 = 2π. */
	const float k = 6.28318530717958647692f / 65536.0f;
	const float yaw = (float)((uint16_t)yaw_byte << 8) * k;
	const float pitch = (float)((uint16_t)pitch_byte << 8) * k;
	const float roll = (float)((uint16_t)roll_byte << 8) * k;

	const float sy = sinf(yaw), cy = cosf(yaw);
	const float sp = sinf(pitch), cp = cosf(pitch);
	const float sr = sinf(roll), cr = cosf(roll);

	/* fview builds side/up/forward from heading and pitch, then applies
	 * roll around its negated-forward axis. Keep columns consistent with
	 * the FlightObject snapshot path; the renderer applies the model-axis
	 * reflection separately. */
	m[0] = cr * cp - sr * cy * sp;
	m[1] = sy * sp;
	m[2] = -cr * cy * sp - sr * cp;
	m[3] = -cr * sp - sr * cy * cp;
	m[4] = sy * cp;
	m[5] = -cr * cy * cp + sr * sp;
	m[6] = sr * sy;
	m[7] = cy;
	m[8] = cr * sy;
}

/* Craft highlight encoding: 0 none, 1 beam target, 2 selected component,
 * 3 player target during its visible blink phase. Component refinement is
 * carried separately by TieFlightObjectComponent.flags bit 2. */
static uint8_t TieFlightSnapshot_CraftHighlight(uint16_t obj_idx, uint8_t ship_idx) {
	(void)ship_idx;

	/* Gate already crossed: gate_drawtraininggate (gate.c:215-219) sets
	 * `currenttarget = parentobject; highlightcolor = 1;` before drawing
	 * the MainHull for any gate with obj_idx < currentgate. Mirror that
	 * here so HD's existing highlight=1 path runs the
	 * highlightmapping[3..5]/targetmapping remap the engine performs in
	 * drawpol_getlightvalue. Sub-meshes stay hidden via the mesh_state
	 * pathway — only MainHull is visible on past gates, so the
	 * craft-level highlight applies to it exclusively. */
	if (obj_idx < NUM_CRAFTS && obj_idx > 0 && objects[obj_idx].genus == GENUS_GATE && obj_idx < currentgate)
		return 1;

	if (obj_idx == bluetarget) {
		/* Beam-locked: every mesh of the craft uses
		 * highlightcolor=1 in the engine — the per-mesh component
		 * promotion to 2 is gated on `if (highlightcolor) goto
		 * label20` (draw.c:861) which keeps the 1 it started with.
		 * Component differentiation only applies to T-key targets;
		 * see the per-mesh resolution in tie_remaster/flight/passes.c. */
		return 1;
	}

	/* T-key target with blink. Engine match for non-bluetarget
	 * crafts requires currenttarget to equal parentobject — i.e.
	 * targetblinkflag and (targetblinkstate & 0x400) both clear in
	 * addition to pstate.target_obj_idx == obj_idx. Bake the blink
	 * phase into the snapshot so the renderer doesn't need a timer.
	 * Per-mesh component override (engine highlightcolor=2 for the
	 * targeted component, 0 elsewhere) lands in the per-component
	 * flags bit 2. */
	if (pstate.target_obj_idx == obj_idx && targetblinkflag == 0 && (targetblinkstate & 0x0400u) == 0) {
		return 3;
	}

	return 0;
}

static bool TieFlightSnapshot_HasCraftData(const FlightObject* obj) {
	return obj->craft_ptr && (obj->genus <= GENUS_PLATFORM || obj->genus == GENUS_GATE);
}

/* Pull craftptr->mesh_state[num_meshes] for a craft slot.
 * The byte at index num_meshes (one past the per-mesh state range)
 * is overlaid as the craft-level lightning-arc anim frame counter
 * in classic. Returns 0xFF for slots that have no CraftData (debris,
 * lasers, explosions) so the HD layer can suppress the bolt rendering
 * uniformly. */
static uint8_t TieFlightSnapshot_LightningState(const FlightObject* obj) {
	if (!TieFlightSnapshot_HasCraftData(obj))
		return 0xFFu;
	uint8_t nm;
	if (TieProfile_UsesTie98Logic()) {
		nm = modelmesh_getcount(obj->ship_idx);
	} else {
		const SpeciesEntry* spec = &species_table[obj->ship_idx];
		if (!spec->model_handle)
			return 0xFFu;
		const ShipModelData* mdl = (const ShipModelData*)((const uint8_t*)spec->model_handle + 2);
		nm = mdl->num_meshes;
	}
	if (nm >= 40)
		return 0xFFu;
	return obj->craft_ptr->mesh_state[nm];
}

/* Emit per-mesh component state for one craft. Returns the
 * number of components written (0 when craft has no CraftData /
 * model). Walks every mesh slot of the species; classic skips
 * mesh_state != MESH_STATE_VISIBLE in draw_drawcraft, but the renderer
 * wants to know about hidden/blown-off meshes too (their absence is
 * part of the visual). */
static uint16_t TieFlightSnapshot_EmitComponents(const FlightObject* obj, uint16_t obj_idx,
												 uint8_t craft_highlight) {
	if (!TieFlightSnapshot_HasCraftData(obj))
		return 0;
	const bool tie98 = TieProfile_UsesTie98Logic();
	const SpeciesEntry* spec = &species_table[obj->ship_idx];
	if (!tie98 && !spec->model_handle)
		return 0;
	const ShipModelData* mdl = tie98 ? NULL : (const ShipModelData*)((const uint8_t*)spec->model_handle + 2);
	uint8_t nm = tie98 ? modelmesh_getcount(obj->ship_idx) : mdl->num_meshes;
	if (!tie98 && nm > 40)
		nm = 40;
	const ShipModelMesh* meshes = tie98 ? NULL : (const ShipModelMesh*)&mdl->lod_records[mdl->num_lods];

	uint16_t emitted = 0;
	for (uint8_t mi = 0; mi < nm; ++mi) {
		TieFlightObjectComponent* c = TieSnapshotBuilder_AllocFlightComponent();
		if (!c)
			break;
		const ShipModelMesh* m = tie98 ? NULL : &meshes[mi];
		const int mesh_type = tie98 ? modelmesh_gettype(obj->ship_idx, mi) : m->mesh_type;
		uint8_t state = obj->craft_ptr->mesh_state[mi];
		uint8_t rot = obj->craft_ptr->mesh_rotation[mi];
		uint8_t f = 0;
		c->tie95_training_pivot_fwd = 0;
		const bool tie98_gate = tie98 && obj->genus == GENUS_GATE;
		const bool detailed_gate = tie98_gate && (obj_idx == gate_render_reference_object ||
												  obj_idx == (uint16_t)(gate_render_reference_object + 1));
		if (tie98_gate) {
			if (mesh_type == TIE_MESH_MAIN_HULL || (detailed_gate && state == MESH_STATE_VISIBLE))
				f |= 0x1u;
		} else if (state == MESH_STATE_VISIBLE) {
			f |= 0x1u; /* visible */
		} else if (obj->genus == GENUS_GATE && mesh_type == TIE_MESH_MAIN_HULL) {
			/* Gate MainHull override. gate_settraininglevel marks every
			 * gate's MainHull MESH_STATE_HIDDEN (gate.c:476-479), but
			 * gate_drawtraininggate explicitly draws it via a direct
			 * drawpol_drawpolyobject call (gate.c:206-225) regardless of
			 * mesh_state. Force visible so HD reproduces the gate frame. */
			f |= 0x1u;
		}
		if (tie98 ? modelmesh_getrotscaledata(obj->ship_idx, mi) != NULL : m->rotation_offset != 0)
			f |= 0x2u;
		/* TIE95 fview_componentrotation ignores authored rotation metadata
		 * during training and synthesizes this pivot from the mesh center. */
		if (!tie98 && mission.train_craft_type && rot != 0) {
			f |= TIE_FLIGHT_COMPONENT_TIE95_TRAINING_ROTATION;
			c->tie95_training_pivot_fwd = (int16_t)-(m->center_fwd >> 1);
		}
		/* Bit 2 marks the focused component, including components with the
		 * same target ID or TIE95 mesh role. Whole-craft highlighting does
		 * not promote components; consumers apply their own blink/PIP gate. */
		const bool is_target = (obj_idx == pstate.target_obj_idx);
		if (is_target && craft_highlight != 1) {
			if (mi == currenttargetcomp) {
				f |= 0x4u;
			} else if (tie98 && currenttargetcomp < nm) {
				const int target_id = modelmesh_gettargetid(obj->ship_idx, currenttargetcomp);
				if (target_id != 0 && modelmesh_gettargetid(obj->ship_idx, mi) == target_id)
					f |= 0x4u;
			} else if (!tie98 && currenttargetcomp < (uint16_t)mdl->num_meshes) {
				const ShipModelMesh* tgt = &meshes[currenttargetcomp];
				if ((m->has_position > 1) || (m->has_position == 1 && m->mesh_type == 1)) {
					if (m->has_position == tgt->has_position && m->mesh_type == tgt->mesh_type) {
						f |= 0x4u;
					}
				}
			}
		}
		c->mesh_idx = mi;
		c->state = state;
		uint16_t rotation_bam = (uint16_t)rot << 8;
		/* TIE98 composes the OPT component matrix through a transpose,
		 * making the visible rotation the inverse of its stored BAM. */
		if (tie98)
			rotation_bam = (uint16_t)(0u - rotation_bam);
		c->rotation_angle = (int16_t)rotation_bam;
		c->flags = f;
		c->hp_remaining = obj->craft_ptr->mesh_component_hp[mi];
		++emitted;
	}
	return emitted;
}

/* SNAPSHOT-ONLY — drain the per-tick billboard capture cache into the
 * TieBillboardState[] queue. Walks the two capture sources:
 *
 *   1. anim_drawverysimpleobject — populated for every slot whose
 *      genus is DEBRIS or EXPLOSION AND whose current animation frame
 *      decodes to an ANIMOP_BITMAP. (DELAY / JUMP / KILL frames leave
 *      species_idx == 0 and are filtered out.)
 *
 *   2. draw_drawcraft's lightning emit — populated once per critically-
 *      damaged craft on the tick the lightning[N] pattern lands on a
 *      bitmap frame.
 *
 * Position comes from world_*_prev — the snapshot emit runs after
 * move_moveobjects integrates one tick, so world_* is post-move; the
 * classic framebuffer was painted with the pre-move position.
 */
static void TieFlightSnapshot_EmitRequiredAssets(void) {
	for (uint16_t species = 0; species < TIE_SPECIES_COUNT; ++species) {
		const SpeciesEntry* entry = &species_table[species];
		if (!(entry->flags & 2) || !(entry->load_flags & 0x18) ||
			((entry->load_flags & 0x40) && !mission.train_craft_type) || !entry->model_handle)
			continue;
		const uint8_t source_kind = entry->load_flags & 3;
		if (source_kind == 1) {
			uint16_t* required = TieSnapshotBuilder_AllocRequiredModelSpecies();
			if (!required)
				return;
			*required = species;
		} else if (source_kind == 2) {
			uint16_t* required = TieSnapshotBuilder_AllocRequiredSpriteSpecies();
			if (!required)
				return;
			*required = species;
		}
	}
}

static void TieFlightSnapshot_EmitBillboards(void) {
	/* Drain the per-flight-slot debris/explosion captures. */
	for (uint16_t i = 0; i < NUM_OBJECTS; ++i) {
		const TieBillboardCaptureFlight* cap = TieBillboardCapture_FlightSlot(i);
		if (!cap || cap->species_idx == 0)
			continue;
		const FlightObject* obj = &objects[i];
		TieBillboardState* out = TieSnapshotBuilder_AllocBillboard();
		if (!out)
			return; /* cap reached; warning already logged */
		out->world_pos[0] = obj->world_x_prev;
		out->world_pos[1] = obj->world_y_prev;
		out->world_pos[2] = obj->world_z_prev;
		out->parent_slot = i;
		out->parent_kind = TIE_BILLBOARD_FLIGHT;
		out->species_idx = cap->species_idx;
		out->bitmap_idx = cap->bitmap_idx;
		out->pixel_scale_q8 = cap->pixel_scale_q8;
		out->bound_hwidth = cap->bound_hwidth;
		out->rotation_bam = cap->rotation_bam;
		out->genus = obj->genus;
	}

	/* Drain the per-craft lightning captures. The classic engine
	 * anchors the bolt at the parent craft's world origin (see the
	 * world_* read in anim_draw_bitmap's flight-object path), NOT at
	 * the damaged Fuselage mesh's articulated offset — preserve that
	 * behaviour. */
	for (uint16_t i = 0; i < NUM_CRAFTS; ++i) {
		const TieBillboardCaptureLightning* cap = TieBillboardCapture_LightningSlot(i);
		if (!cap || !cap->active)
			continue;
		const FlightObject* obj = &objects[i];
		TieBillboardState* out = TieSnapshotBuilder_AllocBillboard();
		if (!out)
			return;
		out->world_pos[0] = obj->world_x_prev;
		out->world_pos[1] = obj->world_y_prev;
		out->world_pos[2] = obj->world_z_prev;
		out->parent_slot = i;
		out->parent_kind = TIE_BILLBOARD_LIGHTNING;
		out->species_idx = cap->species_idx;
		out->bitmap_idx = cap->bitmap_idx;
		out->pixel_scale_q8 = cap->pixel_scale_q8;
		out->bound_hwidth = cap->bound_hwidth;
		out->rotation_bam = cap->rotation_bam;
		/* Parent's genus is the host craft's (FIGHTER, etc.) — not
		 * informative for emissive classification. Renderer uses
		 * parent_kind == TIE_BILLBOARD_LIGHTNING instead. */
		out->genus = 0;
	}
}

/* Export the starfield before classic projection and framebuffer masking.
 * BACKDRP2 constructs the same coordinates in eye space by combining the
 * camera basis; keeping the integer coefficients here produces stable
 * world-space axes without undoing a quantized camera transform. */
static bool TieFlightSnapshot_EmitStarAxis(int lobe, int inner, int outer_shift, int star_off) {
	const int grid_idx = stars[star_off];
	const int nx = grid_idx / 25 - 2;
	const int ny = (grid_idx % 25) / 5 - 2;
	const int nz = grid_idx % 5 - 2;
	int axis[3] = { -32 + nx, -32 + ny, -32 + nz };

	/* Coordinates are scaled by 128: -32 is the cube-face base -1/4,
	 * and each 16-step face increment contributes 1/32 = 4/128. */
	switch (lobe) {
		case 0:
			axis[0] += 4 * inner;
			axis[1] += 4 * outer_shift;
			break;
		case 1:
			axis[0] += 4 * inner;
			axis[2] += 4 * outer_shift;
			break;
		default:
			axis[1] += 4 * inner;
			axis[2] += 4 * outer_shift;
			break;
	}

	TieStarDirection* out = TieSnapshotBuilder_AllocStar();
	if (!out)
		return false;
	out->axis[0] = (float)axis[0];
	out->axis[1] = (float)axis[1];
	out->axis[2] = (float)axis[2];
	uint8_t palette_slot = (uint8_t)(starcol1 + stars[star_off + 1]);
	out->palette_slot = palette_slot <= 3 ? palette_slot : 3;
	memset(out->_pad, 0, sizeof out->_pad);
	return true;
}

static void TieFlightSnapshot_EmitStars(void) {
	if (hyperspaceflag == 3 || hyperspaceflag == 5)
		return;

	const int step = stardetaillevel > 0 ? stardetaillevel : 1;
	for (int lobe = 0; lobe < 3; ++lobe) {
		int star_off = 0;
		for (int outer = 0; outer < 16; outer += step) {
			/* rtsvga2_drawstars updates each lobe's outer base after
			 * drawing the row, then advances the loop index. Preserve
			 * that retail sequencing: rows 0 and 1 both use shift 0. */
			const int outer_shift = outer > 0 ? outer - step : 0;
			for (int inner = 0; inner < 16; inner += step) {
				if (!TieFlightSnapshot_EmitStarAxis(lobe, inner, outer_shift, star_off))
					return;
				star_off += 2;
			}
		}
	}
}

static void TieFlightSnapshot_CaptureFrameState(void) {
	/* The active output is Landru VGA/VESA for TIE95 and the DirectDraw
	 * render surface for TIE98. Do not infer one from the other's globals. */
	int classic_width;
	int classic_height;
	TieClassicDisplay_Dimensions(&classic_width, &classic_height);
	TieSnapshotBuilder_SetClassicDims((uint16_t)classic_width, (uint16_t)classic_height);
	TieFlightLegacyRenderConvention render_convention = TIE_FLIGHT_LEGACY_RENDER_TIE95;
	if (TieProfile_UsesTie98Logic()) {
		render_convention =
			g_useHardware3D ? TIE_FLIGHT_LEGACY_RENDER_TIE98_D3D : TIE_FLIGHT_LEGACY_RENDER_TIE98_SOFTWARE;
	}
	TieSnapshotBuilder_SetLegacyRenderConvention(render_convention);

	/* Logical flight-frame counter — lets the renderer tell whether the
	 * sim advanced between two host-tick snapshots (motion-blur velocity). */
	TieSnapshotBuilder_SetFlightFrame(move_flight_frame());
	TieSnapshotBuilder_SetMissionLoadGeneration(TieRecoveredData_MissionLoadGeneration());
	TieFlightSnapshot_EmitRequiredAssets();

	/* --- Replay mode ------------------------------------------------ */
	TieSnapshotBuilder_SetReplayMode(replayviewmode ? 2 : recordingreplay ? 1 : 0);

	/* --- Directional light (drawpol_getlightvalue) ------------------ *
	 * Engine math: face_dot = clamp_q30(n_q15 · light_q15) >> 15;
	 *              lightval = face_dot >> 11.
	 * The engine's Q15 light vector `(18000, -18000, -18000)` has
	 * magnitude ≈ 31177 (≈ 0.951 in real units) — a Q15 representation
	 * artifact, not an artistic choice. HD remaster path needs a unit
	 * to-light vector so `dot(N, L)` is bounded by 1 everywhere and
	 * the PBR Cook-Torrance / Fresnel terms see a clean cosine. Ship
	 * NORMALISED here. Classic-LUT mode's 16-bin quantisation shifts
	 * moderate-angle faces up by at most one bin — a negligible
	 * brightness change that the engine itself doesn't produce
	 * deliberately. */
	{
		/* The engines use opposite lighting conventions: TIE95 stores the
		 * light as a from-sun (travel) vector dotted against INWARD model
		 * normals; TIE98 stores a to-sun vector dotted against OUTWARD OPT
		 * normals. HD shades outward normals with a to-sun direction, so
		 * TIE95's vector must be negated while TIE98's passes through.
		 * (TIE98's constant flips only Z vs TIE95 instead of the full
		 * negation the convention change implies, so the two games light
		 * from opposite horizontal azimuths; each mode reproduces its own
		 * classic renderer.) */
		const float s = TieProfile_UsesTie98Logic() ? 1.0f : -1.0f;
		const float fx = s * (float)lightX;
		const float fy = s * (float)lightY;
		const float fz = s * (float)lightZ;
		const float len = sqrtf(fx * fx + fy * fy + fz * fz);
		const float inv = (len > 1e-6f) ? (1.0f / len) : 0.0f;
		const float dir3[3] = { fx * inv, fy * inv, fz * inv };
		const float col3[3] = { 1.0f, 1.0f, 1.0f };
		TieSnapshotBuilder_SetDirectionalLight(dir3, col3);
	}

	/* Ship the engine's `gouraudflag` global (drawpol's per-vertex
	 * lighting gate). HD reads this every tick so the in-game video-
	 * options toggle takes effect on the next snapshot — no mesh
	 * rebuild needed. */
	TieSnapshotBuilder_SetGouraudflag(gouraudflag);

	/* And the marking-emission gate. Same wiring path: video-options
	 * + detail-level + replay restore. HD applies the per-mesh
	 * species-17-wing override on top, mirroring DRAW_drawcraft. */
	TieSnapshotBuilder_SetDrawmarkingsflag(drawmarkingsflag);

	/* --- Camera ----------------------------------------------------- */
	TieCameraState* cam = TieSnapshotBuilder_CameraMut();
	cam->world_pos[0] = camera.x;
	cam->world_pos[1] = camera.y;
	cam->world_pos[2] = camera.z;
	if (s_cam_basis_valid) {
		/* The world→eye basis the rasterizer used this tick. Row 0 =
		 * (A1, B1, C1), etc. — same layout transfm2_geteye* multiplies
		 * a world vector by. The renderer-side basis convention is
		 * documented at the top of `src/tie_remaster/flight/renderer.h`. */
		const float m[9] = {
			TIE_SNAP_Q15_TO_F(s_cam_basis_A1), TIE_SNAP_Q15_TO_F(s_cam_basis_B1),
			TIE_SNAP_Q15_TO_F(s_cam_basis_C1), TIE_SNAP_Q15_TO_F(s_cam_basis_A2),
			TIE_SNAP_Q15_TO_F(s_cam_basis_B2), TIE_SNAP_Q15_TO_F(s_cam_basis_C2),
			TIE_SNAP_Q15_TO_F(s_cam_basis_A3), TIE_SNAP_Q15_TO_F(s_cam_basis_B3),
			TIE_SNAP_Q15_TO_F(s_cam_basis_C3),
		};
		TieSnapshotBuilder_Mat3ToQuat(m, cam->ori);
	} else {
		cam->ori[0] = 1.0f;
		cam->ori[1] = 0.0f;
		cam->ori[2] = 0.0f;
		cam->ori[3] = 0.0f;
	}
	/* FOV: classic's perspective math is asymmetric in X vs Y when
	 * yAspect ≠ 0 (VGA 320x200 uses 0xE8BA for the 5:6 non-square
	 * pixel correction; SVGA 640x480 uses 0). The engine projects:
	 *   screen_x = halfpixelswide + perspFactor               × eye_x/eye_z
	 *   screen_y = halfpixelsdeep + perspFactor × yAspect/65k × eye_y/eye_z
	 * So tan(h_half) and tan(v_half) have different denominators —
	 * the HD renderer must use BOTH directly. Defaults match the
	 * front-end / pre-mission state where perspFactor is unset. */
	if (perspFactor > 0) {
		cam->fov_h_half_rad = atanf((float)halfpixelswide / (float)perspFactor);
		/* yAspect is a Q16 multiplier on the projected Y; effective
		 * vertical perspective denominator is perspFactor × yAspect/65536.
		 * Stored as uint16_t but used as a 16-bit signed-to-Q16 ratio
		 * (e.g. 0xE8BA → 0.909). Zero means square pixels (no scale). */
		const float y_aspect_factor = yAspect ? (float)(uint16_t)yAspect * (1.0f / 65536.0f) : 1.0f;
		cam->fov_v_half_rad = atanf((float)halfpixelsdeep / ((float)perspFactor * y_aspect_factor));
	} else {
		cam->fov_h_half_rad = 0.5586f;
		cam->fov_v_half_rad = 0.3f;
	}
	cam->target_obj_slot = camera.view_target_obj;
	cam->pilotview = camera.pilotview;
	cam->zoom_active = (uint8_t)(camera.view_zoom_flag != 0);
	cam->view_zoom_raw = camera.view_zoom;
	/* Per-cockpit-view projection-Y offset, normalized inside the active
	 * aperture. Engine: screen_y = transfm2_screenyoffset +
	 * halfpixelsdeep + projected_y. The HD renderer combines this value
	 * with the aperture rectangle to place the reticle in the full-output
	 * projection. Guard against pre-mission pixelsdeep == 0. */
	cam->screen_y_offset_ndc =
		(pixelsdeep > 0) ? ((float)transfm2_screenyoffset * 2.0f / (float)pixelsdeep) : 0.0f;

	/* Active 3D viewport as a fraction of the classic framebuffer. Use
	 * LOGBUF2's installed geometry rather than panelviewdefs: view 18 is
	 * synthesized as a panel-free viewport, and mirrored/aliased panel
	 * entries may draw through another entry's geometry. These globals are
	 * also exactly what supplied halfpixelswide/deep to the FOV above. */
	if (screenXRes > 0 && screenYRes > 0 && pixelswide > 0 && pixelsdeep > 0) {
		cam->viewport_frac_x = (float)displaycorner_columns / (float)screenXRes;
		cam->viewport_frac_y = (float)displaycorner_lines / (float)screenYRes;
		cam->viewport_frac_w = (float)pixelswide / (float)screenXRes;
		cam->viewport_frac_h = (float)pixelsdeep / (float)screenYRes;
	} else {
		cam->viewport_frac_x = 0.0f;
		cam->viewport_frac_y = 0.0f;
		cam->viewport_frac_w = 1.0f;
		cam->viewport_frac_h = 1.0f;
	}
}

static void TieFlightSnapshot_CaptureWorld(void) {
	/* --- FlightObject pool ------------------------------------------ */
	for (uint16_t i = 0; i < NUM_OBJECTS; ++i) {
		const FlightObject* obj = &objects[i];
		/* Free-slot test matches the engine: tie.c:1819's per-object
		 * render dispatch skips on obj->ship_idx == 0. idnumber is
		 * NOT a free-slot signal — create_createmission() resets
		 * idnumber = 0 and then assigns idnumber++ to each craft, so
		 * the first-created craft (typically the player) has
		 * idnumber 0 even though its slot is occupied. */
		if (obj->ship_idx == 0)
			continue;
		TieFlightObjectState* out = TieSnapshotBuilder_AllocFlight();
		if (!out)
			break; /* cap reached; warning already logged */
		out->id = obj->idnumber;
		out->slot = i;
		out->genus = obj->genus;
		out->ship_idx = obj->ship_idx;
		/* Mesh-component debris carrier: render path needs the source
		 * craft's species and which submesh detached (engine encodes
		 * mesh_idx as anim_frame >> 1 in create_createcomponent). */
		if (obj->ship_idx == 89) {
			out->parent_ship_idx = obj->ship_type_override;
			out->submesh_idx = (uint8_t)(obj->anim_frame >> 1);
		} else {
			out->parent_ship_idx = 0;
			out->submesh_idx = 0;
		}
		out->side = obj->side;
		out->damage_state = obj->damage_state;
		out->fg_idx = obj->fg_idx;
		out->flags = TieFlightSnapshot_FlightFlags(obj);
		/* Position MUST come from world_*_prev, not world_*. The
		 * snapshot emit runs after tie_doframe finishes, i.e. after
		 * move_moveobjects has integrated every object one tick
		 * forward. world_* therefore holds the post-move value, but
		 * the engine's classic framebuffer was painted in step 6 with
		 * the PRE-move value — move_moveobjects writes world_*_prev
		 * = world_* at the top of the integration, so world_*_prev
		 * now holds exactly what the engine rendered. Camera (set in
		 * tie_updatescreen before render and untouched by move) is
		 * already at pre-move time, so reading _prev here keeps the
		 * snapshot internally coherent. Visible symptom of the
		 * post-move read: laser bolts appear ~one tick of bolt velocity
		 * ahead of the muzzle on the spawn frame, and fast-moving
		 * ships sit one tick ahead of the classic framebuffer when
		 * paused. */
		out->world_pos[0] = obj->world_x_prev;
		out->world_pos[1] = obj->world_y_prev;
		out->world_pos[2] = obj->world_z_prev;
		/* Row-major craft-to-world rotation matrix with COLUMNS =
		 * (side, fwd, up). Engine's transfm2_geteyecoords
		 * (transfm2.c:102) consumes vertex (x, y, z) as basis indices
		 * 1/2/3 = calcS/calcf/calcU. Since calcf = -craft->fwd
		 * (fview.c:145, :275), the engine's effective rendering
		 * basis is (side, -fwd, up) — which has det=-1 and is NOT
		 * a valid rotation. We keep this snapshot a valid rotation
		 * (side, fwd, up) and apply the y-axis sign flip in the
		 * renderer (TieRenderMath_Mat4FromQuaternionTranslation). */
		const float m[9] = {
			TIE_SNAP_Q15_TO_F(obj->side_x), TIE_SNAP_Q15_TO_F(obj->fwd_x), TIE_SNAP_Q15_TO_F(obj->up_x),
			TIE_SNAP_Q15_TO_F(obj->side_y), TIE_SNAP_Q15_TO_F(obj->fwd_y), TIE_SNAP_Q15_TO_F(obj->up_y),
			TIE_SNAP_Q15_TO_F(obj->side_z), TIE_SNAP_Q15_TO_F(obj->fwd_z), TIE_SNAP_Q15_TO_F(obj->up_z),
		};
		TieSnapshotBuilder_Mat3ToQuat(m, out->ori);
		out->current_speed = obj->current_speed;
		out->death_timer = obj->death_timer;
		out->highlight = TieFlightSnapshot_CraftHighlight(i, obj->ship_idx);
		out->lightning_state = TieFlightSnapshot_LightningState(obj);
		out->decal_color = obj->decal_color;
		out->model_variant = TieProfile_UsesTie98Logic()
								 ? (tie98_model_variant_enabled[obj->ship_idx] ? obj->decal_color : 0)
								 : obj->decal_color;
		out->anim_frame = obj->anim_frame;
		/* Components follow the parent record. */
		out->component_start = TieSnapshotBuilder_FlightComponentCount();
		out->component_count = TieFlightSnapshot_EmitComponents(obj, i, out->highlight);
	}

	/* --- StaticObject pool ------------------------------------------ */
	for (uint16_t i = 0; i < NUM_STATIC_OBJECTS; ++i) {
		const StaticObject* so = &staticobjects[i];
		if (so->species == 0)
			continue; /* free slot */
		TieStaticObjectState* out = TieSnapshotBuilder_AllocStatic();
		if (!out)
			break;
		out->id = so->idnumber;
		out->slot = i;
		out->ship_class = so->ship_class;
		out->species = so->species;
		out->fg_idx = so->fg_idx;
		out->anim_frame = so->anim_frame;
		/* Static coordinates are compressed by 256 in the engine record. */
		out->world_pos[0] = (int32_t)so->world_x * 256;
		out->world_pos[1] = (int32_t)so->world_y * 256;
		out->world_pos[2] = (int32_t)so->world_z * 256;
		/* Compose the three Euler bytes (yaw, pitch, roll) into the
		 * same row-major basis convention used by FlightObject above
		 * and pass through TieSnapshotBuilder_Mat3ToQuat. */
		float m[9];
		TieFlightSnapshot_ByteEulersToMat(so->roll_byte, so->yaw_byte, so->pitch_byte, m);
		TieSnapshotBuilder_Mat3ToQuat(m, out->ori);
		out->status_flags = so->status_flags;
		const AnimOp* frame_table = (const AnimOp*)species_table[so->species].draw_data;
		out->model_visible = frame_table ? (uint8_t)animop_is_mesh(frame_table[so->anim_frame])
										 : (uint8_t)(so->anim_frame == 0);
		/* Preserve the flight group's planet palette selector. */
		out->palette_version = fg_array[so->fg_idx].version;
	}

	/* --- Hyperspace streak seeds -----------------------------------
	 * During phases 3 & 5 the engine reuses staticobjects[] as streak
	 * spawn points (anim.c case 2 reseeds world_{x,y,z} from
	 * MATH2_getrandom with the fixed LFSR seed 29287; the static loop
	 * above filtered them out because species == 0). Publish them via
	 * the dedicated hyperstars[] channel so the HD streak pass has
	 * clean POD seeds (world position + slot index) without
	 * polluting the static-object channel with orient/species/anim
	 * fields that are undefined on these slots. Capped by the user's
	 * hyperspacedetail setting (0/25/50/75, max 64). */
	if (hyperspaceflag == 3 || hyperspaceflag == 5) {
		uint16_t cap = (uint16_t)hyperspacedetail;
		if (cap > NUM_STATIC_OBJECTS)
			cap = NUM_STATIC_OBJECTS;
		for (uint16_t i = 0; i < cap; ++i) {
			const StaticObject* so = &staticobjects[i];
			TieHyperstar* out = TieSnapshotBuilder_AllocHyperstar();
			if (!out)
				break;
			out->world_pos[0] = (int32_t)so->world_x * 256;
			out->world_pos[1] = (int32_t)so->world_y * 256;
			out->world_pos[2] = (int32_t)so->world_z * 256;
			out->slot = (uint8_t)i;
		}
	}

	TieFlightSnapshot_EmitStars();

	/* --- Mission backdrop set -------------------------------------- */
	{
		TieBackdropSet* bd = TieSnapshotBuilder_BackdropsMut();
		memcpy(bd->slot_pos, backdropposition, TIE_MAX_BACKDROP_SLOTS);
		memcpy(bd->slot_species, backdropspecies, TIE_MAX_BACKDROP_SLOTS);
		/* Recover the planet variant the engine folded into
		 * species_table[].lfd_entry at load (lfd_entry - planet base 47).
		 * Non-planet slots keep 0; slot_species identifies those. */
		for (int i = 0; i < TIE_MAX_BACKDROP_SLOTS; i++) {
			uint8_t sp = backdropspecies[i];
			bd->slot_planet_version[i] =
				(sp >= 114 && sp <= 116)
					? (uint8_t)(species_table[sp].lfd_entry - species_table[87].lfd_entry)
					: 0;
		}
		bd->front_cnt = backdropfrontcnt;
		bd->back_cnt = backdropbackcnt;
		bd->left_cnt = backdropleftcnt;
		bd->right_cnt = backdroprightcnt;
		bd->top_cnt = backdroptopcnt;
		bd->bottom_cnt = backdropbottomcnt;
		bd->draw_enabled = drawbackdropflag;
	}

	/* --- Battle id — mirrors fediskio's currentbattle global. The HD
	 * overlay uses this to resolve per-battle skybox cubemaps; outside
	 * a mission (e.g. front-end) the value still reflects whatever the
	 * pilot save last set, which is benign — the renderer only reads
	 * it during TIE_SCENE_FLIGHT. */
	TieSnapshotBuilder_SetBattleId(currentbattle);

	/* --- Hyperspace transition state ------------------------------- */
	{
		TieHyperspaceState* hs = TieSnapshotBuilder_HyperspaceMut();
		hs->phase = hyperspaceflag;
		hs->abort_flag = hyperabortflag;
		hs->hyperstar_length = (int16_t)hyperstarlength;
		/* hyperstardata polymesh: header (5 + pad 1) + point[0] at +6,
		 * point[1] at +12. The first int16 of each point is the
		 * animated x coord that anim_dohyperspace mutates. */
		hs->hyperstar_p0_x = *(const int16_t*)(hyperstardata + 6);
		hs->hyperstar_p1_x = *(const int16_t*)(hyperstardata + 12);
		hs->hyperspacedetail = (uint16_t)hyperspacedetail;
		hs->hyperticks = hyperticks;
	}

	/* SNAPSHOT-ONLY — 2D billboard queue (debris, explosion, lightning).
	 * Point and classic explosion lights are derived renderer-side from
	 * the same captured flight objects. */
	TieFlightSnapshot_EmitBillboards();
}

void TieFlightSnapshot_Capture(void) {
	TieFlightSnapshot_CaptureFrameState();
	TieFlightSnapshot_CaptureWorld();
}
