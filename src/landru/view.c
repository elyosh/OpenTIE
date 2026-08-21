#include <landru/actor.h>
#include <landru/canvas.h>
#include <landru/film.h>
#include <landru/input.h>
#include <landru/memptr.h>
#include <landru/pal.h>
#include <landru/sound.h>
#include <landru/view.h>

// GLOBAL: TIE 0xD2F68
ViewStruct* view_default_gbl;
// GLOBAL: TIE 0xD2F64
ViewStruct* view_gbl;
// GLOBAL: TIE 0xD2F6C
bool view_module_gbl;

void lview_Create_View_Module(void) {
	view_default_gbl = lmemptr_Alloc_System_Pointer(sizeof(ViewStruct));
	if (!view_default_gbl)
		return;

	view_module_gbl = true;
	lview_Init_View(view_default_gbl);
	view_default_gbl->coords = 0;
	view_default_gbl->frameCount = 0;
	lview_Set_Current_View(view_default_gbl);
}

void lview_Destroy_View_Module(void) {
	if (view_default_gbl)
		lview_Free_View(view_default_gbl);
	view_module_gbl = false;
}

void lview_Init_View(ViewStruct* view) {
	for (int i = 0; i < 4; i++) {
		if (i == 0) {
			lcanvas_Get_Drawing_Canvas_Bounds(&view->frame[i]);
			view->zstart[i] = -32000;
			view->zstop[i] = 32000;
		} else {
			lrect_Set_Rect(&view->frame[i], 0, 0, 0, 0);
			view->zstart[i] = -32767;
			view->zstop[i] = -32767;
		}
		view->rel_x[i] = 0;
		view->rel_y[i] = 0;
		view->x_vel[i] = 0;
		view->y_vel[i] = 0;
		view->trackActor[i] = NULL;
		view->track_active[i] = 0;
		view->clear_view[i] = 3;
	}
	lcanvas_Get_Drawing_Canvas_Bounds(&view->clip_frame);
	view->time = 0;
	view->refresh_world = 0;
	view->clear = 1;
	view->step = 0;
	view->stepCount = 0;
	view->update = NULL;
}

ViewStruct* lview_Alloc_View(void) { return lmemptr_Alloc_System_Pointer(sizeof(ViewStruct)); }

void lview_Free_View(ViewStruct* view) {
	lview_Free_All_From_View(view);
	lmemptr_Free_System_Pointer(view);
}

void lview_Free_All_From_View(ViewStruct* view) {
	(void)view;
	lfilm_Free_Films(lfilm_Ask_Film_List());
	lactor_Free_Actors(lactor_Ask_Actor_List());
	lpal_Free_Palettes(lpal_Ask_Palette_List());
	linput_Free_Input_Lists();
	lsound_Free_Sounds(lsound_Ask_Sound_List());
}

void lview_Set_Current_View(ViewStruct* view) { view_gbl = view; }

ViewStruct* lview_Get_Current_View(void) { return view_gbl; }

void lview_Move_View(void) {
	for (int i = 0; i < 4; i++) {
		if (view_gbl->track_active[i]) {
			lview_Track_View(i, 0);
		} else {
			view_gbl->rel_x[i] += view_gbl->x_vel[i];
			view_gbl->rel_y[i] += view_gbl->y_vel[i];
		}
	}
}

void lview_Origin_To_View(int16_t layer, int16_t* x, int16_t* y) {
	*x -= view_gbl->rel_x[layer];
	*y -= view_gbl->rel_y[layer];
}

void lview_View_To_Origin(int16_t layer, int16_t* x, int16_t* y) {
	*x += view_gbl->rel_x[layer];
	*y += view_gbl->rel_y[layer];
}

void lview_Track_View(int16_t viewId, int16_t snap) {
	if (!view_gbl->trackActor[viewId])
		return;

	int16_t x, y, xf, yf;
	Rect viewFrame;
	lactor_Get_Actor_Pos(view_gbl->trackActor[viewId], &x, &y, &xf, &yf);
	lview_Get_View_Frame(viewId, &viewFrame);

	x = x - ((viewFrame.right - viewFrame.left) >> 1) + view_gbl->track_off_x[viewId];
	y = y - ((viewFrame.bottom - viewFrame.top) >> 1) + view_gbl->track_off_y[viewId];
	lrect_Clip_Point_To_Rect(&view_gbl->trackFrame[viewId], &x, &y);

	int16_t dx = x - view_gbl->rel_x[viewId];
	int16_t dy = y - view_gbl->rel_y[viewId];

	if (!snap) {
		if (dx > view_gbl->max_track_xv[viewId])
			dx = view_gbl->max_track_xv[viewId];
		if (dy > view_gbl->max_track_yv[viewId])
			dy = view_gbl->max_track_yv[viewId];
		if (dx < -view_gbl->max_track_xv[viewId])
			dx = -view_gbl->max_track_xv[viewId];
		if (dy < -view_gbl->max_track_yv[viewId])
			dy = -view_gbl->max_track_yv[viewId];
	}

	view_gbl->rel_x[viewId] += dx;
	view_gbl->rel_y[viewId] += dy;
}

void lview_Track_Actor(int16_t layer, Actor* actor, Rect* bounds, int16_t max_vx, int16_t max_vy) {
	view_gbl->trackActor[layer] = actor;
	lview_Set_Tracking_Frame(layer, bounds);
	lview_Set_Tracking_Max_Vel(layer, max_vx, max_vy);
	int16_t size_x, size_y;
	lactor_Get_Actor_Size(actor, &size_x, &size_y);
	lview_Set_Tracking_Offset(layer, size_x >> 1, size_y >> 1);
	lview_Activate_Tracking(layer);
}

void lview_Activate_Tracking(int16_t viewId) { view_gbl->track_active[viewId] = 1; }

void lview_Deactivate_Tracking(int16_t viewId) { view_gbl->track_active[viewId] = 0; }

void lview_Set_Tracking_Frame(int16_t viewId, Rect* frame) {
	lrect_Copy_Rect(&view_gbl->trackFrame[viewId], frame);
}

void lview_Get_Tracking_Frame(int16_t viewId, Rect* outFrame) {
	lrect_Copy_Rect(outFrame, &view_gbl->trackFrame[viewId]);
}

void lview_Set_Tracking_Max_Vel(int16_t viewId, int16_t vx, int16_t vy) {
	view_gbl->max_track_xv[viewId] = vx;
	view_gbl->max_track_yv[viewId] = vy;
}

void lview_Get_Tracking_Max_Vel(int16_t viewId, int16_t* out_vx, int16_t* out_vy) {
	*out_vx = view_gbl->max_track_xv[viewId];
	*out_vy = view_gbl->max_track_yv[viewId];
}

void lview_Set_Tracking_Offset(int16_t viewId, int16_t ox, int16_t oy) {
	view_gbl->track_off_x[viewId] = ox;
	view_gbl->track_off_y[viewId] = oy;
}

void lview_Get_Tracking_Offset(int16_t viewId, int16_t* out_ox, int16_t* out_oy) {
	*out_ox = view_gbl->track_off_x[viewId];
	*out_oy = view_gbl->track_off_y[viewId];
}

void lview_Set_View_Frame(int16_t viewId, Rect* frame) { lrect_Copy_Rect(&view_gbl->frame[viewId], frame); }

void lview_Get_View_Frame(int16_t viewId, Rect* outFrame) {
	lrect_Copy_Rect(outFrame, &view_gbl->frame[viewId]);
}

int16_t lview_Get_View_Clip_Frame(int16_t viewId, Rect* outFrame) {
	lrect_Copy_Rect(outFrame, &view_gbl->frame[viewId]);
	return lrect_Clip_Rect(outFrame, &view_gbl->clip_frame);
}

void lview_Set_Full_View_Clip_Frame(Rect* frame) { lrect_Copy_Rect(&view_gbl->clip_frame, frame); }

void lview_Get_Full_View_Clip_Frame(Rect* outFrame) { lrect_Copy_Rect(outFrame, &view_gbl->clip_frame); }

void lview_Restore_Full_View_Clip_Frame(void) { lcanvas_Get_Drawing_Canvas_Bounds(&view_gbl->clip_frame); }

void lview_Set_View_ZPlane(int16_t viewId, int16_t z_near, int16_t z_far) {
	view_gbl->zstart[viewId] = z_near;
	view_gbl->zstop[viewId] = z_far;
}

void lview_Get_View_ZPlane(int16_t viewId, int16_t* out_near, int16_t* out_far) {
	*out_near = view_gbl->zstart[viewId];
	*out_far = view_gbl->zstop[viewId];
}

void lview_Set_View_Pos(int16_t viewId, int16_t x, int16_t y) {
	view_gbl->rel_x[viewId] = x;
	view_gbl->rel_y[viewId] = y;
}

void lview_Get_View_Pos(int16_t viewId, int16_t* out_x, int16_t* out_y) {
	*out_x = view_gbl->rel_x[viewId];
	*out_y = view_gbl->rel_y[viewId];
}

void lview_Set_View_Speed(int16_t viewId, int16_t vx, int16_t vy) {
	view_gbl->x_vel[viewId] = vx;
	view_gbl->y_vel[viewId] = vy;
}

void lview_Get_View_Speed(int16_t viewId, int16_t* out_vx, int16_t* out_vy) {
	*out_vx = view_gbl->x_vel[viewId];
	*out_vy = view_gbl->y_vel[viewId];
}

void lview_Set_View_Update_Function(void (*updateFunc)(int32_t)) { view_gbl->update = updateFunc; }

void lview_Get_View_Update_Function(void (**out)(int32_t)) { *out = view_gbl->update; }

void lview_Clear_View_Update_Function(void) { view_gbl->update = NULL; }

void lview_Refresh_View(void) { view_gbl->refresh_world = 1; }

bool lview_Is_View_Erase(int16_t viewId) { return view_gbl->clear_view[viewId] & 1; }

void lview_Enable_View_Erase(int16_t viewId) { view_gbl->clear_view[viewId] |= 1; }

void lview_Disable_View_Erase(int16_t viewId) { view_gbl->clear_view[viewId] &= ~1; }

void lview_Enable_Global_View_Erase(void) { view_gbl->clear = 1; }

void lview_Disable_Global_View_Erase(void) { view_gbl->clear = 0; }

void lview_Enable_All_View_Erase(void) {
	view_gbl->clear = 1;
	for (int16_t i = 0; i < 4; i++)
		lview_Enable_View_Erase(i);
}

void lview_Disable_All_View_Erase(void) {
	view_gbl->clear = 0;
	for (int16_t i = 0; i < 4; i++)
		lview_Disable_View_Erase(i);
}

bool lview_Is_View_Copy(int16_t viewId) { return view_gbl->clear_view[viewId] & 2; }

void lview_Enable_View_Copy(int16_t viewId) { view_gbl->clear_view[viewId] |= 2; }

void lview_Disable_View_Copy(int16_t viewId) { view_gbl->clear_view[viewId] &= ~2; }

int32_t lview_Get_View_Time(void) { return view_gbl->time; }
