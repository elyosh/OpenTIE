#include "tie_runtime/snapshot/snapshot.h"
#include "tie/tie.h" /* color_remap_table */
#include "tie_runtime/audio/config.h"
#include "tie_runtime/diagnostics/diagnostics.h"
#include "tie_runtime/display/classic_display.h"
#include "tie_runtime/display/classic_framebuffer.h"
#include "tie_runtime/flight_assets/model_types.h"
#include "tie_runtime/input/input.h"
#include "tie_runtime/runtime/exports.h"
#include "tie_runtime/runtime/profile.h"
#include "tie_runtime/snapshot/snapshot_internal.h"
#include "tie_runtime/storage/storage.h"
#include "tie_runtime/timing/sim_clock.h"
#include <landru/cursor.h> /* lcursor_get_bitmap */

#include <ctype.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

/* The previous slot becomes valid after two snapshots are finalized. */

void TieSnapshotBuilder_Mat3ToQuat(const float m[9], float q[4]) {
	const float trace = m[0] + m[4] + m[8];
	if (trace > 0.0f) {
		const float s = sqrtf(trace + 1.0f) * 2.0f;
		q[0] = 0.25f * s;
		q[1] = (m[7] - m[5]) / s;
		q[2] = (m[2] - m[6]) / s;
		q[3] = (m[3] - m[1]) / s;
	} else if (m[0] > m[4] && m[0] > m[8]) {
		const float s = sqrtf(1.0f + m[0] - m[4] - m[8]) * 2.0f;
		q[0] = (m[7] - m[5]) / s;
		q[1] = 0.25f * s;
		q[2] = (m[1] + m[3]) / s;
		q[3] = (m[2] + m[6]) / s;
	} else if (m[4] > m[8]) {
		const float s = sqrtf(1.0f + m[4] - m[0] - m[8]) * 2.0f;
		q[0] = (m[2] - m[6]) / s;
		q[1] = (m[1] + m[3]) / s;
		q[2] = 0.25f * s;
		q[3] = (m[5] + m[7]) / s;
	} else {
		const float s = sqrtf(1.0f + m[8] - m[0] - m[4]) * 2.0f;
		q[0] = (m[3] - m[1]) / s;
		q[1] = (m[2] + m[6]) / s;
		q[2] = (m[5] + m[7]) / s;
		q[3] = 0.25f * s;
	}
}

typedef struct TieSnapshotSlot {
	TieSnapshot header;
	TieFlightObjectState flights[TIE_MAX_FLIGHT_OBJECTS];
	TieStaticObjectState statics[TIE_MAX_STATIC_OBJECTS];
	TieActor2DState actors_2D[TIE_MAX_ACTORS_2D];
	TieFilm2DState films_2D[TIE_MAX_FILMS_2D];
	TieEvent events[TIE_MAX_EVENTS];
	TieDraw2D draws_2D[TIE_MAX_DRAWS_2D];
	TieUIText ui_texts[TIE_MAX_UI_TEXTS];
	TiePaintCmd paint_cmds[TIE_MAX_PAINT_CMDS];
	TieTitleCrawlLine title_crawl_lines[TIE_MAX_TITLE_CRAWL_LINES];
	TieFlightObjectComponent flight_components[TIE_MAX_FLIGHT_COMPONENTS];
	TieBillboardState billboards[TIE_MAX_BILLBOARDS];
	TieHyperstar hyperstars[TIE_MAX_HYPERSTARS];
	TieStarDirection stars[TIE_MAX_STARS];
	uint16_t required_model_species[TIE_SPECIES_COUNT];
	uint16_t required_sprite_species[TIE_SPECIES_COUNT];
	uint32_t dropped[TIE_SNAPSHOT_CHANNEL_COUNT];
} TieSnapshotSlot;

static TieSnapshotSlot s_slots[2];
static uint8_t s_cur = 0;              /* slot most recently emitted into */
static uint32_t s_finalized_count = 0; /* number of completed ticks */
static uint64_t s_tick_counter = 0;    /* monotonic, == tick of in-progress slot */
static TieSceneKind s_scene_kind = TIE_SCENE_FRONTEND;
static uint32_t s_pending_dropped[TIE_SNAPSHOT_CHANNEL_COUNT];
static uint64_t s_next_overflow_warning_tick;

static const char* const s_channel_names[TIE_SNAPSHOT_CHANNEL_COUNT] = {
	"flights",  "statics", "components", "billboards", "hyperstars", "stars", "models", "sprites",
	"actors2D", "films2D", "draws2D",    "texts",      "paint",      "crawl", "events",
};

static const uint32_t s_channel_capacities[TIE_SNAPSHOT_CHANNEL_COUNT] = {
	TIE_MAX_FLIGHT_OBJECTS, TIE_MAX_STATIC_OBJECTS,    TIE_MAX_FLIGHT_COMPONENTS,
	TIE_MAX_BILLBOARDS,     TIE_MAX_HYPERSTARS,        TIE_MAX_STARS,
	TIE_SPECIES_COUNT,      TIE_SPECIES_COUNT,         TIE_MAX_ACTORS_2D,
	TIE_MAX_FILMS_2D,       TIE_MAX_DRAWS_2D,          TIE_MAX_UI_TEXTS,
	TIE_MAX_PAINT_CMDS,     TIE_MAX_TITLE_CRAWL_LINES, TIE_MAX_EVENTS,
};

static void TieSnapshot_SnapshotDrop(TieSnapshotSlot* slot, TieSnapshotChannel channel) {
	slot->dropped[channel]++;
}

/* Active-film tag — written by TieSnapshotBuilder_SetActiveFilm, copied
 * into every begin_tick. Empty strings = no film active. */
static char s_active_lfd[16];
static char s_active_film[12];

/* Scene tag + composite mode — persistent state set by per-scene Open
 * functions, copied into every begin_tick. set_active_film auto-
 * derives s_scene_tag = "<lfd>/<film>" so cutscene bundles continue to
 * resolve via the existing path; non-film scenes call
 * TieSnapshotBuilder_SetSceneTag directly. */
static char s_scene_tag[64];
static TieRedrawModel s_redraw_model = TIE_REDRAW_INCREMENTAL;

/* Monotonic per-tick counter shared across draws_2D, ui_texts, and
 * paint_cmds. Each alloc stamps the next value into z_order so the
 * application can merge-dispatch the three channels in true engine emit
 * order — without this, paint primitives always render on top of
 * text (which is wrong for button labels) or under (wrong for
 * register's progressive-reveal cover). Reset in begin_tick. */
static uint16_t s_emit_z = 0;

/* Wire each slot's snapshot pointers to its inline arrays once. The
 * inline arrays are owned by the slot for the tie_core session; only
 * the array *contents* mutate per tick. */
static void TieSnapshot_WireSlotPointers(TieSnapshotSlot* slot) {
	slot->header.flights = slot->flights;
	slot->header.statics = slot->statics;
	slot->header.actors_2D = slot->actors_2D;
	slot->header.films_2D = slot->films_2D;
	slot->header.events = slot->events;
	slot->header.draws_2D = slot->draws_2D;
	slot->header.ui_texts = slot->ui_texts;
	slot->header.paint_cmds = slot->paint_cmds;
	slot->header.title_crawl_lines = slot->title_crawl_lines;
	slot->header.flight_components = slot->flight_components;
	slot->header.billboards = slot->billboards;
	slot->header.hyperstars = slot->hyperstars;
	slot->header.stars = slot->stars;
	slot->header.required_model_species = slot->required_model_species;
	slot->header.required_sprite_species = slot->required_sprite_species;
}

void TieSnapshotBuilder_BeginTick(void) {
	s_cur ^= 1;
	TieSnapshotSlot* slot = &s_slots[s_cur];

	/* Header. Preserve nothing — every field is overwritten by emitters
	 * or zeroed here so a partial emit produces a valid empty section
	 * rather than stale data from the previous occupancy of this slot. */
	slot->header.tick = s_tick_counter;
	slot->header.sim_time_us = TieSimClock_NowUs();
	slot->header.scene_kind = s_scene_kind;
	slot->header.classic_w = 0;
	slot->header.classic_h = 0;
	slot->header.landru_coord_w = 0;
	slot->header.landru_coord_h = 0;
	slot->header.landru_presentation_generation = 0;
	slot->header.landru_pixel_aspect = TIE_SOURCE_PIXEL_ASPECT_SQUARE;
	slot->header.frontend_profile_id = 0;
	memcpy(slot->header.current_lfd_basename, s_active_lfd, sizeof slot->header.current_lfd_basename);
	memcpy(slot->header.current_film_name, s_active_film, sizeof slot->header.current_film_name);
	memcpy(slot->header.scene_tag, s_scene_tag, sizeof slot->header.scene_tag);
	slot->header.redraw_model = (uint8_t)s_redraw_model;
	slot->header.flight_screen = TIE_FLIGHT_SCREEN_NORMAL;
	slot->header.replay_mode = 0;
	slot->header.flight_count = 0;
	slot->header.static_count = 0;
	slot->header.actor_2D_count = 0;
	slot->header.film_2D_count = 0;
	slot->header.event_count = 0;
	slot->header.draw_2D_count = 0;
	slot->header.ui_text_count = 0;
	slot->header.paint_cmd_count = 0;
	slot->header.title_crawl_count = 0;
	slot->header.flight_component_count = 0;
	slot->header.billboard_count = 0;
	slot->header.hyperstar_count = 0;
	slot->header.star_count = 0;
	slot->header.required_model_species_count = 0;
	slot->header.required_sprite_species_count = 0;
	memset(slot->dropped, 0, sizeof slot->dropped);
	slot->header.mission_load_generation = 0;
	/* Default directional light: -Y (down), white. */
	slot->header.directional_dir[0] = 0.0f;
	slot->header.directional_dir[1] = -1.0f;
	slot->header.directional_dir[2] = 0.0f;
	slot->header.directional_color[0] = 1.0f;
	slot->header.directional_color[1] = 1.0f;
	slot->header.directional_color[2] = 1.0f;
	/* Default Gouraud off; emitter overrides per tick from tie_core's
	 * `gouraudflag` global. */
	slot->header.gouraudflag = 0;
	/* Default markings on; matches the engine's bpflight init
	 * (drawmarkingsflag = 1). Emitter overrides per tick. */
	slot->header.drawmarkingsflag = 1;
	slot->header.legacy_render_convention = TIE_FLIGHT_LEGACY_RENDER_TIE95;
	slot->header.scene_clock.engine_frame = 0;
	slot->header.scene_clock.frame_progress = 0.0f;
	slot->header.scene_clock.cel_period_us = 0;
	s_emit_z = 0;
	memset(&slot->header.camera, 0, sizeof slot->header.camera);
	/* HUD carries over from the previous tick — engine paints (and
	 * TieHudSnapshot_Capture writes) overlay on top, fields the engine
	 * doesn't touch keep their previous-tick value (framebuffer
	 * persistence semantics). */
	memcpy(&slot->header.hud, &s_slots[s_cur ^ 1].header.hud, sizeof slot->header.hud);
	memset(&slot->header.cursor, 0, sizeof slot->header.cursor);
	memset(&slot->header.fade, 0, sizeof slot->header.fade);
	memset(&slot->header.map, 0, sizeof slot->header.map);
	memset(&slot->header.cockpit, 0, sizeof slot->header.cockpit);
	memset(&slot->header.backdrops, 0, sizeof slot->header.backdrops);
	memset(&slot->header.hyperspace, 0, sizeof slot->header.hyperspace);
	slot->header.battle_id = 0;
	memset(slot->header.palette, 0, sizeof slot->header.palette);

	TieSnapshot_WireSlotPointers(slot);

	/* SNAPSHOT-ONLY note: the per-tick billboard capture caches are
	 * intentionally NOT cleared here. They're cleared by tie_core's
	 * render entry (render_world_or_skip) so that paused frames —
	 * which skip the render and therefore can't re-fill the caches —
	 * keep showing the last-rendered billboards. TieSnapshotBuilder_Begin
	 * tick fires every host tick (incl. paused), so resetting here
	 * would zero the billboard pass during pause. */
}

void TieSnapshotBuilder_FinalizeTick(void) {
	TieSnapshotSlot* slot = &s_slots[s_cur];
	bool overflowed = false;
	for (int channel = 0; channel < TIE_SNAPSHOT_CHANNEL_COUNT; ++channel) {
		s_pending_dropped[channel] += slot->dropped[channel];
		overflowed = overflowed || slot->dropped[channel] != 0;
	}
	if (overflowed && s_tick_counter >= s_next_overflow_warning_tick) {
		char message[512];
		int offset = snprintf(message, sizeof message, "snapshot tick=%" PRIu64 " dropped", s_tick_counter);
		for (int channel = 0;
			 channel < TIE_SNAPSHOT_CHANNEL_COUNT && offset > 0 && (size_t)offset < sizeof message;
			 ++channel) {
			if (!s_pending_dropped[channel])
				continue;
			offset +=
				snprintf(message + offset, sizeof message - (size_t)offset, " %s=%u/%u",
						 s_channel_names[channel], s_pending_dropped[channel], s_channel_capacities[channel]);
		}
		TieDiagnostics_Log(TIE_LOG_WARN, "%s\n", message);
		memset(s_pending_dropped, 0, sizeof s_pending_dropped);
		s_next_overflow_warning_tick = s_tick_counter + 250u;
	}
	s_tick_counter++;
	if (s_finalized_count < 2)
		s_finalized_count++;
}

void TieSnapshot_OverflowStats(TieSnapshotOverflowStats* out) {
	if (!out)
		return;
	out->tick = s_slots[s_cur].header.tick;
	memcpy(out->dropped, s_slots[s_cur].dropped, sizeof out->dropped);
	memcpy(out->capacity, s_channel_capacities, sizeof out->capacity);
}

void TieSnapshotBuilder_SetMissionLoadGeneration(uint32_t generation) {
	s_slots[s_cur].header.mission_load_generation = generation;
}

const TieSnapshot* TieSnapshot_Current(void) {
	if (s_finalized_count < 1)
		return NULL;
	return &s_slots[s_cur].header;
}

const TieSnapshot* TieSnapshot_Previous(void) {
	if (s_finalized_count < 2)
		return NULL;
	return &s_slots[s_cur ^ 1].header;
}

/* ===== Writer-side helpers ===== */

TieFlightObjectState* TieSnapshotBuilder_AllocFlight(void) {
	TieSnapshotSlot* slot = &s_slots[s_cur];
	if (slot->header.flight_count >= TIE_MAX_FLIGHT_OBJECTS) {
		TieSnapshot_SnapshotDrop(slot, TIE_SNAPSHOT_CHANNEL_FLIGHTS);
		return NULL;
	}
	return &slot->flights[slot->header.flight_count++];
}

TieStaticObjectState* TieSnapshotBuilder_AllocStatic(void) {
	TieSnapshotSlot* slot = &s_slots[s_cur];
	if (slot->header.static_count >= TIE_MAX_STATIC_OBJECTS) {
		TieSnapshot_SnapshotDrop(slot, TIE_SNAPSHOT_CHANNEL_STATICS);
		return NULL;
	}
	return &slot->statics[slot->header.static_count++];
}

TieFlightObjectComponent* TieSnapshotBuilder_AllocFlightComponent(void) {
	TieSnapshotSlot* slot = &s_slots[s_cur];
	if (slot->header.flight_component_count >= TIE_MAX_FLIGHT_COMPONENTS) {
		TieSnapshot_SnapshotDrop(slot, TIE_SNAPSHOT_CHANNEL_FLIGHT_COMPONENTS);
		return NULL;
	}
	return &slot->flight_components[slot->header.flight_component_count++];
}

/* SNAPSHOT-ONLY: allocator for the per-tick HD billboard queue. */
TieBillboardState* TieSnapshotBuilder_AllocBillboard(void) {
	TieSnapshotSlot* slot = &s_slots[s_cur];
	if (slot->header.billboard_count >= TIE_MAX_BILLBOARDS) {
		TieSnapshot_SnapshotDrop(slot, TIE_SNAPSHOT_CHANNEL_BILLBOARDS);
		return NULL;
	}
	return &slot->billboards[slot->header.billboard_count++];
}

/* SNAPSHOT-ONLY: allocator for the per-tick hyperspace-streak queue.
 * Drained by TieFlightSnapshot_Capture during phases 3 & 5; idle every
 * other tick. */
TieHyperstar* TieSnapshotBuilder_AllocHyperstar(void) {
	TieSnapshotSlot* slot = &s_slots[s_cur];
	if (slot->header.hyperstar_count >= TIE_MAX_HYPERSTARS) {
		TieSnapshot_SnapshotDrop(slot, TIE_SNAPSHOT_CHANNEL_HYPERSTARS);
		return NULL;
	}
	return &slot->hyperstars[slot->header.hyperstar_count++];
}

TieStarDirection* TieSnapshotBuilder_AllocStar(void) {
	TieSnapshotSlot* slot = &s_slots[s_cur];
	if (slot->header.star_count >= TIE_MAX_STARS) {
		TieSnapshot_SnapshotDrop(slot, TIE_SNAPSHOT_CHANNEL_STARS);
		return NULL;
	}
	return &slot->stars[slot->header.star_count++];
}

uint16_t* TieSnapshotBuilder_AllocRequiredModelSpecies(void) {
	TieSnapshotSlot* slot = &s_slots[s_cur];
	if (slot->header.required_model_species_count >= TIE_SPECIES_COUNT) {
		TieSnapshot_SnapshotDrop(slot, TIE_SNAPSHOT_CHANNEL_REQUIRED_MODELS);
		return NULL;
	}
	return &slot->required_model_species[slot->header.required_model_species_count++];
}

uint16_t* TieSnapshotBuilder_AllocRequiredSpriteSpecies(void) {
	TieSnapshotSlot* slot = &s_slots[s_cur];
	if (slot->header.required_sprite_species_count >= TIE_SPECIES_COUNT) {
		TieSnapshot_SnapshotDrop(slot, TIE_SNAPSHOT_CHANNEL_REQUIRED_SPRITES);
		return NULL;
	}
	return &slot->required_sprite_species[slot->header.required_sprite_species_count++];
}

uint16_t TieSnapshotBuilder_FlightComponentCount(void) {
	return s_slots[s_cur].header.flight_component_count;
}

void TieSnapshotBuilder_SetReplayMode(uint8_t mode) { s_slots[s_cur].header.replay_mode = mode; }

void TieSnapshotBuilder_SetDirectionalLight(const float dir3[3], const float rgb3[3]) {
	TieSnapshot* h = &s_slots[s_cur].header;
	if (dir3) {
		h->directional_dir[0] = dir3[0];
		h->directional_dir[1] = dir3[1];
		h->directional_dir[2] = dir3[2];
	}
	if (rgb3) {
		h->directional_color[0] = rgb3[0];
		h->directional_color[1] = rgb3[1];
		h->directional_color[2] = rgb3[2];
	}
}

void TieSnapshotBuilder_SetGouraudflag(uint8_t flag) { s_slots[s_cur].header.gouraudflag = flag; }

void TieSnapshotBuilder_SetDrawmarkingsflag(uint8_t flag) { s_slots[s_cur].header.drawmarkingsflag = flag; }

TieActor2DState* TieSnapshotBuilder_AllocActor2D(void) {
	TieSnapshotSlot* slot = &s_slots[s_cur];
	if (slot->header.actor_2D_count >= TIE_MAX_ACTORS_2D) {
		TieSnapshot_SnapshotDrop(slot, TIE_SNAPSHOT_CHANNEL_ACTORS_2D);
		return NULL;
	}
	return &slot->actors_2D[slot->header.actor_2D_count++];
}

TieFilm2DState* TieSnapshotBuilder_AllocFilm2D(void) {
	TieSnapshotSlot* slot = &s_slots[s_cur];
	if (slot->header.film_2D_count >= TIE_MAX_FILMS_2D) {
		TieSnapshot_SnapshotDrop(slot, TIE_SNAPSHOT_CHANNEL_FILMS_2D);
		return NULL;
	}
	return &slot->films_2D[slot->header.film_2D_count++];
}

TieDraw2D* TieSnapshotBuilder_AllocDraw2D(void) {
	TieSnapshotSlot* slot = &s_slots[s_cur];
	if (slot->header.draw_2D_count >= TIE_MAX_DRAWS_2D) {
		TieSnapshot_SnapshotDrop(slot, TIE_SNAPSHOT_CHANNEL_DRAWS_2D);
		return NULL;
	}
	TieDraw2D* out = &slot->draws_2D[slot->header.draw_2D_count];
	out->z_order = (int16_t)s_emit_z++;
	slot->header.draw_2D_count++;
	return out;
}

TieUIText* TieSnapshotBuilder_AllocUIText(void) {
	TieSnapshotSlot* slot = &s_slots[s_cur];
	if (slot->header.ui_text_count >= TIE_MAX_UI_TEXTS) {
		TieSnapshot_SnapshotDrop(slot, TIE_SNAPSHOT_CHANNEL_UI_TEXTS);
		return NULL;
	}
	TieUIText* out = &slot->ui_texts[slot->header.ui_text_count];
	memset(out, 0, sizeof *out);
	out->z_order = (int16_t)s_emit_z++;
	slot->header.ui_text_count++;
	return out;
}

TiePaintCmd* TieSnapshotBuilder_AllocPaintCmd(void) {
	TieSnapshotSlot* slot = &s_slots[s_cur];
	if (slot->header.paint_cmd_count >= TIE_MAX_PAINT_CMDS) {
		TieSnapshot_SnapshotDrop(slot, TIE_SNAPSHOT_CHANNEL_PAINT_CMDS);
		return NULL;
	}
	TiePaintCmd* out = &slot->paint_cmds[slot->header.paint_cmd_count];
	out->z_order = (int16_t)s_emit_z++;
	slot->header.paint_cmd_count++;
	return out;
}

TieTitleCrawlLine* TieSnapshotBuilder_AllocTitleCrawlLine(void) {
	TieSnapshotSlot* slot = &s_slots[s_cur];
	if (slot->header.title_crawl_count >= TIE_MAX_TITLE_CRAWL_LINES) {
		TieSnapshot_SnapshotDrop(slot, TIE_SNAPSHOT_CHANNEL_TITLE_CRAWL);
		return NULL;
	}
	return &slot->title_crawl_lines[slot->header.title_crawl_count++];
}

/* ===== Brief-map channel ===== */

TieMapHeader* TieSnapshotBuilder_MapMut(void) { return &s_slots[s_cur].header.map; }

/* Allocate the next per-tick emit-counter slot. Channels that don't
 * own an alloc helper (e.g. single-record snapshot fields like
 * map.z_order) call this directly to interleave with draws_2D /
 * ui_texts / paint_cmds in the application's INCREMENTAL merge-dispatch.
 * The alloc helpers (TieSnapshotBuilder_AllocDraw2D et al.) bump the
 * same counter, so all four channels share one strictly-monotonic
 * sequence. */
int16_t TieSnapshotBuilder_NextEmitZ(void) { return (int16_t)s_emit_z++; }

void TieSnapshotBuilder_SetSceneClock(int32_t engine_frame, float frame_progress, uint32_t cel_period_us) {
	TieSnapshotSlot* slot = &s_slots[s_cur];
	slot->header.scene_clock.engine_frame = engine_frame;
	slot->header.scene_clock.frame_progress = frame_progress;
	slot->header.scene_clock.cel_period_us = cel_period_us;
}

TieCameraState* TieSnapshotBuilder_CameraMut(void) { return &s_slots[s_cur].header.camera; }

TieHudState* TieSnapshotBuilder_HudMut(void) { return &s_slots[s_cur].header.hud; }

TieCursorState* TieSnapshotBuilder_CursorMut(void) { return &s_slots[s_cur].header.cursor; }

TieFadeState* TieSnapshotBuilder_FadeMut(void) { return &s_slots[s_cur].header.fade; }

TieCockpitState* TieSnapshotBuilder_CockpitMut(void) { return &s_slots[s_cur].header.cockpit; }

TieBackdropSet* TieSnapshotBuilder_BackdropsMut(void) { return &s_slots[s_cur].header.backdrops; }

TieHyperspaceState* TieSnapshotBuilder_HyperspaceMut(void) { return &s_slots[s_cur].header.hyperspace; }

void TieSnapshotBuilder_SetBattleId(uint8_t battle_id) { s_slots[s_cur].header.battle_id = battle_id; }

void TieSnapshotBuilder_SetFlightScreen(TieFlightScreen screen) {
	s_slots[s_cur].header.flight_screen = (uint8_t)screen;
}

const uint8_t* TieCursorSnapshot_Bitmap(int16_t* out_w, int16_t* out_h) {
	return lcursor_get_bitmap(out_w, out_h);
}

const uint8_t* TieTextSnapshot_ColorRemapTable(void) { return color_remap_table; }

uint32_t* TieSnapshotBuilder_PaletteMut(void) { return s_slots[s_cur].header.palette; }

void TieSnapshotBuilder_SetClassicDims(uint16_t w, uint16_t h) {
	s_slots[s_cur].header.classic_w = w;
	s_slots[s_cur].header.classic_h = h;
}

void TieSnapshotBuilder_SetLegacyRenderConvention(TieFlightLegacyRenderConvention convention) {
	s_slots[s_cur].header.legacy_render_convention = convention;
}

void TieSnapshotBuilder_SetLandruPresentation(uint16_t w, uint16_t h, uint8_t pixel_aspect,
											  uint8_t profile_id, uint32_t generation) {
	TieSnapshot* snapshot = &s_slots[s_cur].header;
	snapshot->landru_coord_w = w;
	snapshot->landru_coord_h = h;
	snapshot->landru_pixel_aspect = pixel_aspect;
	snapshot->frontend_profile_id = profile_id;
	snapshot->landru_presentation_generation = generation;
}

void TieSnapshotBuilder_SetFlightFrame(uint32_t frame) { s_slots[s_cur].header.flight_frame = frame; }

void TieSnapshotBuilder_PushEvent(const TieEvent* ev) {
	if (!ev)
		return;
	TieSnapshotSlot* slot = &s_slots[s_cur];
	if (slot->header.event_count >= TIE_MAX_EVENTS) {
		TieSnapshot_SnapshotDrop(slot, TIE_SNAPSHOT_CHANNEL_EVENTS);
		return;
	}
	slot->events[slot->header.event_count++] = *ev;
}

void TieSnapshotBuilder_SetSceneKind(TieSceneKind kind) {
	s_scene_kind = kind;
	s_slots[s_cur].header.scene_kind = kind;
}

TieSceneKind TieSnapshotBuilder_GetSceneKind(void) { return s_scene_kind; }

/* Copy `src` into `dst[cap]`, NUL-terminate, optionally uppercase the
 * source as it copies (used for the LFD basename to match retail
 * directory conventions). Trailing slack stays zero. */
static void TieSnapshot_CopyShortStr(char* dst, size_t cap, const char* src, int upper) {
	memset(dst, 0, cap);
	if (!src)
		return;
	size_t i = 0;
	for (; i + 1 < cap && src[i]; ++i)
		dst[i] = upper ? (char)toupper((unsigned char)src[i]) : src[i];
}

void TieSnapshotBuilder_SetActiveFilm(const char* lfd_basename, const char* film_name) {
	/* Update the persistent state used by every subsequent
	 * begin_tick. Also stamp the in-progress slot if a tick is mid-
	 * emission so the scene tag doesn't lag a frame. */
	TieSnapshot_CopyShortStr(s_active_lfd, sizeof s_active_lfd, lfd_basename, 1);
	TieSnapshot_CopyShortStr(s_active_film, sizeof s_active_film, film_name, 0);

	/* Auto-derive scene_tag = "<lfd>/<film>" so the compositor's
	 * unified bundle key works without each cutscene caller having to
	 * compose the tag manually. Empty when both are NULL/empty. */
	memset(s_scene_tag, 0, sizeof s_scene_tag);
	if (s_active_lfd[0] && s_active_film[0]) {
		snprintf(s_scene_tag, sizeof s_scene_tag, "%s/%s", s_active_lfd, s_active_film);
	}

	TieSnapshotSlot* slot = &s_slots[s_cur];
	memcpy(slot->header.current_lfd_basename, s_active_lfd, sizeof slot->header.current_lfd_basename);
	memcpy(slot->header.current_film_name, s_active_film, sizeof slot->header.current_film_name);
	memcpy(slot->header.scene_tag, s_scene_tag, sizeof slot->header.scene_tag);
}

void TieSnapshotBuilder_SetSceneTag(const char* tag) {
	memset(s_scene_tag, 0, sizeof s_scene_tag);
	if (tag && tag[0]) {
		size_t i = 0;
		for (; i + 1 < sizeof s_scene_tag && tag[i]; ++i)
			s_scene_tag[i] = tag[i];
	}
	TieSnapshotSlot* slot = &s_slots[s_cur];
	memcpy(slot->header.scene_tag, s_scene_tag, sizeof slot->header.scene_tag);
}

void TieSnapshotBuilder_SetRedrawModel(TieRedrawModel model) {
	s_redraw_model = model;
	s_slots[s_cur].header.redraw_model = (uint8_t)model;
}

void TiePaletteSnapshot_Capture(void) {
	/* TieClassicFramebuffer_Current().palette is a pointer to tie_core's live
	 * 256×3 byte VGA-DAC mirror (0..63 per channel). Expand each
	 * channel 6→8 with the standard `(c << 2) | (c >> 4)` so 0x3F maps
	 * to 0xFF and 0x00 stays 0x00, then pack as 0xFFRRGGBB.
	 *
	 * Pre-mode-set / pre-tie_init the framebuffer descriptor's palette
	 * pointer is non-NULL (it points at s_palette, which is BSS and
	 * thus zero) — emitting all-zero entries is the right behavior;
	 * a renderer that hasn't yet seen a palette write reads black. */
	const TieFramebuffer* fb = TieClassicFramebuffer_Current();
	if (!fb || !fb->palette)
		return; /* defensive — keeps the slot's palette[] zeroed */
	const uint8_t* src = fb->palette;
	uint32_t* dst = TieSnapshotBuilder_PaletteMut();
	for (int i = 0; i < 256; ++i) {
		const uint8_t r6 = src[3 * i + 0];
		const uint8_t g6 = src[3 * i + 1];
		const uint8_t b6 = src[3 * i + 2];
		const uint32_t r8 = (uint32_t)((r6 << 2) | (r6 >> 4));
		const uint32_t g8 = (uint32_t)((g6 << 2) | (g6 >> 4));
		const uint32_t b8 = (uint32_t)((b6 << 2) | (b6 >> 4));
		dst[i] = 0xFF000000u | (r8 << 16) | (g8 << 8) | b8;
	}
}
