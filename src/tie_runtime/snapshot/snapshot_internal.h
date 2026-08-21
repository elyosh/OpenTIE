#ifndef TIE_RUNTIME_SNAPSHOT_INTERNAL_H
#define TIE_RUNTIME_SNAPSHOT_INTERNAL_H

#include "tie_runtime/snapshot/snapshot_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ===== Writer-side helpers (tie_core-internal) =====
 *
 * Module emitters call these between begin_tick and finalize_tick.
 * The alloc_* helpers atomically bump the count of the in-progress
 * slot and return a pointer to the next entry, or NULL when the cap
 * is reached. Caller fills the returned
 * struct in-place; nothing else to do.
 *
 * The *_mut helpers return a pointer to the in-progress slot's
 * scalar struct so an emitter can write fields directly. */
TieFlightObjectState* TieSnapshotBuilder_AllocFlight(void);
TieStaticObjectState* TieSnapshotBuilder_AllocStatic(void);
TieFlightObjectComponent* TieSnapshotBuilder_AllocFlightComponent(void);
TieBillboardState* TieSnapshotBuilder_AllocBillboard(void);
TieHyperstar* TieSnapshotBuilder_AllocHyperstar(void);
TieStarDirection* TieSnapshotBuilder_AllocStar(void);
uint16_t* TieSnapshotBuilder_AllocRequiredModelSpecies(void);
uint16_t* TieSnapshotBuilder_AllocRequiredSpriteSpecies(void);
TieActor2DState* TieSnapshotBuilder_AllocActor2D(void);
TieFilm2DState* TieSnapshotBuilder_AllocFilm2D(void);
TieDraw2D* TieSnapshotBuilder_AllocDraw2D(void);
TieUIText* TieSnapshotBuilder_AllocUIText(void);
TiePaintCmd* TieSnapshotBuilder_AllocPaintCmd(void);
TieTitleCrawlLine* TieSnapshotBuilder_AllocTitleCrawlLine(void);

/* Header mutator — emitter writes scalar fields directly. */
TieMapHeader* TieSnapshotBuilder_MapMut(void);

/* Allocate the next per-tick emit-counter slot. Use for single-record
 * snapshot fields (e.g. map.z_order) that need to interleave with
 * draws_2D / ui_texts / paint_cmds in the application's INCREMENTAL merge-
 * dispatch but don't go through one of the per-channel allocators.
 * Returns a strictly-monotonic int16 within the current tick. */
int16_t TieSnapshotBuilder_NextEmitZ(void);

/* Set the engine-time scalar that drives the active scene's clock
 * application-side. Called once per tick by an emit pipeline that owns the
 * current scene (e.g. TieRecoveredTitle_CaptureSnapshot for SCENE_TITLE). Outside
 * such a scene the field stays at 0 / 0.0 — consumers treat that as
 * "no scene-clock signal." */
void TieSnapshotBuilder_SetSceneClock(int32_t engine_frame, float frame_progress, uint32_t cel_period_us);

/* Set the scene-tag string used by the compositor for bundle lookup.
 * Pass NULL or "" to clear. Persistent across ticks until reset.
 * For film-driven scenes, prefer TieSnapshotBuilder_SetActiveFilm which
 * also derives the tag automatically. */
void TieSnapshotBuilder_SetSceneTag(const char* tag);

/* Set the HD-overlay RT redraw cadence. Persistent across ticks until
 * reset. Cutscenes (PLAY1, TIELOGO) set FULL_FRAME on push; UI scenes
 * stay at default INCREMENTAL. */
void TieSnapshotBuilder_SetRedrawModel(TieRedrawModel model);

TieCameraState* TieSnapshotBuilder_CameraMut(void);
TieHudState* TieSnapshotBuilder_HudMut(void);
TieCursorState* TieSnapshotBuilder_CursorMut(void);
TieFadeState* TieSnapshotBuilder_FadeMut(void);
TieCockpitState* TieSnapshotBuilder_CockpitMut(void);
TieBackdropSet* TieSnapshotBuilder_BackdropsMut(void);
TieHyperspaceState* TieSnapshotBuilder_HyperspaceMut(void);
uint32_t* TieSnapshotBuilder_PaletteMut(void); /* points to [256] */

/* Active battle index (0..12). Set once per tick by the flight-
 * snapshot emit from tie_core's `currentbattle` global. Renderer
 * reads via TieSnapshot.battle_id. */
void TieSnapshotBuilder_SetBattleId(uint8_t battle_id);
void TieSnapshotBuilder_SetFlightScreen(TieFlightScreen screen);

void TieSnapshotBuilder_SetClassicDims(uint16_t w, uint16_t h);
/* Records the active classic flight renderer after runtime fallback. */
void TieSnapshotBuilder_SetLegacyRenderConvention(TieFlightLegacyRenderConvention convention);
void TieSnapshotBuilder_SetLandruPresentation(uint16_t w, uint16_t h, uint8_t pixel_aspect,
											  uint8_t profile_id, uint32_t generation);

/* Stamp the logical flight-frame counter (move_flight_frame()) into the
 * in-progress slot. Written once per tick by TieFlightSnapshot_Capture;
 * lets consumers detect whether the sim advanced between snapshots. */
void TieSnapshotBuilder_SetFlightFrame(uint32_t frame);
void TieSnapshotBuilder_SetMissionLoadGeneration(uint32_t generation);

/* Replay-mode byte, written once per tick by TieFlightSnapshot_Capture from
 * the engine's recordingreplay and replayviewmode globals. Persistent until
 * rewritten; zeroed on begin_tick. */
void TieSnapshotBuilder_SetReplayMode(uint8_t mode);

/* tie_core-internal — Shoemake branch-by-trace conversion of a
 * row-major 3×3 rotation matrix to a quaternion in (w, x, y, z)
 * order. Used by emit pipelines that produce a quaternion from a
 * cached Q15 worldeye basis (camera + PIP camera + per-object).
 * Caller must pass a near-orthonormal matrix; behaviour is
 * undefined otherwise. */
void TieSnapshotBuilder_Mat3ToQuat(const float m[9], float q[4]);

/* Global directional light. Defaults to a unit-Y down-vector
 * with white linear color until an emitter overrides. Both arrays
 * are 3-float (xyz / rgb). */
void TieSnapshotBuilder_SetDirectionalLight(const float dir3[3], const float rgb3[3]);

/* Global Gouraud-shading toggle. Mirrors tie_core's `gouraudflag`
 * global (tie.c). Value is 0 (flat) or 0x40 (Gouraud on); HD reads
 * `!= 0` as the live toggle. */
void TieSnapshotBuilder_SetGouraudflag(uint8_t flag);

/* Global marking-emission toggle. Mirrors tie_core's `drawmarkingsflag`
 * (tie.c). HD reads `!= 0`; the per-craft per-mesh override
 * (species 17, wing meshes, non-friendly side) is applied host-side
 * in tie_remaster/flight/passes.c. */
void TieSnapshotBuilder_SetDrawmarkingsflag(uint8_t flag);

/* Current snapshot slot's flight_component_count, useful for
 * stamping (component_start, component_count) on a flight record
 * before emitting its component entries. */
uint16_t TieSnapshotBuilder_FlightComponentCount(void);

/* ===== Event push (tie_core-internal) =====
 *
 * Called from emitter sites (laser_createprojectile, collide_*, etc.)
 * during the tick. The event lands in the in-progress snapshot.
 * Drops with a TieDiagnostics_Log warn above TIE_MAX_EVENTS. */
void TieSnapshotBuilder_PushEvent(const TieEvent* ev);

/* ===== Scene-kind setter (tie_core-internal) =====
 *
 * Task-push helpers call this when they push a top-level modal so the
 * snapshot's scene_kind reflects the active scene. Idempotent; cheap. */
void TieSnapshotBuilder_SetSceneKind(TieSceneKind kind);
TieSceneKind TieSnapshotBuilder_GetSceneKind(void);

/* ===== Active-film tag (tie_core-internal) =====
 *
 * Stamps the in-progress and future snapshots with the (LFD, film)
 * the host's cutscene compositor uses as its manifest key. Pass
 * lfd or film == NULL (or empty strings) to clear — the compositor
 * then disables and falls back to classic rendering for the next
 * scene. Idempotent; cheap. */
void TieSnapshotBuilder_SetActiveFilm(const char* lfd_basename, const char* film_name);

/* ===== Palette emit (tie_core-internal) =====
 *
 * Convert the live 256×3 VGA-DAC palette (0..63 per channel) into the
 * snapshot's XRGB8888 entries. Reads the source through the existing
 * TieClassicFramebuffer_Current() accessor so the emitter has no internal-
 * header dependency on the host storage module. */
void TiePaletteSnapshot_Capture(void);

/* ===== Per-tick orchestration (tie_core-internal) =====
 *
 * TieRuntime_Tick brackets module emit calls between begin/finalize. The
 * begin call rotates slots, zeroes counts, snapshots scene kind +
 * sim_time + tick number. The finalize call marks the just-emitted
 * slot as the new current and flips the previous-valid flag once two
 * ticks have completed. */
void TieSnapshotBuilder_BeginTick(void);
void TieSnapshotBuilder_FinalizeTick(void);

#ifdef __cplusplus
}
#endif

#endif /* TIE_RUNTIME_SNAPSHOT_INTERNAL_H */
