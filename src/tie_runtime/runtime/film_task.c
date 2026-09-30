#include "tie_runtime/runtime/film_task.h"
#include "tie/play1.h"
#include "tie/wavestream_tie98.h"
#include "tie_runtime/audio/music_policy.h"
#include "tie_runtime/snapshot/snapshot_internal.h"
#include <landru/bitmap.h>
#include <landru/memhdl.h>
#include <landru/stream.h>
#include <landru/surface.h>
#include <landru/task.h>
#include <landru/timer.h>
#include <landru/view.h>
#include <landru/viewadd.h>

typedef struct FilmTask {
	SceneHeadStruct* scene_head;
	ResFile* file;
	ResFile* file2;
	int16_t scene;
	bool rate_changed;
	bool is_streaming_active;
	LandruSurfaceSet surface_set;
	bool started;
	bool opened;
} FilmTask;
static bool play1_tie98_music_ends_after_scene(int16_t scene) {
	if (scene >= 251 && scene <= 263)
		return true;

	switch (scene) {
		case 90:
		case 210:
		case 230:
		case 240:
		case 270:
		case 420:
		case 500:
		case 510:
		case 520:
		case 531:
		case 550:
		case 573:
		case 581:
		case 591:
		case 603:
		case 610:
		case 623:
		case 700:
		case 710:
		case 720:
		case 730:
		case 740:
			return true;
		default:
			return false;
	}
}
static LandruTaskStepResult film_step(void* self) {
	FilmTask* task = self;
	if (task->started)
		return LANDRU_TASK_STEP_DONE;
	task->started = true;
	play1_Play1(task->scene_head);
	return LANDRU_TASK_STEP_CONTINUE;
}
static void film_end(void* self) {
	FilmTask* task = self;
	if (!task->opened)
		return;
	/* CLEANUP */
	if (TieMusicPolicy_UsesTie98() && play1_tie98_music_ends_after_scene(task->scene))
		FrontendWaveStream_Shutdown();
	/* Restore frame rate to 20fps for scenes that changed it */
	if (task->rate_changed)
		xtimer_Set_Frame_Rate(20);

	/* Tear down streaming */
	if (task->is_streaming_active && play1_is_streaming) {
		xstream_Unchain_Current_Stream_File(0);
		play1_is_streaming = 0;
		if (play1_read_buffer) {
			xmemhdl_Free_Handle(play1_read_buffer);
			play1_read_buffer = LANDRU_NULL_HANDLE;
		}
		if (play1_last_frame.data)
			xbitmap_Free_Bitmap(&play1_last_frame);
		if (play1_current_frame.data)
			xbitmap_Free_Bitmap(&play1_current_frame);
	}

	xview_Clear_View_Update_Function();
	if (task->file2)
		xres_Close_Resource(task->file2);
	if (task->file)
		xres_Close_Resource(task->file);

	/* Drop the active-film tag — the compositor will fall back to
	 * classic rendering for whatever scene runs next until another
	 * PLAY1 push re-tags. */
#ifdef TIE_MODERN
	TieSnapshotBuilder_SetActiveFilm(NULL, NULL);
#endif
	if (task->surface_set == LANDRU_SURFACE_SVGA)
		(void)xsurface_Select_Surface_Set(LANDRU_SURFACE_VGA);
}
static const LandruTaskVtable film_vtable = { .step = film_step, .end = film_end };
void TieFilm_RunView(ResFile* file, ResFile* file2, int16_t scene, bool rate_changed,
					 bool is_streaming_active, LandruSurfaceSet surface_set) {
	FilmTask* task = landru_task_top();
	task->file = file;
	task->file2 = file2;
	task->scene = scene;
	task->rate_changed = rate_changed;
	task->is_streaming_active = is_streaming_active;
	task->surface_set = surface_set;
	task->opened = true;
	xviewadd_Push_Handle_View_Task();
}
void TieFilm_Begin(SceneHeadStruct* head) {
	TieSnapshotBuilder_SetSceneKind(TIE_SCENE_CUTSCENE);
	TieSnapshotBuilder_SetRedrawModel(TIE_REDRAW_FULL_FRAME);
	FilmTask* task = landru_task_push(&film_vtable);
	if (task)
		task->scene_head = head;
}
