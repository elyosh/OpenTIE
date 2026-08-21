#ifndef LANDRU_VIEW_H
#define LANDRU_VIEW_H

#include <stdbool.h>
#include <stdint.h>

#include <landru/actor.h>
#include <landru/rect.h>

typedef struct {
	Rect frame[4];
	int16_t zstart[4];
	int16_t zstop[4];
	int16_t rel_x[4];
	int16_t rel_y[4];
	int16_t x_vel[4];
	int16_t y_vel[4];
	Rect trackFrame[4];
	Actor* trackActor[4];
	int16_t max_track_xv[4];
	int16_t max_track_yv[4];
	int16_t track_active[4];
	int16_t track_off_x[4];
	int16_t track_off_y[4];
	int16_t clear_view[4];
	uint8_t reserved[48];
	Rect clip_frame;
	int32_t time;
	int16_t refresh_world;
	int16_t clear;
	int16_t step;
	int16_t stepCount;
	int16_t coords;
	int16_t frameCount;
	void (*update)(int32_t);
} ViewStruct;

extern ViewStruct* view_default_gbl;
extern ViewStruct* view_gbl;
extern bool view_module_gbl;

void lview_Create_View_Module(void);
void lview_Destroy_View_Module(void);
void lview_Init_View(ViewStruct* view);
ViewStruct* lview_Alloc_View(void);
void lview_Free_View(ViewStruct* view);
void lview_Free_All_From_View(ViewStruct* view);
void lview_Set_Current_View(ViewStruct* view);
ViewStruct* lview_Get_Current_View(void);
void lview_Move_View(void);
void lview_Origin_To_View(int16_t layer, int16_t* x, int16_t* y);
void lview_View_To_Origin(int16_t layer, int16_t* x, int16_t* y);
void lview_Track_View(int16_t viewId, int16_t snap);
void lview_Track_Actor(int16_t layer, Actor* actor, Rect* bounds, int16_t max_vx, int16_t max_vy);
void lview_Activate_Tracking(int16_t viewId);
void lview_Deactivate_Tracking(int16_t viewId);
void lview_Set_Tracking_Frame(int16_t viewId, Rect* frame);
void lview_Get_Tracking_Frame(int16_t viewId, Rect* outFrame);
void lview_Set_Tracking_Max_Vel(int16_t viewId, int16_t vx, int16_t vy);
void lview_Get_Tracking_Max_Vel(int16_t viewId, int16_t* out_vx, int16_t* out_vy);
void lview_Set_Tracking_Offset(int16_t viewId, int16_t ox, int16_t oy);
void lview_Get_Tracking_Offset(int16_t viewId, int16_t* out_ox, int16_t* out_oy);
void lview_Set_View_Frame(int16_t viewId, Rect* frame);
void lview_Get_View_Frame(int16_t viewId, Rect* outFrame);
int16_t lview_Get_View_Clip_Frame(int16_t viewId, Rect* outFrame);
void lview_Set_Full_View_Clip_Frame(Rect* frame);
void lview_Get_Full_View_Clip_Frame(Rect* outFrame);
void lview_Restore_Full_View_Clip_Frame(void);
void lview_Set_View_ZPlane(int16_t viewId, int16_t z_near, int16_t z_far);
void lview_Get_View_ZPlane(int16_t viewId, int16_t* out_near, int16_t* out_far);
void lview_Set_View_Pos(int16_t viewId, int16_t x, int16_t y);
void lview_Get_View_Pos(int16_t viewId, int16_t* out_x, int16_t* out_y);
void lview_Set_View_Speed(int16_t viewId, int16_t vx, int16_t vy);
void lview_Get_View_Speed(int16_t viewId, int16_t* out_vx, int16_t* out_vy);
void lview_Set_View_Update_Function(void (*updateFunc)(int32_t));
void lview_Get_View_Update_Function(void (**out)(int32_t));
void lview_Clear_View_Update_Function(void);
void lview_Refresh_View(void);
bool lview_Is_View_Erase(int16_t viewId);
void lview_Enable_View_Erase(int16_t viewId);
void lview_Disable_View_Erase(int16_t viewId);
void lview_Enable_Global_View_Erase(void);
void lview_Disable_Global_View_Erase(void);
void lview_Enable_All_View_Erase(void);
void lview_Disable_All_View_Erase(void);
bool lview_Is_View_Copy(int16_t viewId);
void lview_Enable_View_Copy(int16_t viewId);
void lview_Disable_View_Copy(int16_t viewId);
int32_t lview_Get_View_Time(void);

#endif
