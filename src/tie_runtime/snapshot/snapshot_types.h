#ifndef TIE_RUNTIME_SNAPSHOT_TYPES_H
#define TIE_RUNTIME_SNAPSHOT_TYPES_H

#include "tie_runtime/species_id.h"

#include <stdint.h>

/* POD render state finalized after each runtime tick. Current and previous
 * slots remain valid between ticks. Values cross the boundary by value or
 * stable ID; the snapshot contains no engine pointers. */

#ifdef __cplusplus
extern "C" {
#endif

/* Actor and film lists are truncated to these snapshot capacities. */
#define TIE_MAX_ACTORS_2D 128
#define TIE_MAX_FILMS_2D 64
#define TIE_MAX_FLIGHT_OBJECTS 120
#define TIE_MAX_STATIC_OBJECTS 64
#define TIE_MAX_HUD_INSTRUMENTS 95
#define TIE_MAX_RADAR_BLIPS 48

/* Semantic IDs for the fixed 95-entry cockpit geometry table. */
typedef enum TieHudInstrumentId {
	TIE_HUDI_RADAR_LEFT = 0,
	TIE_HUDI_RADAR_RIGHT = 1,
	TIE_HUDI_CMD_3D_CRT = 2,
	/* 3..10: laser-LED row, one slot per active weapon group (engine
	 * caps in practice — all shipped craft have weapon_group_cnt ≤ 8). */
	TIE_HUDI_LASER_LED_FIRST = 3,
	TIE_HUDI_LASER_LED_LAST = 10,
	/* 11..14: missile-hardpoint ready levers (hp_idx 0..3). Engine writes
	 * via panel_updatehardpoint → panel_updatelever(hp_idx + 11, ...). */
	TIE_HUDI_MISSILE_HP_FIRST = 11,
	TIE_HUDI_MISSILE_HP_LAST = 14,
	/* 15..18: ammo-digit positions next to each hardpoint (text-pass). */
	TIE_HUDI_MISSILE_AMMO_FIRST = 15,
	TIE_HUDI_MISSILE_AMMO_LAST = 18,
	/* Forward / rear shield bars (lo + hi pair per half). */
	TIE_HUDI_SHIELD_FWD_NORMAL = 19,
	TIE_HUDI_SHIELD_FWD_OVER = 20,
	TIE_HUDI_SHIELD_REAR_NORMAL = 21,
	TIE_HUDI_SHIELD_REAR_OVER = 22,
	/* Hull-damage indicator — PANEL_updateshields balance section. */
	TIE_HUDI_HULL_DAMAGE_LEVER = 23,
	TIE_HUDI_SPEED_DIGITS = 24,
	TIE_HUDI_THROTTLE_DIGITS = 25,
	/* Power-distribution sliders (PANEL_updatepower). */
	TIE_HUDI_POWER_BALANCE = 26, /* engine residual */
	TIE_HUDI_POWER_LASERS = 27,
	TIE_HUDI_POWER_SHIELDS = 28,
	TIE_HUDI_POWER_BEAM = 29,
	TIE_HUDI_CLOCK_DIGITS = 30,
	TIE_HUDI_REC_LED = 31,
	TIE_HUDI_REC_PCT = 32,
	TIE_HUDI_VIEW17_TITLE = 33,
	TIE_HUDI_BEAM_ARC = 35,
	TIE_HUDI_GUNSIGHT = 36,
	/* 37..44: per-weapon-group fire-status lever. */
	TIE_HUDI_WEAPON_FIRE_FIRST = 37,
	TIE_HUDI_WEAPON_FIRE_LAST = 44,
	/* 45..57: 13 subsystem damage-crack overlays. */
	TIE_HUDI_DAMAGE_CRACK_FIRST = 45,
	TIE_HUDI_DAMAGE_CRACK_LAST = 57,
	/* CMD-readout target text/digits. */
	TIE_HUDI_TARGET_SUBSYSTEM_PCT = 58,
	TIE_HUDI_TARGET_DIST_KM_INT = 59,
	TIE_HUDI_TARGET_DIST_KM_FRAC = 60,
	TIE_HUDI_TARGET_SHIELD_PCT = 61,
	TIE_HUDI_TARGET_HULL_PCT = 62,
	TIE_HUDI_TARGET_CARGO = 63,
	TIE_HUDI_TARGET_SUBSYSTEM_FOCUS = 65,
	/* Weapon-warning LEDs. */
	TIE_HUDI_WARN_INCOMING = 66,
	TIE_HUDI_WARN_LOCK = 67,
	TIE_HUDI_WARN_IMPACT = 68,
	/* Threat-view target weapons + percentages. */
	TIE_HUDI_THREAT_DIST_KM_INT = 71,
	TIE_HUDI_THREAT_DIST_KM_FRAC = 72,
	TIE_HUDI_THREAT_ION = 73,
	TIE_HUDI_THREAT_TORP = 74,
	TIE_HUDI_THREAT_MISSILE = 75,
	TIE_HUDI_THREAT_BEAM = 76,
	TIE_HUDI_THREAT_SHIELD_PCT = 77,
	TIE_HUDI_THREAT_HULL_PCT = 78,
	/* Drop-down covers painted by PANEL_updatecovers when the
	 * corresponding subsystem is inactive (subsystem_active bit clear).
	 * Cover the shield-LED zone (83, gated on SF_SHIELDS) and the
	 * beam-charge zone (84+85, gated on SF_TRACTOR_BEAM). */
	TIE_HUDI_COVER_SHIELDS = 83,
	TIE_HUDI_COVER_BEAM_UP = 84,
	TIE_HUDI_COVER_BEAM_DOWN = 85,
	/* Beam fire-status lever. */
	TIE_HUDI_BEAM_FIRE = 91,
} TieHudInstrumentId;
#define TIE_MAX_EVENTS 128
/* Per-craft component pool. Upper bound is 120 craft × ~12 meshes
 * = 1440; pinned at 2048 to absorb variability across species. The
 * pool is flat; each TieFlightObjectState carries
 * (component_start, component_count) indices into it. */
#define TIE_MAX_FLIGHT_COMPONENTS 2048

/* Per-tick billboard queue. The classic engine has a 32-slot drawitems[]
 * cap; we double to 64 so debris + active explosion sprites + lightning
 * bolts can coexist without trimming when detail levels rise. */
#define TIE_MAX_BILLBOARDS 64

/* Per-tick hyperspace-streak queue. Engine maintains 64 static-object
 * slots and uses them as streak seeds during hyperspace phases 3 & 5
 * (anim.c case 2 reseeds positions; tie_updatescreen's static loop
 * draws each via draw_drawhyperstar). The slot count cap is the
 * hardware-detail-setting-driven `hyperspacedetail` (max 64). */
#define TIE_MAX_HYPERSTARS 64
#define TIE_MAX_STARS 768

/* Title-crawl cap matches title.c's MAX_LINES (18 lines). */
#define TIE_MAX_TITLE_CRAWL_LINES 18
#define TIE_TITLE_CRAWL_MAX_CHARS 80 /* mirrors title.c draw_Back stack buffer */

/* Engine frame plus fractional progress for smooth scene animation.
 * Zero values mean no scene clock is active. */
typedef struct TieSceneClock {
	int32_t engine_frame;
	float frame_progress;
	/* Cel period in microseconds at the active scene's frame rate
	 * (frame_rate_gbl × 4 × 1000). Ships zero outside FILM scenes
	 * or when no frame rate is set. Consumers that quantize
	 * frame_progress to a fixed Hz grid (e.g. cutscene compositor
	 * with manifest `interp_rate: 24`) divide cel_period_us by
	 * (1e6 / target_hz) to get updates-per-cel. */
	uint32_t cel_period_us;
} TieSceneClock;

/* Draw-list cap. Captures imperative actor draw calls (DELT/ANIM/RAW)
 * issued from outside the standard actor pass — the dialog-driven UI
 * code in computer.c, register.c, etc. draws the same Actor* multiple
 * times per frame at different positions/states, which actors_2D (one
 * record per Actor) can't represent. The standard actor pass also
 * emits into this list so a single draw-list captures the whole frame
 * for both film-driven and free-form scenes. */
#define TIE_MAX_DRAWS_2D 256
#define TIE_MAX_UI_TEXTS 64
#define TIE_UI_TEXT_MAX_CHARS 80
#define TIE_MAX_PAINT_CMDS 128

/* Brief-map channel caps. Sized to engine static caps:
 *   icons      = up to 48 active flight groups (EFArrayStruct.fg[48])
 *   targets    = 8 simultaneous target slots (brief.target_*[8])
 *   text overlays = 8 simultaneous text slots (brief.text_*[8])
 *   paragraphs = 0..2 (top + bottom paragraph slots) */

/* ===== Scene kind =====
 *
 * Coarse classifier so a renderer can pick the right scene graph
 * (3D space vs 2D briefing canvas). Set by task-push helpers as they
 * push the corresponding modal: tie_Push_FlightMission_Task →
 * TIE_SCENE_FLIGHT, brief_Push_* → TIE_SCENE_BRIEFING, etc.
 * Default after tie_init = TIE_SCENE_FRONTEND. */
typedef enum {
	TIE_SCENE_FRONTEND = 0, /* register / mainmenu / TOURDESK / options */
	TIE_SCENE_BRIEFING = 1,
	TIE_SCENE_HANGAR = 2,
	TIE_SCENE_FLIGHT = 3, /* in-mission space */
	TIE_SCENE_DEBRIEF = 4,
	TIE_SCENE_CUTSCENE = 5,       /* film-only screens (TIELOGO / debrief film) */
	TIE_SCENE_FLIGHT_LOADING = 6, /* classic flight framebuffer, no 3D remaster */
} TieSceneKind;

/* Full-screen presentation nested inside a flight scene. The information-room
 * values follow USER_inflightinfo's seven-page carousel, shifted by one so
 * zero remains the normal in-flight view. */
typedef enum TieFlightScreen {
	TIE_FLIGHT_SCREEN_NORMAL = 0,
	TIE_FLIGHT_SCREEN_GOALS = 1,
	TIE_FLIGHT_SCREEN_MAP = 2,
	TIE_FLIGHT_SCREEN_MESSAGES = 3,
	TIE_FLIGHT_SCREEN_DAMAGE = 4,
	TIE_FLIGHT_SCREEN_WINGMEN = 5,
	TIE_FLIGHT_SCREEN_HELP = 6,
	TIE_FLIGHT_SCREEN_OPTIONS = 7,
	TIE_FLIGHT_SCREEN_REPLAY_PROMPT = 8,
	TIE_FLIGHT_SCREEN_REPLAY_VIEWER = 9,
} TieFlightScreen;

/* Classic flight-renderer behavior selected for this snapshot. This is
 * renderer-visible convention state, not an asset-source identifier. */
typedef enum TieFlightLegacyRenderConvention {
	TIE_FLIGHT_LEGACY_RENDER_TIE95 = 0,
	TIE_FLIGHT_LEGACY_RENDER_TIE98_SOFTWARE = 1,
	TIE_FLIGHT_LEGACY_RENDER_TIE98_D3D = 2,
} TieFlightLegacyRenderConvention;

typedef enum TieSourcePixelAspect {
	TIE_SOURCE_PIXEL_ASPECT_SQUARE = 0,
	TIE_SOURCE_PIXEL_ASPECT_VGA_4_3 = 1,
} TieSourcePixelAspect;

/* RT clear cadence — independent of compositing. Both modes use the
 * same overlay-on-classic compositing rule (transparent RT clear,
 * PMA-blend over classic FB, Tab-toggle gates the HD layer's alpha).
 * The difference is purely about when the RT is cleared:
 *
 *   FULL_FRAME : cutscenes redraw every actor every tick, so the RT
 *                clears at the START of each frame and rebuilds from
 *                scratch. Animations don't trail.
 *   INCREMENTAL: UI uses dirty-rect refresh; most frames have zero
 *                draws_2D entries. RT clears ONCE per scene_tag
 *                transition and persists thereafter; per-frame
 *                draws_2D / ui_texts / paint_cmds emits update only
 *                the changed regions. Mirrors classic FB's update
 *                model in HD.
 */
typedef enum {
	TIE_REDRAW_INCREMENTAL = 0, /* default — most scenes are UI */
	TIE_REDRAW_FULL_FRAME = 1,
} TieRedrawModel;

/* Routing target for emitted draws_2D / paint_cmds / ui_texts records.
 *
 * Most engine emits land on the main cutscene RT. The brief-map widget's
 * polygon-warp path (SCENE_BRIEF) is the exception: classic redirects
 * all widget drawing to a 320×200 brief_buffer scratch and then memcpy-
 * warps that scratch onto the briefing-room polygon. The HD renderer
 * mirrors this — emits made while the widget's scratch canvas is bound
 * are tagged TIE_EMIT_TARGET_BRIEF_SOURCE so the application routes them onto
 * a transient HD source RT (which the polygon-warp quad then samples).
 *
 * Tagging happens at record-allocation time (lactor_emit_draw,
 * emit_paint, lfont_Print_Clipped_Text) — the tag is sourced from the
 * canvas module via lcanvas_Render_Emit_Target(). The TIE Landru adapter
 * maps Landru's auxiliary target to BRIEF_SOURCE and its screen target to
 * CUTSCENE. */
typedef enum {
	TIE_EMIT_TARGET_CUTSCENE = 0,     /* default — main cutscene RT */
	TIE_EMIT_TARGET_BRIEF_SOURCE = 1, /* brief-map source RT */
} TieEmitTarget;

/* Genus values mirror src/tie/tie.h enum Genus — shipped through the
 * snapshot so the renderer can dispatch on actor kind without
 * including the engine internal header. */
typedef enum {
	TIE_GENUS_FIGHTER = 0,
	TIE_GENUS_TRANSPORT = 1,
	TIE_GENUS_UTILITY = 2,
	TIE_GENUS_FREIGHTER = 3,
	TIE_GENUS_STARSHIP = 4,
	TIE_GENUS_PLATFORM = 5,
	/* Shooter-discriminators, not weapon-type tags — see comment on
	 * GENUS_PROJECTILE_PLAYER / GENUS_PROJECTILE_NPC in tie.h. */
	TIE_GENUS_PROJECTILE_PLAYER = 6,
	TIE_GENUS_PROJECTILE_NPC = 7,
	TIE_GENUS_MINE = 8,
	TIE_GENUS_DEBRIS = 11,
	TIE_GENUS_EXPLOSION = 13,
	TIE_GENUS_GATE = 14,
} TieGenus;

/* Renderer-visible flight-object flags. Subset of the engine's
 * internal state flattened into one bitmask so the renderer can
 * branch without parsing CraftData. */
enum {
	TIE_FOBJ_HYPER = 1u << 0,     /* hyperspace transition active */
	TIE_FOBJ_EJECTING = 1u << 1,  /* eject-pod animation underway */
	TIE_FOBJ_DESTROYED = 1u << 2, /* death_timer counting down */
	TIE_FOBJ_INVISIBLE = 1u << 3, /* slot occupied but not drawn (debris cycle) */
};

/* 3D coordinate contract:
 *   world: +X right, +Y forward, +Z up
 *   eye:   +X right, +Y down, +Z forward; visible depth is positive
 *   craft: (side, forward, up), a right-handed rotation
 *
 * Mesh rendering uses the reflected (side, -forward, up) basis. Snapshot
 * quaternions encode the proper craft rotation; mesh consumers negate its
 * second column. Camera orientation encodes world-to-eye rotation.
 *
 * World positions remain signed native integers. Subtract an integer view
 * origin before float conversion. Q15 components divide by 32768, raw mesh
 * vertices use half a native world unit, and one world unit is 1600/65536 m. */

/* ===== Per-actor state =====
 *
 * id is stable across ticks and slot reuse — sourced from
 * FlightObject.idnumber (monotonic per spawn). Renderers use it to
 * key their pooled scene nodes. */
typedef struct TieFlightObjectState {
	uint32_t id;
	uint16_t slot;    /* objects[] index, 0..TIE_MAX_FLIGHT_OBJECTS-1 */
	uint8_t genus;    /* TieGenus */
	uint8_t ship_idx; /* TieSpeciesId value; fixed-width snapshot ABI */
	/* Mesh-component debris resolution. When create_blowoffcomponent
	 * spawns a wing/engine chunk, the FlightObject is recorded with
	 * ship_idx = TIE_SPECIES_DEBRIS_MESH and
	 * stashes the parent's species in ship_type_override and the
	 * source submesh in anim_frame (= 2 * mesh_idx). The classic
	 * renderer reads both fields to draw a single submesh of the
	 * parent's polymesh at the chunk's pose; the HD renderer needs
	 * the same data to dispatch the same draw. Zero on non-debris
	 * objects. */
	uint8_t parent_ship_idx;
	uint8_t submesh_idx;
	uint8_t side;         /* IFF (0..3 imperial/rebel/neutral/etc.) */
	uint8_t damage_state; /* hull-plate / sprite frame */
	uint16_t fg_idx;      /* owning flight group */
	uint16_t flags;       /* TIE_FOBJ_* */
	int32_t world_pos[3]; /* absolute native engine world units */
	/* Craft orientation. Quaternion (w, x, y, z) encoding the
	 * craft-to-world rotation whose columns are (side, fwd, up).
	 * This is a valid rotation (det = +1). Renderers reproducing
	 * the engine's vertex math must post-multiply by a y-axis
	 * reflection — the engine's effective render basis is
	 * (side, -fwd, up); see "Engine basis conventions" above. */
	float ori[4];
	int16_t current_speed; /* throttle scalar 0..3600 (raw) */
	int16_t death_timer;   /* ticks until destroy anim end (0 = alive) */
	/* 0 = none, 1 = beam target, 3 = player target during blink-on.
	 * Component overrides use TieFlightObjectComponent.flags bit 2. */
	uint8_t highlight;
	/* Lightning arc on critically damaged craft. Reads
	 * craftptr->mesh_state[num_meshes] — the slot one past the
	 * per-mesh state range, overlaid as the bolt anim frame counter.
	 * 0..24 = active bolt frame from lightning[25] atlas;
	 * >=25 means no lightning. 0xFF when the craft has no CraftData
	 * (debris/explosion slots). */
	uint8_t lightning_state;
	/* Per-craft decal and marking tint selector from FlightObject.
	 * decal_color, set once at creation from FG.camoflage. Indexes
	 * the marking-material palette ramp the HD shader applies to
	 * decal sub-polygons. */
	uint8_t decal_color;
	uint8_t model_variant; /* material variant selected by model logic */
	/* FlightObject.anim_frame copy — polymorphic per genus:
	 *   GENUS_EXPLOSION: frame index into the engine's range table
	 *     (0..11 typical) used by the renderer-side light-source
	 *     policy to pick the animation-dependent intensity.
	 *   GENUS_DEBRIS (ship_idx 89): encodes the source submesh as
	 *     (anim_frame >> 1); already decoded into submesh_idx above
	 *     but the raw value is retained for completeness.
	 *   Other genera: whatever the engine stashed there (animation
	 *     phase counter, mostly). */
	uint8_t anim_frame;
	/* Indices into TieSnapshot.flight_components[] for this craft's
	 * articulated mesh state. component_count == 0 means no
	 * components (debris, lasers, missiles). When >0 the renderer
	 * walks [component_start, component_start + component_count). */
	uint16_t component_start;
	uint16_t component_count;
} TieFlightObjectState;

/* Per-craft articulated mesh state. One record per visible
 * mesh of a craft. Sized for the LOD-controlled mesh count
 * (species-dependent; peak ~12). The rotation is normalized to the
 * visible convention of the selected classic engine. */
#define TIE_FLIGHT_COMPONENT_TIE95_TRAINING_ROTATION 0x08u

typedef struct TieFlightObjectComponent {
	uint8_t mesh_idx;       /* index inside ship-model mesh table */
	uint8_t state;          /* mirrors classic CraftData.mesh_state[mesh_idx]:
							 * 0 = visible, 2 = hidden (killed-in-place or
							 * scripted-hidden), 4 = blown off as debris.
							 * See enum MeshState in tie.h. */
	int16_t rotation_angle; /* renderer-visible 16-bit BAM */
	uint8_t flags;          /* bit 0 = visible (state == MESH_STATE_VISIBLE)
							 * bit 1 = articulated (has rotation_offset)
							 * bit 2 = component-highlight active (matches
							 *         classic currenttargetcomp resolution)
							 * bit 3 = TIE95 training synthetic rotation */
	uint8_t hp_remaining;   /* mirrors CraftData.mesh_component_hp[mesh_idx]:
							 * 0 = destroyed, 0xFF = indestructible, else live
							 * HP. Use with TieRecoveredData_MeshTypeInitialHp() to
							 * derive a 0..1 damage ratio for the OPT
							 * multi-state texture path. */
	/* Exact pivot produced by TIE95 fview_componentrotation, in classic
	 * transformed-coordinate units. Valid only when flags bit 3 is set. */
	int16_t tie95_training_pivot_fwd;
} TieFlightObjectComponent;

/* Classic drawitems billboard captured for HD composition.
 * parent_kind values:
 *   0 = TIE_BILLBOARD_FLIGHT     (objects[parent_slot] is the source;
 *                                 used for GENUS_DEBRIS + GENUS_EXPLOSION)
 *   1 = TIE_BILLBOARD_STATIC     (staticobjects[parent_slot] is the source)
 *   2 = TIE_BILLBOARD_LIGHTNING  (objects[parent_slot] is the source craft;
 *                                 the bolt sprite is rendered at the craft's
 *                                 world origin per anim_draw_bitmap, NOT at
 *                                 the damaged fuselage mesh's offset)
 * rotation_bam is the classic post-orientation screen-space roll. */
typedef struct TieBillboardState {
	int32_t world_pos[3]; /* parent's absolute native world position */
	uint16_t parent_slot;
	int16_t rotation_bam;    /* 16-bit BAM, signed; 16-bit binary angle the
							  * engine computed from its eye-space basis */
	uint16_t pixel_scale_q8; /* rotscale_calcscale OUTPUT (Q8.8): the pixel
							  * multiplier the engine's ROTSCALE would have
							  * applied to the sprite's native dims at this
							  * eye_z. Already factors in bound_hwidth, the
							  * damage ramp, and the engine's 1024 max clamp. */
	uint16_t bound_hwidth;   /* species_table[bitmap_species].bound_hwidth, in
							  * engine world units. Used by the HD billboard
							  * pass to bias the sprite's effective eye_z
							  * forward (toward camera) by ~the sprite radius
							  * — replicates classic's "flat object inside
							  * mesh's bbox wins" rule (xtrans2.c:488-520)
							  * without needing per-pixel bbox data. */
	uint8_t parent_kind;
	uint8_t species_idx;
	uint8_t bitmap_idx;
	uint8_t genus; /* TieGenus of the parent FlightObject at
					* capture time. The capture gate only emits
					* TIE_GENUS_DEBRIS or TIE_GENUS_EXPLOSION;
					* TIE_BILLBOARD_LIGHTNING captures set this
					* to 0 (parent is a craft slot whose own
					* genus isn't informative — discriminate via
					* parent_kind == TIE_BILLBOARD_LIGHTNING). */
} TieBillboardState;

enum {
	TIE_BILLBOARD_FLIGHT = 0,
	TIE_BILLBOARD_STATIC = 1,
	TIE_BILLBOARD_LIGHTNING = 2,
};

/* Hyperspace streak seed emitted in phases 3 and 5. Endpoints are shared
 * through TieHyperspaceState; slot modulo four selects palette entry 252..255. */
typedef struct TieHyperstar {
	int32_t world_pos[3]; /* restored absolute native world position */
	uint8_t slot;         /* 0..63, drives the per-shade palette
						   * index via (slot & 3). */
} TieHyperstar;

/* One source star from TIE's three cube-face lobes. `axis` is a
 * camera-independent world-space direction axis: the classic renderer
 * negates the complete eye vector when it points behind the camera, so
 * +axis and -axis intentionally identify the same star. `palette_slot`
 * selects the live palette entry 252..255; it is a colour selector, not
 * a monochrome brightness value. */
typedef struct TieStarDirection {
	float axis[3];
	uint8_t palette_slot;
	uint8_t _pad[3];
} TieStarDirection;

typedef struct TieStaticObjectState {
	uint32_t id;
	uint16_t slot;
	uint8_t ship_class; /* 8=mine,9=planet,10=asteroid,11/12=backdrop,13=expl,14=gate */
	uint8_t species;    /* 0 = free slot (filtered out) */
	uint8_t fg_idx;
	uint8_t anim_frame; /* polymorphic per ship_class */
	/* Per-static palette variant from fgstatus[fg_idx]
	 * .version, 0..7. Picks one of 8 entries in planetpalptrs[] for
	 * the 16-entry palette remap LUT applied to planet sprites. Set
	 * once at mission load. HD authoring: one texture variant per
	 * (planet_species, palette_version) pair. */
	uint8_t palette_version;
	uint8_t model_visible;
	int32_t world_pos[3];
	float ori[4];
	uint16_t status_flags; /* 10-bit subsystems */
} TieStaticObjectState;

/* 2D landru actor — front-end UI layer. (res_type, res_name) is the
 * stable key; landru's Actor.id is not globally unique. */
/* 2D actor flags exposed to renderers. The TIE Landru adapter maps these
 * explicitly from LandruActorRenderFlags. */
enum {
	TIE_ACTOR2D_VISIBLE = 0x0001,
	TIE_ACTOR2D_ACTIVE = 0x0002,
	TIE_ACTOR2D_HFLIP = 0x0100,
	TIE_ACTOR2D_VFLIP = 0x0200,
	/* AF_REMAP_COLOR equivalent — the engine sets this on icon_actors[0]
	 * (iconsgrn) inside player_Draw_Display_Ship's target loop to render
	 * the cel as a flat-color silhouette using fore_color as the paint
	 * index. Renderer reads fore_color from the snapshot record and
	 * applies tint=(0,0,0,1) bias=palette[fore_color]. */
	TIE_ACTOR2D_REMAP_COLOR = 0x0400,
};

typedef struct TieActor2DState {
	uint32_t res_type; /* FOURCC */
	char res_name[8];
	int16_t id;
	/* FilmObject array index that owns this actor; -1 when the actor
	 * wasn't instantiated by the FILM loader. Lets cutscene manifests
	 * disambiguate multiple instances of the same DELT/ANIM resource
	 * within a single film. */
	int16_t film_entry_index;
	/* Natural classic-coordinate bounds before actor scaling. Renderers use:
	 *     scaled_w  = w * xscale / 256
	 *     scaled_h  = h * yscale / 256
	 *     scaled_x  = x + (w - scaled_w) / 2     (recenter)
	 *     scaled_y  = y + (h - scaled_h) / 2
	 */
	int16_t x, y;
	int16_t w, h;
	int16_t zplane;
	int16_t state;  /* current cel */
	uint16_t flags; /* TIE_ACTOR2D_* */

	/* Watcom Q8 actor scale (256 = identity, set by lactor_Set_Actor_
	 * Scale). Renderers re-apply via the formulas above. The vast
	 * majority of actors stay at 256/256 for their whole lifetime;
	 * only a handful of legacy effects (title.c's Star Wars logo
	 * zoom) animate non-identity scales. */
	int16_t xscale, yscale;

	/* Clip rect in classic-coord screen space — set by FILM
	 * ACTOR_CLIP, defaults to the full 320×200 canvas. Renderers
	 * intersect their dst rect with this and skip pixels outside.
	 * Stored as (left, top, right, bottom) so 0-area rects are
	 * trivially detectable.
	 *
	 * No-clip sentinel convention (shared with TieDraw2D /
	 * TieUIText / TiePaintCmd): unclipped iff `clip_right <=
	 * clip_left || clip_bottom <= clip_top`. Do NOT use INT16_MAX. */
	int16_t clip_left, clip_top, clip_right, clip_bottom;

	/* Cel-start state for cutscene smoothing:
	 *     render_x = lerp(prev_x, x, scene_clock.frame_progress)
	 * prev_xv/yv are the velocity that produced this cel; xv/yv are the
	 * post-script velocity for the next cel. */
	int16_t prev_x, prev_y;
	int16_t prev_xv, prev_yv;
	int16_t xv, yv;
	int16_t xvf, yvf;

	/* Scale (Q8, 256 = identity) captured at the start of the
	 * most recent Move_Actor — alongside prev_x/y. Lets the
	 * cutscene compositor lerp xscale/yscale at the same
	 * frame_progress as position so user-callback scale
	 * animations (title.c's Star-Wars-logo zoom) render smoothly
	 * instead of stepping once per cel. */
	int16_t prev_xscale, prev_yscale;

	/* Palette index used by TIE_ACTOR2D_REMAP_COLOR. Only meaningful
	 * when the flag is set; renderers ignore it otherwise. */
	int16_t fore_color;
} TieActor2DState;

typedef struct TieFilm2DState {
	uint32_t res_type;
	char res_name[8];
	int16_t x, y;
	int16_t zplane;
	uint16_t cur_cel;
	uint16_t cels;
	uint16_t flags;
} TieFilm2DState;

/* Static title-crawl line. The application scrolls the cached line stack
 * from initial_y using the scene clock and scene tag. */
typedef struct TieTitleCrawlLine {
	char text[TIE_TITLE_CRAWL_MAX_CHARS]; /* NUL-terminated */
	/* Initial classic-coord y for this line, fixed for the whole
	 * scene. Line i's value is the engine's `start + 28*i + 200`
	 * formula at scene init, which depends on the scene type
	 * (SCENE_TITLE on fast system → start=100; todtxtN → start=1).
	 * The application scrolls all lines uniformly:
	 *     current_y = initial_y - elapsed_engine_frames * 1.0
	 */
	float initial_y;
	uint8_t font_id;
	uint8_t font_domain; /* TieFontDomain */
} TieTitleCrawlLine;

typedef enum TieFontDomain {
	TIE_FONT_DOMAIN_LANDRU = 0,
	TIE_FONT_DOMAIN_TITLE,
	TIE_FONT_DOMAIN_COCKPIT,
} TieFontDomain;

/* Single imperative actor-draw command captured from the engine's draw
 * functions. (x, y) is the position passed to the draw call (xoff/yoff
 * in landru's lact*_Draw_*_Actor signatures), NOT the actor's struct
 * x/y which is unused for dialog-driven UI. (w, h) come from the
 * actor's current frame bounds; (state) from the actor's cel index at
 * the moment of the draw. z_order is a monotonic per-frame counter so
 * the renderer can replay back-to-front. */
typedef struct TieDraw2D {
	uint32_t res_type; /* DELT / ANIM / RAW FOURCC */
	char res_name[8];
	int16_t film_entry_index; /* -1 for direct allocations */
	int16_t x, y;             /* draw-call position, classic coords */
	int16_t w, h;             /* current frame dims */
	int16_t state;            /* cel for ANIMs */
	/* Canvas-clip rect (classic-px). No-clip iff
	 * `clip_right <= clip_left || clip_bottom <= clip_top`. */
	int16_t clip_left, clip_top, clip_right, clip_bottom;
	uint16_t flags;     /* TIE_ACTOR2D_* */
	int16_t fore_color; /* palette index used by REMAP_COLOR */
	int16_t z_order;    /* fills in emit order; 0..count-1 */
	uint8_t target;     /* TieEmitTarget routing tag */
} TieDraw2D;

/* Captured Landru text draw. Bytes 0x01 and 0x02 toggle between the normal
 * and bold palette indices; shadow state is captured separately. */
typedef struct TieUIText {
	char text[TIE_UI_TEXT_MAX_CHARS]; /* NUL-terminated */
	int16_t x, y;
	uint8_t color_index;
	uint8_t bold_color_index;
	uint8_t shadow_color_index;
	uint8_t shadow; /* 0/1 — `s_cur_font->shadow` at draw time */
	uint8_t font_id;
	uint8_t font_domain; /* TieFontDomain */
	uint8_t target;      /* TieEmitTarget routing tag */
	int16_t z_order;
	/* `background` mirrors the engine's `setbackcolor` semantics for
	 * cockpit/HUD text: rtsvga2_outchar*VGA fills every non-ink pixel
	 * of each glyph cell with `backcolor` (= remap(setbackcolor input)).
	 * When `background` is set, the renderer paints a solid
	 * `background_color_index` strip spanning the full text run
	 * (advance_total × font_cell_h) BEFORE the glyph quads — the
	 * engine-equivalent of those non-ink fills. When `background`
	 * is 0 (lfont snapshot hook, cutscene subtitles, etc.) only the
	 * inked pixels render. Since the strip uses each record's own
	 * extent, callers emitting two adjacent records (e.g. MM and SS
	 * in panel_updateclock) get an unpainted gap between them where
	 * the underlying bitmap shows through. */
	uint8_t background;
	uint8_t background_color_index;
	/* Classic-pixel clip rectangle. An empty or inverted rectangle disables
	 * clipping; bounds must remain within the source canvas. */
	int16_t clip_left, clip_top, clip_right, clip_bottom;
} TieUIText;

/* Cursor coordinates locate its hotspot; hot_x/hot_y locate that point within
 * the bitmap. Cursor state is separate from actor and text draws. */
typedef enum {
	TIE_CURSOR_POINTER = 0,
	TIE_CURSOR_WAIT = 1,
	TIE_CURSOR_NONE = 2,
} TieCursorKind;

typedef struct TieCursorState {
	int16_t x, y;
	int16_t hot_x, hot_y;
	int16_t w, h;
	uint8_t visible;
	uint8_t kind; /* TieCursorKind */
} TieCursorState;

/* HD color-fade state. With normalized hd_factor and target color, composite:
 *   tint.rgb = (hd_factor/255) * blend_alpha
 *   tint.a   =                   blend_alpha
 *   bias.rgb = (target_rgb/255) * (1 - hd_factor/255) * blend_alpha
 *   out.rgb  = sample.rgb * tint.rgb + bias.rgb * sample.a
 *   out.a    = sample.a   * tint.a
 * A black target reduces to alpha modulation. */
typedef enum {
	TIE_FADE_NONE = 0,
	TIE_FADE_ACTIVE = 1,
} TieFadeKind;

typedef struct TieFadeState {
	uint8_t kind;      /* TIE_FADE_NONE or TIE_FADE_ACTIVE */
	uint8_t hd_factor; /* 255 = HD untouched, 0 = HD fully replaced by (r,g,b) */
	uint8_t r, g, b;   /* fade target color (engine palette space, 0..255) */
	/* Preserve the previous cutscene RT while the fade modifies its tint. The
	 * first unfrozen frame clears stale content before drawing the new scene. */
	uint8_t freeze_overlay;
} TieFadeState;

typedef enum {
	TIE_MAP_BG_STARS = 0,          /* "stars" delta, var1=16  */
	TIE_MAP_BG_COMBAT = 1,         /* "cmbtmap2" delta, var1=17 (combat-A tint) */
	TIE_MAP_BG_COMBAT_DEBRIEF = 2, /* "cmbtmap2" delta, var1=16 (combat-B/C/D/E) */
	TIE_MAP_BG_TRAINING = 3,       /* training-map variant */
} TieMapBgKind;

/* Brief-map widget — minimal control state shipped to the application. The
 * widget content (grid lines, ship icons, target reticles, readout
 * text, paragraph strips) flows through the standard snapshot channels
 * (paint_cmds, draws_2D, ui_texts) tagged by lcanvas_Snapshot_Emit_
 * Target — the application renders them via the merge dispatcher and
 * the map renderer pipes the polygon-warp path's records onto its source RT.
 * What stays here is just the wrapper geometry the application can't get
 * from those channels: the backdrop kind, the source-rect dims used
 * by the polygon UV inset, the destination geometry (rect or quad4),
 * and the merge-dispatch z slot. */
typedef struct TieMapHeader {
	uint8_t active;      /* 0 = no map this tick */
	uint8_t bg_kind;     /* TieMapBgKind */
	uint8_t has_polygon; /* 1 = warp via dst_poly */

	/* Source-space dims (always 292×147 for current callers). The
	 * polygon-warp UV inset uses these to size its sample window. */
	int16_t src_rect_w, src_rect_h;

	/* Destination geometry. has_polygon picks one branch:
	 *   1 → dst_poly_x[4], dst_poly_y[4] (CW from TL)
	 *   0 → dst_rect_{x,y} (size == src_rect_*) */
	int16_t dst_rect_x, dst_rect_y;
	int16_t dst_poly_x[4], dst_poly_y[4];

	/* Brief-map quad's z slot in the merge dispatch — stamped at the
	 * top of idraw_Map. Engine emits tagged TIE_EMIT_TARGET_CUTSCENE
	 * by the brief widget land at z > start_z so they layer on top
	 * of the rect-path backdrop quad. */
	int16_t start_z;
} TieMapHeader;

/* Solid-color classic primitives captured for the HD overlay. SHADE_RECT
 * uses alpha as intensity and colors[0] as its blend target. */
typedef enum {
	TIE_PAINT_FILL_RECT = 0,
	TIE_PAINT_FRAME_RECT = 1,
	TIE_PAINT_HLINE = 2,
	TIE_PAINT_VLINE = 3,
	TIE_PAINT_PIXEL = 4,
	TIE_PAINT_BEVEL = 5,       /* shadow + highlight 1px frame */
	TIE_PAINT_FRAME_BEVEL = 6, /* outline-only bevel */
	TIE_PAINT_DBEVEL = 7,      /* outer + inner shadow + outer + inner highlight */
	TIE_PAINT_FRAME_DBEVEL = 8,
	TIE_PAINT_XOR_RECT = 9,
	TIE_PAINT_SHADE_RECT = 10, /* darken-toward-color rect (talk/debrief shade) */
} TiePaintOp;

/* Colors[] meaning depends on op:
 *   FILL_RECT / FRAME_RECT / HLINE / VLINE / PIXEL : colors[0] = color
 *   BEVEL / FRAME_BEVEL : colors = {shadow, highlight, fill, _, _}
 *   DBEVEL / FRAME_DBEVEL : colors = {outer_shadow, inner_shadow,
 *                                     outer_highlight, inner_highlight, fill}
 *   XOR_RECT : colors[0] = xor color
 *   SHADE_RECT : colors = {target_R, target_G, target_B, intensity, _}
 *                where target_R/G/B are 8-bit (the engine's 0..63 VGA
 *                DAC values rescaled to 0..255 — typically ALL ZEROS
 *                for darken-toward-black) and intensity is the binary's
 *                shade strength byte (0..255), used directly as the
 *                blend's alpha numerator (alpha = intensity / 256).
 * `pressed` only meaningful for the bevel ops. */
typedef struct TiePaintCmd {
	uint8_t op; /* TiePaintOp */
	uint8_t pressed;
	uint8_t colors[5];
	uint8_t target;     /* TieEmitTarget routing tag */
	int16_t x, y, w, h; /* classic-coord; HLINE: w=length, h ignored */
	int16_t z_order;
	/* Canvas-clip rect snapshot in classic-px, captured at emit time
	 * (the value of lcanvas_Get_Drawing_Canvas_Clip when the paint
	 * fired). Mirrors TieDraw2D's clip_* fields: classic engine clips
	 * each lpaint_* op against this rect via dl_rect / dl_horiz_line
	 * / dl_vert_line, the HD compositor applies it as a scissor.
	 * No-clip iff `clip_right <= clip_left || clip_bottom <= clip_top`. */
	int16_t clip_left, clip_top, clip_right, clip_bottom;
} TiePaintCmd;

/* Camera pose + projection. The captured half-FOVs describe the active
 * classic 3D aperture rather than the entire framebuffer. */
typedef struct TieCameraState {
	int32_t world_pos[3];
	/* World-to-eye rotation as a quaternion (w, x, y, z). The
	 * underlying 3x3 has rows = eye axes expressed in world coords
	 * (engine's worldeyeA/B/C from fview_newcalcview). NOTE: the
	 * engine's eye-Y axis points DOWN on the classic FB (positive
	 * eye_y maps to screen_y > halfheight per transfm2_getscreeny);
	 * renderers using HLSL/SDL_GPU NDC (+Y up) must flip Y in their
	 * projection. See "Engine basis conventions" above. */
	float ori[4];
	/* Horizontal and vertical half-FOV (each = atan(half_extent /
	 * effective_persp)). Both come from the engine's perspective
	 * setup (tie.c:736/760: perspFactor + halfpixelswide/deep, with
	 * yAspect applied to the vertical term for VGA's 5:6 non-square
	 * pixels — see transfm2_getscreeny:308-313). Renderers must use
	 * BOTH directly to match classic; deriving v from h × RT_aspect
	 * loses the cockpit-viewport aspect and shifts apparent scale. */
	float fov_h_half_rad;
	float fov_v_half_rad;
	uint16_t target_obj_slot; /* 0xFFFF = free cam */
	uint8_t pilotview;        /* 0..21 cockpit view selector */
	uint8_t zoom_active;      /* nonzero when zoom is engaged */
	int16_t view_zoom_raw;    /* engine zoom value, 48..5120 */
	/* Per-cockpit-view projection-Y offset normalized to NDC. The
	 * engine adds `transfm2_screenyoffset` (panel.c:2268) to the
	 * projected Y in transfm2_getscreeny (`screen_y = screenyoffset +
	 * halfpixelsdeep + (eyey/eyez × persp)`); a negative value shifts
	 * rendered content UP on screen so it aligns with the reticle
	 * drawn into the cockpit bitmap. Forward TIE Fighter cockpit uses
	 * -19 pixels with a 145-line viewport.
	 *
	 * Captured as a normalized NDC delta = (2 × yoffset_px /
	 * pixelsdeep_px). DOS Y-down convention carries through: negative =
	 * content shifted up on screen. The HD renderer combines this with
	 * the aperture rectangle to solve the full-output projection offset
	 * that lands the forward axis on the cockpit reticle. */
	float screen_y_offset_ndc;
	/* Active classic 3D aperture as a fraction of the classic framebuffer,
	 * captured from LOGBUF2's installed position and dimensions. The HD
	 * renderer maps this aperture into the full-height 4:3 reference to
	 * recover focal scale and optical-center placement, then extends the
	 * same projection across the full output. */
	float viewport_frac_x;
	float viewport_frac_y;
	float viewport_frac_w;
	float viewport_frac_h;
} TieCameraState;

/* Cockpit and HUD state shared with the classic panel. Zeroed when no
 * player craft is active. */
/* Radar blip — anchor-relative.
 *
 *   radar_idx  — 0 = left radar (TIE_HUDI_RADAR_LEFT), 1 = right
 *                (TIE_HUDI_RADAR_RIGHT). Renderer looks up the disc's
 *                authored anchor in the layout's instruments table.
 *   offset_x/y — signed classic-px from the disc center. Renderer
 *                composes `disc_anchor_ref + offset × (radar_radius_ref
 *                / radar_classic_radius)` so blips track per-radar
 *                geometry under hand-authored HD layouts where each
 *                disc may be relocated / resized independently. */
typedef struct TieHudBlip {
	uint16_t color;
	uint8_t radar_idx;
	int16_t offset_x;
	int16_t offset_y;
} TieHudBlip;

/* (x, y, param1, param2) mirror panel.c's HudInstrument 1:1 — the
 * static layout loaded from the .INT file (reflows on VGA↔SVGA
 * resolution switch, hence shipped per-tick). param1/param2 are
 * widget-specific layout payloads (digit count, default color,
 * shape index, etc. — see panel.h for the polysemy).
 *
 * value/color carry the current painted state (the engine's
 * oldinstruments[idx] + the festring color panel_updatevalue picked).
 * value defaults to -1 ("never painted"); color to 0. */
typedef struct TieHudInstrument {
	uint16_t x;
	uint16_t y;
	uint8_t param1;
	uint8_t param2;
	int16_t value;
	uint8_t color;
	uint8_t digits;
} TieHudInstrument;

#define TIE_MAX_WEAPON_GROUPS 12

typedef struct TieHudState {
	/* Player-craft summary. */
	int16_t hull_damage; /* 0..hull_max: cumulative damage past shields */
	int16_t hull_max;    /* kill threshold; also denominator for hull-damage lever */
	/* Subsystem loadout / status (mirrored from CraftData). 13-bit
	 * mask, both fields use the same layout. installed_subsystems is
	 * set once at create-time; working_subsystems clears bits on damage
	 * and re-sets on repair. working_subsystems ⊆ installed_subsystems. */
	uint16_t installed_subsystems;
	uint16_t working_subsystems;
	uint16_t subsystem_active; /* on/off */
	/* Engine `CraftData.status_flags` (+0x0AE). Distinct from
	 * subsystem_active. Bit 4 (0x10) = lasers powered, bit 2 (0x04) =
	 * target lock-on enabled, bit 3 (0x08) = missiles enabled, bit 0
	 * (0x01) = shields up, bit 8 (0x100) = beam armed.
	 * Consumed by panel_updatelasers / shields / beam / etc. */
	uint16_t status_flags;
	/* Active player weapon-group count (cp->weapon_group_cnt). Bounds
	 * the LASER_LED_ROW iteration at indices 3..3+N-1. */
	uint8_t weapon_group_cnt;
	uint8_t beam_type;
	/* Per-LED palette index for the 9-LED beam-arc bar (idx 35).
	 * panel_updatebeam picks one of beamcolors[0..3] per LED based on
	 * the cumulative beam_charge bucket; the snapshot mirrors those
	 * choices so the HD draw doesn't replicate the bucketing math. */
	uint8_t beam_arc_led_colors[9];
	/* Power-distribution settings (`CraftData.laser_power` / shield_power /
	 * beam_power, +0x0D1 / +0x0CE / shared bank). Each 0..4 with 2 the
	 * neutral default; F8/F9/F10 cycle them. Drive the four vertical
	 * sliders at HUD instruments 26..29 via panel_updatepower. */
	uint8_t laser_power;
	uint8_t shield_power;
	uint8_t beam_power;
	uint8_t ion_drained; /* 1 if ion_drain_timer > 0 */
	/* `pstate.player_craft->slam_active` mirror. The HUD speed (idx 24)
	 * and throttle (idx 25) fields render in 0x52 grey when this is 0,
	 * mirroring panel_updatevalue's slam-off path. */
	uint8_t slam_active;
	uint16_t throttle_speed;
	/* Targeting. */
	uint16_t target_obj_slot; /* 0xFFFF = none */
	/* panel_buildobjectname output with 0xFE+color escape pairs
	 * preserved. Two-color split: 0xFE <primary_color> SHORT_NAME
	 * [": " 0xFE <secondary_color> FG_NAME [" " <count_digit>]]. The
	 * compose_text festring path consumes 0xFE escapes inline so the
	 * application ships the raw buffer to one TieUIText record. Empty when
	 * no target / no name resolved. */
	char target_name[32];
	uint16_t target_status;
	/* UI overlays. Bracket is anchor-relative — same convention as
	 * TieHudBlip.{radar_idx,offset_x,offset_y}. It sits on top of the
	 * targeted craft's blip, so it lives on the same radar disc. */
	int16_t blipbox_x, blipbox_y;
	uint8_t bracket_present;
	uint8_t bracket_radar_idx;
	int16_t bracket_offset_x;
	int16_t bracket_offset_y;
	uint8_t blipbox_present;
	uint8_t lock_present;
	uint16_t blip_count_left;
	uint16_t blip_count_right;
	/* Engine's radar disc radius in classic-px (44 for SVGA, 18 for VGA
	 * — math2_getradarcoord's boundary table max). Renderer uses this
	 * as the denominator when mapping blip / bracket offsets into the
	 * layout's ref-frame radar radius. */
	uint16_t radar_classic_radius;
	/* Instrument records (cockpit-relative position + the two state
	 * bytes panel_updatevalue / panel_updatelever drive). One-to-one
	 * with panel.c's instruments[] table. */
	TieHudInstrument instruments[TIE_MAX_HUD_INSTRUMENTS];
	/* Digit-field state painted by panel_updatevalue, indexed by the
	 * same idx the engine uses. Value is the raw uint the engine
	 * formatted; color is the resolved festring color (CRITICAL /
	 * WARNING / grayed / param2-default) the engine picked. */
	uint16_t instrument_value[TIE_MAX_HUD_INSTRUMENTS];
	uint8_t instrument_value_color[TIE_MAX_HUD_INSTRUMENTS];
	/* Radar blips, hemisphere-flattened. */
	TieHudBlip blips_left[TIE_MAX_RADAR_BLIPS];
	TieHudBlip blips_right[TIE_MAX_RADAR_BLIPS];

	/* Pre-formatted "MM:SS" painted into instrument[30] by
	 * panel_updateclock. */
	char mission_clock_text[6];

	/* Resolved string painted into instrument[65]. Empty = no text. */
	char target_subsystem_text[24];

	/* World-space inputs for aspect-independent target and subsystem boxes. */
	uint16_t target_bound_hwidth;      /* avg of spec_data extents <<mss;
										* 1995-look HD reads this. */
	uint16_t target_bound_hwidth_1998; /* selected component extent or whole-object
										* species bound; 1998-look HD reads this. */
	int32_t target_box_center_offset_1998[3];
	uint8_t target_box_engine_ok;
	uint8_t target_box_inputs_ok;
	/* Integral native offset relative to the target ship position. */
	int32_t target_subsystem_offset[3];
	uint8_t target_subsystem_box_engine_ok;

	/* Resolved string painted into instrument[63]. Empty = no text. */
	char target_cargo[24];

	/* Threat-view (pilotview 20) resolved text — engine paints these
	 * strings into the threat-view text slots; the snapshot mirrors
	 * what the engine wrote. Empty = no text (line not painted). */
	char target_order_text[48];        /* idx 79 value */
	char target_link_target_label[32]; /* idx 80 left col */
	char target_link_name[24];         /* idx 80 right col */
	char target_link_dist_label[32];   /* idx 81 left col */
	char target_link_dist_text[12];    /* idx 81 right col, "K.FF" */
	char target_eta_label[32];         /* idx 82 left col */
	char target_eta_text[8];           /* idx 82 right col, "MM:SS" / "UNKNOWN" */

	/* Training-mission CRT replaces the standard CMD readout
	 * when mission.train_craft_type != 0. Mirrors gate_trainingupdatecrt
	 * (gate.c:858-985) + gate_updatebonuspoints (gate.c:634-665).
	 * Renderer composes 5 label+digit pairs (LEVEL / REMAIN / PASSED /
	 * TARGETS / SCORE) plus a separate MM:SS timer + 5-digit bonus
	 * row. `player_spec_num` picks the left-vs-right-of-origin layout
	 * the engine selects from pstate.player_spec_num. */
	struct {
		uint8_t active;          /* mission.train_craft_type != 0 */
		uint8_t level;           /* mission.train_level */
		uint8_t player_spec_num; /* pstate.player_spec_num */
		uint8_t timer_min;       /* mtimer_min */
		uint8_t timer_sec;       /* mtimer_sec */
		/* gate.c's bonus_countdown_active — true only while the
		 * per-section countdown task is running. HD bonus bar
		 * emits exclusively when set; outside this window the
		 * classic cockpit-bitmap paint leaves the region bare. */
		uint8_t bonus_active;
		/* FlightObject slot for the player craft. Remaster-only consumers
		 * use the slot to resolve the coherent pose in flights[]. */
		uint16_t player_object_slot;
		uint16_t gates_remaining; /* mission.train_gates_remaining */
		uint16_t gates_passed;    /* mission.train_gates_passed */
		uint16_t targets_hit;     /* mission.train_targets */
		int32_t score;            /* mission.mission_score */
		int32_t bonus;            /* mission.train_bonus */
	} training;

	/* In-flight message banner — see src/tie/msg.c. Mirrors
	 * messagequeue[0] (the currently-displayed slot) plus the
	 * msgLineTop/Bottom/Right band geometry msg_messageinit set up.
	 * msg_messageprintf has already expanded the template +
	 * argtable / messageptrs into body[] by the time we snapshot,
	 * so the HD renderer just walks the bytes — no string-table
	 * lookup needed application-side. */
	struct {
		/* SVGA cockpit-coord (or VGA classic-coord) band rectangle:
		 *   line_top..line_bottom = main message area (backcolor 0x2C)
		 *   line_top - 1          = 1-px separator strip (color 0x2D)
		 *   line_right            = x where the time-warp indicator
		 *                           begins ("T:<N>x"). */
		uint16_t line_top;
		uint16_t line_bottom;
		uint16_t line_right;
		/* messagequeue[0].template_idx != 0xFFFF. When 0 the band is
		 * still painted (backcolor + separator + time-warp) but no
		 * message text is drawn. */
		uint8_t present;
		/* messagequeue[0].msg_type (clamped 0..7; 6 represents the
		 * template-byte-≥8 case). */
		uint8_t msg_type;
		/* messagequeue[0].side — faction side used to colour
		 * msg_type==2 (event) messages via eventsidecolors[side]. */
		uint16_t side;
		/* messagequeue[0].body[] verbatim. body[0] is the raw type
		 * prefix (renderer skips it; ≥8 case keeps body[0..] as text);
		 * body[1] may carry a '0'..'3' sub-side selector for type==1;
		 * '[' / ']' bytes in the remainder are color nudges (skipped
		 * and emit a dim/brighten color step). Max 70 emitted chars;
		 * the extra trailing byte gives the renderer a guaranteed
		 * NUL terminator on a 70-char body. */
		char body[71];
		/* acceleratedtimesetting — drives the "T:<N>x" warp indicator
		 * at line_right. 1 = normal speed; 2..N = accelerated. */
		uint8_t accelerated_time;
	} msg_bar;
} TieHudState;

/* ===== Cockpit state =====
 *
 * Mirrors tie_core's panel.c PanelViewDef + panel_update3Dcrt switch so
 * the HD overlay can pick the active cockpit asset, position the 2D
 * widgets, and frame the picture-in-picture target render the same
 * way classic does. Updated once per tick by TieHudSnapshot_Capture. */
typedef struct TieCockpitState {
	uint8_t view_idx;     /* mirrors camera.pilotview */
	uint8_t panel_loaded; /* 1 if panelviewptrs[view].handle != 0 */
	int16_t view_yoffset; /* screen-Y offset (PanelViewDef.yoffset) */
	uint16_t view_x, view_y;
	uint16_t view_width, view_depth;
	char view_name[10];         /* PanelViewDef.name — LFD basename, NUL-terminated */
	char view_title[16];        /* requested PanelViewDef.title */
	uint8_t view_title_visible; /* resolved panel source is view 17 */
	uint8_t inherit_view;       /* PanelViewDef.flags & 0x80; nonzero = panel inherited */
	uint8_t mirrored_view;      /* (flags & 0xC0) == 0xC0; H-flipped variant */
	/* 3D CRT (PIP) viewport — pulled from instruments[2] and the
	 * panel_update3Dcrt resolution switch. Renderer uses
	 * (pip_x, pip_y, pip_w, pip_h) directly as the inset rect. */
	uint16_t pip_x, pip_y;
	uint16_t pip_w, pip_h;
	uint8_t pip_target_present; /* nonzero when panel_pointcamera ran this tick */
	uint8_t mask_variant;       /* 0..6 selector mirroring panel_update3Dcrt's
								 * mask-data switch (default/gunboat/tieadv7/
								 * tieadv8/spec5/spec4/missileboat). */
	uint16_t pip_target_slot;   /* obj idx routed to PIP (== hud.target_obj_slot when active) */
	float pip_cam_ori[4];
	/* PIP perspective half-FOVs (radians) derived from the engine's
	 * PIP-viewport projection math:
	 *     tan(h_half) = (pip_w / 2) / perspFactor
	 *     tan(v_half) = (pip_h / 2) / (perspFactor × yAspect/65536)
	 * The PIP rect is smaller than the main 3D viewport so these are
	 * narrower than camera.fov_*_half_rad — the engine's "telephoto"
	 * effect that frames the target inside the CMD CRT bezel.
	 * Zero when pip_target_present == 0. */
	float pip_fov_h_half_rad;
	float pip_fov_v_half_rad;
	/* Exact target-minus-camera displacement in native world units. */
	int32_t pip_back_step[3];
	/* Targeted-subsystem mesh index to highlight inside the PIP only
	 * (engine's highlightcolor=2 ramp via the highlightmapping table —
	 * see flight_mesh_classic_lut.frag.hlsl). Resolves the full
	 * engine gate: `pstate.radar_enable && target is a craft &&
	 * pstate.radar_target1 is a valid mesh index`. Mirrors the engine
	 * PIP path where panel_update3Dcrt OR's bit 0x200 into
	 * currenttarget so DRAW_drawcraft's component-highlight code
	 * promotes that one mesh's drawpol highlightcolor to 2 (and only
	 * the matching mesh — no whole-craft tint, unlike the main viewport
	 * which can also apply highlightcolor=1 for bluetarget). 0xFF
	 * means "no highlight" — PIP renders the silhouette plain. */
	uint8_t pip_subsys_idx;
	/* Resolution. Mirrors `flightResolution` so the renderer scales
	 * coords correctly. Duplicated from the snapshot header for
	 * locality on this struct. */
	uint16_t classic_w, classic_h;
	/* HUD parts atlas filename basename — `parts[0..7]` from the
	 * craft's .INT, lowercased with the trailing 'p' stripped (e.g.
	 * TIEFTRP → "tieftr"). Used by the renderer to find
	 * `<remaster_dir>/flight/cockpits/<parts_basename>_parts.ktx2`
	 * + the matching `<parts_basename>_hud_layout.yaml`. NUL-padded;
	 * empty string when no parts file is bound. */
	char parts_basename[10];
	uint16_t parts_shape_count;
} TieCockpitState;

/* ===== Backdrop / skybox set =====
 *
 * `backdrp2_backdrop` draws a skybox of up to 3 visible cube faces
 * per frame. The 64-slot pool is partitioned into 6 face ranges
 * (front/back/left/right/top/bottom) by the counts below; each slot
 * carries a species idx (planet / nebula / distant capital ship
 * sprite) and a packed byte encoding the slot's 2D position on the
 * active cube face (low 4 bits = primary axis index+sign, high 4 =
 * secondary axis index+sign). */
#define TIE_MAX_BACKDROP_SLOTS 64
typedef struct TieBackdropSet {
	uint8_t slot_pos[TIE_MAX_BACKDROP_SLOTS];     /* backdropposition[] */
	uint8_t slot_species[TIE_MAX_BACKDROP_SLOTS]; /* backdropspecies[]  */
	/* Planet slots only (slot_species 114..116): 0..7, selects the
	 * PLANET{n+1} bitmap AND planetpalptrs[n] palette — the .TIE FG
	 * `version` field, folded into species_table[].lfd_entry at load and
	 * recovered here. 0 for non-planet slots (galaxy/cluster/star, which
	 * slot_species already identifies). */
	uint8_t slot_planet_version[TIE_MAX_BACKDROP_SLOTS];
	uint16_t front_cnt, back_cnt;
	uint16_t left_cnt, right_cnt;
	uint16_t top_cnt, bottom_cnt;
	uint8_t draw_enabled; /* mirrors drawbackdropflag; cleared in hyperspace */
} TieBackdropSet;

/* Hyperspace FSM state, shared streak endpoints, detail cap, and phase clock. */
typedef struct TieHyperspaceState {
	uint8_t phase;             /* 0 = inactive, 1..6 = hyperspaceflag */
	uint8_t abort_flag;        /* hyperabortflag — set if path blocked */
	int16_t hyperstar_length;  /* drives streak length (phases 3 & 5) */
	int16_t hyperstar_p0_x;    /* near-endpoint x in the polymesh */
	int16_t hyperstar_p1_x;    /* far-endpoint x in the polymesh */
	uint16_t hyperspacedetail; /* count of active hyperstar slots (0/25/50/75) */
	uint16_t hyperticks;       /* mono-increasing per-phase tick counter */
} TieHyperspaceState;

/* One-tick transition events. param0 and param1 are kind-specific:
 *   LASER_SPAWN  : param0 = laser_kind (warhead vs cannon vs ion),
 *                  param1 = source actor slot
 *   MISSILE_LOCK : param0 = target actor slot (id), param1 = unused
 *   EXPLOSION    : param0 = explosion_kind (ship vs debris vs warhead),
 *                  param1 = unused
 *   HYPER_FLASH  : param0 = direction (0=in, 1=out), param1 = unused
 *   IMUSE_CUE    : param0 = soundId, param1 = group
 */
typedef enum {
	TIE_EVENT_LASER_SPAWN = 1,
	TIE_EVENT_MISSILE_LOCK = 2,
	TIE_EVENT_EXPLOSION = 3,
	TIE_EVENT_HYPER_FLASH = 4,
	TIE_EVENT_IMUSE_CUE = 5,
} TieEventKind;

typedef struct TieEvent {
	uint32_t kind;     /* TieEventKind */
	uint32_t actor_id; /* 0 if N/A */
	int32_t world_pos[3];
	int32_t param0, param1;
} TieEvent;

/* ===== Snapshot header =====
 *
 * Pointers are stable for the slot's lifetime — they index into the
 * slot's inline arrays. Counts are the active prefix; renderers walk
 * [0, count). */
typedef struct TieSnapshot {
	uint64_t tick; /* 0-based host-tick counter */
	/* Flight-sim frame counter — incremented once per move_moveobjects
	 * (one logical flight frame), NOT per host tick. The snapshot is
	 * emitted every host tick, but the flight sim advances on its own
	 * time-gated cadence, so consecutive host-tick snapshots can be
	 * identical. Consumers that need true inter-frame motion (e.g.
	 * motion-blur velocity) compare this between the current and previous
	 * snapshot to tell whether the sim actually advanced. */
	uint32_t flight_frame;
	uint64_t sim_time_us; /* synthetic clock now_us */
	TieSceneKind scene_kind;
	uint16_t classic_w, classic_h; /* current presented fb dims */
	uint16_t landru_coord_w, landru_coord_h;
	uint32_t landru_presentation_generation;
	uint8_t landru_pixel_aspect; /* TieSourcePixelAspect */
	uint8_t frontend_profile_id; /* TieFrontendProfileId */

	/* Active film context. Set by play1_Push_Play1_Task (and any
	 * other future film-driven scene push); cleared on pop. Cutscene
	 * compositor in the host uses (lfd_basename, film_name) as the
	 * key into its asset manifest. Both NUL-terminated; empty when
	 * no film is active. lfd_basename is stored uppercase ASCII to
	 * match retail directory layout (LOGO, SCENE1, BRIEF, ...). */
	char current_lfd_basename[16];
	char current_film_name[12]; /* 8-char res_name + slack */

	/* Generic scene-bundle key. Auto-derived as "<lfd>/<film>" by
	 * TieSnapshotBuilder_SetActiveFilm when both are non-empty; can be
	 * overridden by TieSnapshotBuilder_SetSceneTag for screens that don't
	 * have a real (lfd, film) tuple (e.g. SCENE_COMPUTER → "scene/
	 * computer"). The compositor looks up the manifest at
	 * <remaster>/<scene_tag>/manifest.yaml. Empty when no scene is
	 * tagged (legacy path before scenes are wired). */
	char scene_tag[64];

	/* RT clear cadence for the HD overlay. Cutscenes (PLAY1 / TIELOGO
	 * / etc) set FULL_FRAME on push; UI screens stay at default
	 * INCREMENTAL. Composited identically against classic FB in
	 * either case — the cadence only controls clear behaviour. */
	uint8_t redraw_model;
	/* Active full-screen flight presentation, including information rooms and
	 * replay UI, or TIE_FLIGHT_SCREEN_NORMAL for the cockpit view. */
	uint8_t flight_screen; /* TieFlightScreen */
	/* Replay-mode byte: 0 = live flight, 1 = recording-in-
	 * progress (panel still shown), 2 = viewing a recorded replay
	 * (panel suppressed). */
	uint8_t replay_mode;

	TieCameraState camera;
	/* VGA palette packed as ARGB uint32:
	 *   0xFF000000 | (R << 16) | (G << 8) | B
	 * Renderers texture-uploading the array verbatim on a
	 * little-endian host see bytes (B, G, R, A) in memory, which
	 * matches SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM — picking
	 * R8G8B8A8_UNORM would swap R and B at sample time. */
	uint32_t palette[256];

	/* Global directional light as a world-space unit vector and linear RGB. */
	float directional_dir[3];
	float directional_color[3];
	/* 0 for flat shading or 0x40 for Gouraud shading. */
	uint8_t gouraudflag;
	/* Mirrors tie_core's `drawmarkingsflag` global. 0 = no marking
	 * decals, 1 = draw them (video-options + detail-level + replay
	 * persistence; see option.c / user.c / bpflight.c). Classic
	 * additionally overlays a per-mesh override inside
	 * DRAW_drawcraft (binary @0x1b884) and ANIM_drawverysimpleobject
	 * (binary @0x104b7): species 17 wing meshes (mesh_type == 2)
	 * suppress markings when `objects[obj].side != 0`. HD applies
	 * the same override per-craft per-mesh in tie_remaster/flight/passes.c. */
	uint8_t drawmarkingsflag;
	/* Actual runtime convention after any renderer fallback. */
	TieFlightLegacyRenderConvention legacy_render_convention;

	uint16_t flight_count;
	uint16_t static_count;
	uint16_t actor_2D_count;
	uint16_t film_2D_count;
	uint16_t event_count;
	uint16_t draw_2D_count;
	uint16_t ui_text_count;
	uint16_t paint_cmd_count;
	uint16_t title_crawl_count;
	/* Total per-craft component records in this snapshot.
	 * Indexed by TieFlightObjectState.component_start/count. */
	uint16_t flight_component_count;
	/* Per-tick debris, explosion, and lightning billboards. */
	uint16_t billboard_count;
	/* Hyperspace streak seeds, present only in phases 3 and 5. */
	uint16_t hyperstar_count;
	uint16_t star_count;
	uint16_t required_model_species_count;
	uint16_t required_sprite_species_count;
	uint32_t mission_load_generation;

	TieSceneClock scene_clock;

	const TieFlightObjectState* flights;
	const TieStaticObjectState* statics;
	const TieActor2DState* actors_2D;
	const TieFilm2DState* films_2D;
	const TieEvent* events;
	const TieDraw2D* draws_2D;
	const TieUIText* ui_texts;
	const TiePaintCmd* paint_cmds;
	const TieTitleCrawlLine* title_crawl_lines;
	const TieFlightObjectComponent* flight_components;
	const TieBillboardState* billboards;
	const TieHyperstar* hyperstars;
	const TieStarDirection* stars;
	const uint16_t* required_model_species;
	const uint16_t* required_sprite_species;

	TieHudState hud;
	TieCursorState cursor;
	TieFadeState fade;
	TieMapHeader map;
	TieCockpitState cockpit;
	TieBackdropSet backdrops;
	TieHyperspaceState hyperspace;
	/* Active battle index (0..12 for the 13 stock battles, plus any
	 * secret slots). Mirrors tie_core's `currentbattle` global. Used
	 * by the HD overlay to resolve <remaster_dir>/flight/skyboxes/
	 * battle<N>.ktx2 (or any other per-battle asset selector). */
	uint8_t battle_id;
} TieSnapshot;

#ifdef __cplusplus
}
#endif

#endif /* TIE_RUNTIME_SNAPSHOT_TYPES_H */
