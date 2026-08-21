#include "tie_runtime/integration/landru_adapter.h"
#include "tie_runtime/display/classic_framebuffer.h"

#include "tie/flight_surface_tie98.h"
#include "tie/frontend_display_tie98.h"
#include "tie_runtime/audio/config.h"
#include "tie_runtime/diagnostics/diagnostics.h"
#include "tie_runtime/display/classic_display.h"
#include "tie_runtime/display/classic_framebuffer.h"
#include "tie_runtime/flight_assets/model_types.h"
#include "tie_runtime/input/input.h"
#include "tie_runtime/runtime/exports.h"
#include "tie_runtime/runtime/profile.h"
#include "tie_runtime/storage/storage.h"
#include "tie_runtime/timing/sim_clock.h"

#include "aeron/log.h"
#include "tie/wavestream_tie98.h"
#include "tie_runtime/audio/music_policy.h"
#include "tie_runtime/display/classic_display.h"
#include "tie_runtime/snapshot/snapshot.h"
#include "tie_runtime/snapshot/snapshot_internal.h"

#include <landru/actor.h>
#include <landru/cursor.h>
#include <landru/fade.h>
#include <landru/film.h>
#include <landru/host.h>
#include <landru/surface.h>
#include <landru/vesa.h>

#include <stdarg.h>
#include <string.h>

static TieFileRoot TieLandruAdapter_TieRoot(LandruFileRoot root) {
	switch (root) {
		case LANDRU_FILE_ROOT_ASSET:
			return TIE_FILE_ROOT_FRONTEND_ASSET;
		case LANDRU_FILE_ROOT_AUXILIARY_ASSET:
			return TIE_FILE_ROOT_FLIGHT_ASSET;
		case LANDRU_FILE_ROOT_USER:
			return TIE_FILE_ROOT_USER;
		case LANDRU_FILE_ROOT_TEMP:
			return TIE_FILE_ROOT_TEMP;
		default:
			return TIE_FILE_ROOT_FRONTEND_ASSET;
	}
}

static TieLogLevel TieLandruAdapter_TieLogLevel(LandruLogLevel level) {
	switch (level) {
		case LANDRU_LOG_TRACE:
			return TIE_LOG_TRACE;
		case LANDRU_LOG_INFO:
			return TIE_LOG_INFO;
		case LANDRU_LOG_WARN:
			return TIE_LOG_WARN;
		default:
			return TIE_LOG_ERROR;
	}
}

static uint8_t TieLandruAdapter_TieRenderTarget(LandruRenderTarget target) {
	return target == LANDRU_RENDER_TARGET_AUXILIARY ? TIE_EMIT_TARGET_BRIEF_SOURCE : TIE_EMIT_TARGET_CUTSCENE;
}

static uint8_t TieLandruAdapter_TiePaintOp(LandruPaintOp op) {
	switch (op) {
		case LANDRU_PAINT_FRAME_RECT:
			return TIE_PAINT_FRAME_RECT;
		case LANDRU_PAINT_HLINE:
			return TIE_PAINT_HLINE;
		case LANDRU_PAINT_VLINE:
			return TIE_PAINT_VLINE;
		case LANDRU_PAINT_PIXEL:
			return TIE_PAINT_PIXEL;
		case LANDRU_PAINT_BEVEL:
			return TIE_PAINT_BEVEL;
		case LANDRU_PAINT_FRAME_BEVEL:
			return TIE_PAINT_FRAME_BEVEL;
		case LANDRU_PAINT_DBEVEL:
			return TIE_PAINT_DBEVEL;
		case LANDRU_PAINT_FRAME_DBEVEL:
			return TIE_PAINT_FRAME_DBEVEL;
		case LANDRU_PAINT_XOR_RECT:
			return TIE_PAINT_XOR_RECT;
		case LANDRU_PAINT_SHADE_RECT:
			return TIE_PAINT_SHADE_RECT;
		default:
			return TIE_PAINT_FILL_RECT;
	}
}

static uint8_t TieLandruAdapter_CursorKind(LandruCursorKind kind) {
	switch (kind) {
		case LANDRU_CURSOR_WAIT:
			return TIE_CURSOR_WAIT;
		case LANDRU_CURSOR_NONE:
			return TIE_CURSOR_NONE;
		default:
			return TIE_CURSOR_POINTER;
	}
}

static uint8_t TieLandruAdapter_TieFadeKind(LandruFadeKind kind) {
	return kind == LANDRU_FADE_ACTIVE ? TIE_FADE_ACTIVE : TIE_FADE_NONE;
}

static uint16_t TieLandruAdapter_TieActorFlags(uint16_t flags) {
	uint16_t result = 0;
	if (flags & LANDRU_ACTOR_RENDER_VISIBLE)
		result |= TIE_ACTOR2D_VISIBLE;
	if (flags & LANDRU_ACTOR_RENDER_ACTIVE)
		result |= TIE_ACTOR2D_ACTIVE;
	if (flags & LANDRU_ACTOR_RENDER_HFLIP)
		result |= TIE_ACTOR2D_HFLIP;
	if (flags & LANDRU_ACTOR_RENDER_VFLIP)
		result |= TIE_ACTOR2D_VFLIP;
	if (flags & LANDRU_ACTOR_RENDER_REMAP_COLOR)
		result |= TIE_ACTOR2D_REMAP_COLOR;
	return result;
}

static void TieLandruAdapter_HostLog(void* userdata, LandruLogLevel level, const char* format, va_list args) {
	(void)userdata;
	AeronLogLevel aeron_level = AERON_LOG_ERROR;
	const TieLogLevel level_value = TieLandruAdapter_TieLogLevel(level);
	if (level_value == TIE_LOG_INFO)
		aeron_level = AERON_LOG_INFO;
	else if (level_value == TIE_LOG_WARN)
		aeron_level = AERON_LOG_WARN;
	else if (level_value == TIE_LOG_TRACE)
		aeron_level = AERON_LOG_TRACE;
	Aeron_LogMessageV(aeron_level, "tie.landru", format, args);
}

static LandruFile* TieLandruAdapter_OpenFile(void* userdata, LandruFileRoot root, const char* path,
											 const char* mode) {
	(void)userdata;
	return TieStorage_Open(TieLandruAdapter_TieRoot(root), path, mode);
}

static size_t TieLandruAdapter_HostFileRead(void* userdata, void* buffer, size_t size, size_t count,
											LandruFile* file) {
	(void)userdata;
	return TieStorage_Read(buffer, size, count, file);
}

static size_t TieLandruAdapter_HostFileWrite(void* userdata, const void* buffer, size_t size, size_t count,
											 LandruFile* file) {
	(void)userdata;
	return TieStorage_Write(buffer, size, count, file);
}

static int TieLandruAdapter_HostFileSeek(void* userdata, LandruFile* file, long offset, int origin) {
	(void)userdata;
	return TieStorage_Seek(file, offset, origin);
}

static long TieLandruAdapter_HostFileTell(void* userdata, LandruFile* file) {
	(void)userdata;
	return TieStorage_Tell(file);
}

static int TieLandruAdapter_HostFileClose(void* userdata, LandruFile* file) {
	(void)userdata;
	return TieStorage_Close(file);
}

static LandruDir* TieLandruAdapter_OpenDirectory(void* userdata, LandruFileRoot root, const char* path) {
	(void)userdata;
	return TieStorage_DirOpen(TieLandruAdapter_TieRoot(root), path);
}

static int TieLandruAdapter_HostDirNext(void* userdata, LandruDir* dir, LandruDirEntry* entry) {
	(void)userdata;
	if (!entry)
		return 0;
	TieDirEntry tie_entry;
	if (!TieStorage_DirNext(dir, &tie_entry))
		return 0;
	memcpy(entry->name, tie_entry.name, sizeof entry->name);
	entry->name[sizeof entry->name - 1] = '\0';
	entry->is_dir = tie_entry.is_dir;
	entry->size = tie_entry.size;
	return 1;
}

static void TieLandruAdapter_HostDirClose(void* userdata, LandruDir* dir) {
	(void)userdata;
	TieStorage_DirClose(dir);
}

static int TieLandruAdapter_HostPathIsDir(void* userdata, LandruFileRoot root, const char* path) {
	(void)userdata;
	return TieStorage_IsDirectory(TieLandruAdapter_TieRoot(root), path);
}

static int TieLandruAdapter_HostKeyPending(void* userdata) {
	(void)userdata;
	return TieInput_KeyPending();
}

static int TieLandruAdapter_HostKeyRead(void* userdata) {
	(void)userdata;
	return TieInput_ReadKey();
}

static int TieLandruAdapter_HostModifierKeys(void* userdata) {
	(void)userdata;
	return TieInput_ModifierKeys();
}

static void TieLandruAdapter_HostMousePosition(void* userdata, int16_t* buttons, int16_t* x, int16_t* y) {
	(void)userdata;
	if (buttons)
		*buttons = 0;
	if (x)
		*x = 0;
	if (y)
		*y = 0;
	TieInput_GetMousePosition(buttons, x, y);
}

static void TieLandruAdapter_HostMouseMovement(void* userdata, int16_t* x, int16_t* y) {
	(void)userdata;
	if (x)
		*x = 0;
	if (y)
		*y = 0;
	TieInput_GetMouseMovement(x, y);
}

static void TieLandruAdapter_HostMouseSetPosition(void* userdata, int16_t x, int16_t y) {
	(void)userdata;
	TieInput_SetMousePosition(x, y);
}

static void TieLandruAdapter_HostMouseShow(void* userdata, bool show) {
	(void)userdata;
	TieInput_ShowCursor(show);
}

static int TieLandruAdapter_HostJoystickCount(void* userdata) {
	(void)userdata;
	return TieInput_JoystickPresent();
}

static void TieLandruAdapter_HostJoystickRead(void* userdata, int port, int16_t* axes, int axis_count,
											  uint16_t* buttons) {
	(void)userdata;
	if (axes && axis_count > 0)
		memset(axes, 0, (size_t)axis_count * sizeof *axes);
	if (buttons)
		*buttons = 0;
	TieInput_ReadJoystick(port, axes, axis_count, buttons);
}

static void TieLandruAdapter_HostPaletteSet(void* userdata, const uint8_t* rgb, int start, int count) {
	(void)userdata;
	TieClassicFramebuffer_SetPalette(rgb, start, count);
}

static void TieLandruAdapter_HostVideoSetMode(void* userdata, uint16_t mode) {
	(void)userdata;
	FrontendDisplay_SetDisplayMode(mode);
	FlightSurface_Lock();
	FlightSurface_Unlock();
	lvesa_Set_Platform_Pitch((int16_t)FrontendDisplay_GetDrawSurfacePitch());
	lsurface_Invalidate_Presentation();
}

static void TieLandruAdapter_HostVideoLock(void* userdata) {
	(void)userdata;
	FlightSurface_Lock();
}

static void TieLandruAdapter_HostVideoUnlock(void* userdata) {
	(void)userdata;
	FlightSurface_Unlock();
}

static void TieLandruAdapter_HostVideoCopyToPresentSurface(void* userdata) {
	(void)userdata;
	if (DDRAW_Present_Landru_Frame() == DX_DD_OK)
		TieClassicFramebuffer_CapturePresentedVga();
}

static void TieLandruAdapter_HostVideoPresent(void* userdata) {
	(void)userdata;
	FrontendDisplay_PresentFrame();
}

static uint64_t TieLandruAdapter_HostNowUs(void* userdata) {
	(void)userdata;
	return TieSimClock_NowUs();
}

bool TieLandruAdapter_EmitActorState(const LandruActorRenderState* state) {
	if (!state)
		return false;
	TieActor2DState* out = TieSnapshotBuilder_AllocActor2D();
	if (!out)
		return false;
	out->res_type = state->res_type;
	memcpy(out->res_name, state->res_name, sizeof out->res_name);
	out->id = state->id;
	out->film_entry_index = state->film_entry_index;
	out->x = state->x;
	out->y = state->y;
	out->w = state->w;
	out->h = state->h;
	out->zplane = state->zplane;
	out->state = state->state;
	out->flags = TieLandruAdapter_TieActorFlags(state->flags);
	out->xscale = state->xscale;
	out->yscale = state->yscale;
	out->clip_left = state->clip_left;
	out->clip_top = state->clip_top;
	out->clip_right = state->clip_right;
	out->clip_bottom = state->clip_bottom;
	out->prev_x = state->prev_x;
	out->prev_y = state->prev_y;
	out->prev_xv = state->prev_xv;
	out->prev_yv = state->prev_yv;
	out->xv = state->xv;
	out->yv = state->yv;
	out->xvf = state->xvf;
	out->yvf = state->yvf;
	out->prev_xscale = state->prev_xscale;
	out->prev_yscale = state->prev_yscale;
	out->fore_color = state->fore_color;
	return true;
}

static void TieLandruAdapter_RenderActor(void* userdata, const LandruActorRenderState* state) {
	(void)userdata;
	(void)TieLandruAdapter_EmitActorState(state);
}

static void TieLandruAdapter_RenderDraw(void* userdata, const LandruDrawCommand* command) {
	(void)userdata;
	TieDraw2D* out = TieSnapshotBuilder_AllocDraw2D();
	if (!out)
		return;
	out->res_type = command->res_type;
	memcpy(out->res_name, command->res_name, sizeof out->res_name);
	out->film_entry_index = command->film_entry_index;
	out->x = command->x;
	out->y = command->y;
	out->w = command->w;
	out->h = command->h;
	out->state = command->state;
	out->clip_left = command->clip_left;
	out->clip_top = command->clip_top;
	out->clip_right = command->clip_right;
	out->clip_bottom = command->clip_bottom;
	out->flags = TieLandruAdapter_TieActorFlags(command->flags);
	out->fore_color = command->fore_color;
	out->target = TieLandruAdapter_TieRenderTarget(command->target);
}

static void TieLandruAdapter_RenderFilm(void* userdata, const LandruFilmRenderState* state) {
	(void)userdata;
	TieFilm2DState* out = TieSnapshotBuilder_AllocFilm2D();
	if (!out)
		return;
	out->res_type = state->res_type;
	memcpy(out->res_name, state->res_name, sizeof out->res_name);
	out->x = state->x;
	out->y = state->y;
	out->zplane = state->zplane;
	out->cur_cel = state->cur_cel;
	out->cels = state->cels;
	out->flags = state->flags;
}

static void TieLandruAdapter_RenderText(void* userdata, const LandruTextCommand* command) {
	(void)userdata;
	TieUIText* out = TieSnapshotBuilder_AllocUIText();
	if (!out)
		return;
	memcpy(out->text, command->text, sizeof out->text);
	out->x = command->x;
	out->y = command->y;
	out->color_index = command->color_index;
	out->bold_color_index = command->bold_color_index;
	out->shadow_color_index = command->shadow_color_index;
	out->shadow = command->shadow;
	out->font_id = command->font_id;
	out->font_domain = TIE_FONT_DOMAIN_LANDRU;
	out->target = TieLandruAdapter_TieRenderTarget(command->target);
	out->background = 0;
	out->background_color_index = 0;
	out->clip_left = command->clip_left;
	out->clip_top = command->clip_top;
	out->clip_right = command->clip_right;
	out->clip_bottom = command->clip_bottom;
}

static void TieLandruAdapter_RenderPaint(void* userdata, const LandruPaintCommand* command) {
	(void)userdata;
	TiePaintCmd* out = TieSnapshotBuilder_AllocPaintCmd();
	if (!out)
		return;
	out->op = TieLandruAdapter_TiePaintOp(command->op);
	out->pressed = command->pressed;
	memcpy(out->colors, command->colors, sizeof out->colors);
	out->target = TieLandruAdapter_TieRenderTarget(command->target);
	out->x = command->x;
	out->y = command->y;
	out->w = command->w;
	out->h = command->h;
	out->clip_left = command->clip_left;
	out->clip_top = command->clip_top;
	out->clip_right = command->clip_right;
	out->clip_bottom = command->clip_bottom;
}

static void TieLandruAdapter_RenderCursor(void* userdata, const LandruCursorRenderState* state) {
	(void)userdata;
	TieCursorState* out = TieSnapshotBuilder_CursorMut();
	out->x = state->x;
	out->y = state->y;
	out->hot_x = state->hot_x;
	out->hot_y = state->hot_y;
	out->w = state->w;
	out->h = state->h;
	out->visible = state->visible;
	out->kind = TieLandruAdapter_CursorKind(state->kind);
}

static void TieLandruAdapter_RenderFade(void* userdata, const LandruFadeRenderState* state) {
	(void)userdata;
	TieFadeState* out = TieSnapshotBuilder_FadeMut();
	out->kind = TieLandruAdapter_TieFadeKind(state->kind);
	out->hd_factor = state->source_factor;
	out->r = state->r;
	out->g = state->g;
	out->b = state->b;
	out->freeze_overlay = state->freeze_frame;
}

static void TieLandruAdapter_RenderSceneClock(void* userdata, int32_t frame, float progress,
											  uint32_t period_us) {
	(void)userdata;
	TieSnapshotBuilder_SetSceneClock(frame, progress, period_us);
}

static bool TieLandruAdapter_RenderIsSceneTransition(void* userdata) {
	(void)userdata;
	const TieSnapshot* current = TieSnapshot_Current();
	const TieSnapshot* previous = TieSnapshot_Previous();
	return current && previous &&
		   strncmp(current->scene_tag, previous->scene_tag, sizeof current->scene_tag) != 0;
}

static void TieLandruAdapter_FrontendAudioPump(void* userdata) {
	(void)userdata;
	if (TieMusicPolicy_UsesTie98())
		FrontendWaveStream_Update();
}

bool TieLandruAdapter_Init(void) {
	LandruHost host = {
		.log = TieLandruAdapter_HostLog,
		.file_open = TieLandruAdapter_OpenFile,
		.file_read = TieLandruAdapter_HostFileRead,
		.file_write = TieLandruAdapter_HostFileWrite,
		.file_seek = TieLandruAdapter_HostFileSeek,
		.file_tell = TieLandruAdapter_HostFileTell,
		.file_close = TieLandruAdapter_HostFileClose,
		.dir_open = TieLandruAdapter_OpenDirectory,
		.dir_next = TieLandruAdapter_HostDirNext,
		.dir_close = TieLandruAdapter_HostDirClose,
		.path_is_dir = TieLandruAdapter_HostPathIsDir,
		.key_pending = TieLandruAdapter_HostKeyPending,
		.key_read = TieLandruAdapter_HostKeyRead,
		.modifier_keys = TieLandruAdapter_HostModifierKeys,
		.mouse_position = TieLandruAdapter_HostMousePosition,
		.mouse_movement = TieLandruAdapter_HostMouseMovement,
		.mouse_set_position = TieLandruAdapter_HostMouseSetPosition,
		.mouse_show = TieLandruAdapter_HostMouseShow,
		.joystick_count = TieLandruAdapter_HostJoystickCount,
		.joystick_read = TieLandruAdapter_HostJoystickRead,
		.palette_set = TieLandruAdapter_HostPaletteSet,
		.frontend_audio_pump = TieLandruAdapter_FrontendAudioPump,
		.now_us = TieLandruAdapter_HostNowUs,
	};
	host.video = (LandruPlatformVideo) {
		.set_mode = TieLandruAdapter_HostVideoSetMode,
		.lock = TieLandruAdapter_HostVideoLock,
		.unlock = TieLandruAdapter_HostVideoUnlock,
		.copy_to_present_surface = TieLandruAdapter_HostVideoCopyToPresentSurface,
		.present = TieLandruAdapter_HostVideoPresent,
	};
	LandruRenderSink sink = {
		.actor = TieLandruAdapter_RenderActor,
		.draw = TieLandruAdapter_RenderDraw,
		.film = TieLandruAdapter_RenderFilm,
		.text = TieLandruAdapter_RenderText,
		.paint = TieLandruAdapter_RenderPaint,
		.cursor = TieLandruAdapter_RenderCursor,
		.fade = TieLandruAdapter_RenderFade,
		.scene_clock = TieLandruAdapter_RenderSceneClock,
		.is_scene_transition = TieLandruAdapter_RenderIsSceneTransition,
	};
	if (!landru_set_host(&host))
		return false;
	if (!landru_set_render_sink(&sink)) {
		landru_clear_host();
		return false;
	}
	return true;
}

void TieLandruAdapter_Shutdown(void) {
	landru_set_render_sink(NULL);
	landru_clear_host();
}

void TieLandruAdapter_EmitRenderState(void) {
	lactor_emit_render_state();
	lfilm_emit_render_state();
	lcursor_emit_render_state();
	lfade_emit_render_state();
}
