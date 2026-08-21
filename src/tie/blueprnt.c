/*
 * BLUEPRNT.C — Ship blueprint viewer screen
 *
 * Displays a rotating 3D hologram of each ship, with nav buttons to
 * cycle through ships and scroll the view pitch, a door to exit back
 * to the concourse, and an info overlay showing the ship name, size,
 * and description text with staggered fade-in animation.
 *
 * The 3D rendering is handled by BPFLIGHT (Open/Close_Flight_Engine).
 * The screen layout uses a film ("blueprnt") with actors assigned by
 * var1: 3=arrow, 5=projector, 10=door background, 15=door,
 * 20=decorative, 25=title overlay.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "landru/actcust.h"
#include "landru/actdelt.h"
#include "landru/actor.h"
#include "landru/cursor.h"
#include "landru/dirty.h"
#include "landru/error.h"
#include "landru/film.h"
#include "landru/font.h"
#include "landru/inpattr.h"
#include "landru/inpcall.h"
#include "landru/input.h"
#include "landru/io.h"
#include "landru/rect.h"
#include "landru/res.h"
#include "landru/surface.h"
#include "landru/vesa.h"
#include "landru/view.h"
#include "landru/viewadd.h"
#include "tie/blueprnt.h"
#include "tie/shellext.h"
#include "tie/shipext.h"
#include "tie/soundext.h"
#include "tie/textext.h"
#include "tie_runtime/runtime/profile.h"
#include <landru/task.h>

#include "tie/bpflight.h"

/* Ship size scaling factor: the engine multiplies the raw dimension
 * by 1605/65536 before display. Result is in whatever unit the
 * "Ship Statistics" panel labels — not anchored in the engine. */
#define SIZE_SCALE_FACTOR 1605

static const char blueprint_str[] = "blueprnt.lfd";
static const char blueprint_film_name[] = "blueprnt";

/* Module state */
static ResFile* blueprint_file;
static Film* blueprint_film;
static Input* world_input;
static Input* button_input[4];
static Input* door_input;
static Actor* ship_name_actor;
static Actor* ship_comp_actor;
static Actor* ship_info_actor;
// GLOBAL: TIE 0xF5790
static Actor* arrow_actor;
// GLOBAL: TIE 0xF5980
static Actor* door_actor;
// GLOBAL: TIE 0xF5990
static Actor* door_back_actor;
// GLOBAL: TIE 0xF5968
static Actor* title_actor;
static int16_t blueprint_info_ship;
static int16_t blueprint_info_time;
static int32_t blueprint_info_size;
static bool blueprint_svga;

/* Forward declarations (referenced by film_Blueprint_Callback before definition) */
static void blueprnt_user_Blueprint_Projector(Actor* the_actor, int32_t time);
static void blueprnt_user_Blueprint_Door(Actor* the_actor, int32_t time);
static int blueprnt_draw_Blueprint_Title(Actor* the_actor, Rect* draw_rect, Rect* clip_rect, int16_t off_x,
										 int16_t off_y, int16_t refresh);

static int32_t scale_and_round_ship_size(int32_t extent) {
	int32_t raw = (int32_t)(((int64_t)extent * SIZE_SCALE_FACTOR) >> 16);
	if (raw < 100)
		return 5 * ((raw + 2) / 5);
	return 50 * ((raw + 25) / 50);
}

static int32_t compute_ship_size(void) {
	if (blueprint_svga)
		return scale_and_round_ship_size(tie98_preview_primary_model_max_extent());

	const uint8_t* data = (const uint8_t*)bpflight_fltobj_data;
	const uint16_t dimension = *(const uint16_t*)(data + 12);
	const uint8_t shift = data[32];
	const int32_t extent = (int32_t)((uint32_t)(dimension / 2) << shift);
	return scale_and_round_ship_size(extent);
}

/* ------------------------------------------------------------------ */

// FUNCTION: TIE 0x6E3D0
static void blueprnt_end_Blueprint_View(int32_t time) {
	if (time == 0 && !lcursor_Is_Cursor_Visible())
		lcursor_Show_Cursor();
}

/* ------------------------------------------------------------------ */

// FUNCTION: TIE 0x6E3E4; TIE98 0x4045B0
static int16_t blueprnt_film_Blueprint_Callback(Film* the_film, FilmObject* film_object) {
	if (blueprint_svga && film_object->id == FTC_PALETTE) {
		lfilm_Rewind_Palette_Film(the_film, film_object, (void*)(film_object + 1));
		return 0;
	}
	if (film_object->id != 3)
		return 0;

	lfilm_Rewind_Actor_Film(the_film, film_object, (void*)(film_object + 1));
	Actor* the_actor = (Actor*)film_object->object;
	int16_t var1 = the_actor->var1;

	switch (var1) {
		case 3:
			arrow_actor = the_actor;
			break;
		case 5:
			lactor_Set_Actor_User_Function(the_actor, blueprnt_user_Blueprint_Projector);
			break;
		case 10:
			lactor_Non_Refreshable_Actor(the_actor);
			door_back_actor = the_actor;
			break;
		case 15:
			lactor_Set_Actor_User_Function(the_actor, blueprnt_user_Blueprint_Door);
			lactor_Non_Refreshable_Actor(the_actor);
			door_actor = the_actor;
			break;
		case 20:
			lactor_Non_Refreshable_Actor(the_actor);
			break;
		case 25:
			title_actor = the_actor;
			lactor_Set_Actor_Draw_Function(title_actor, (lactorDrawFunc)blueprnt_draw_Blueprint_Title);
			break;
	}

	return 0;
}

/* ------------------------------------------------------------------ */

// FUNCTION: TIE 0x6E4B8
static int16_t blueprnt_iupdate_Blueprint(Input* input, Rect* draw_rect, Rect* clip_rect, int16_t active,
										  uint8_t mouseState, uint8_t prevMouseState, int16_t key,
										  int16_t prevKey) {
	(void)draw_rect;
	(void)clip_rect;
	(void)key;
	(void)prevKey;

	if (active)
		return 0;

	if (input->id) {
		/* Nav buttons (id 1-4) */
		if (mouseState == 3 || prevMouseState == 3) {
			linpattr_Clear_Input_Flag1(input);
			linpattr_Selected_Input(input);
			lactor_Hide_Actor(arrow_actor);
		}
		if (mouseState == 1 || prevMouseState == 1) {
			linpattr_Set_Input_Flag1(input);
			lactor_Show_Actor(arrow_actor);
			lactor_Set_Actor_State(arrow_actor, input->id - 1, 0);
			if (!blueprint_svga)
				arrow_actor->x = input->id > 2 ? 19 : -32;
			soundext_Play_SFX(sfxButton, 80);
		}
	} else {
		/* World background (id 0) — select on click */
		if (mouseState == 1 || prevMouseState == 1)
			linpattr_Selected_Input(input);
	}
	/* Binary BLUEPRNT_iupdate_Blueprint at 0x6e56f returns 1. A 0 return
	 * makes XINPCALL_Update_Mouse_Down call Set_InputActive_Ignore and
	 * swallow every subsequent mouse event on this input. */
	return 1;
}

/* ------------------------------------------------------------------ */

// FUNCTION: TIE 0x6E588
static void blueprnt_iuser_Blueprint(Input* input, int32_t time) {
	(void)time;

	if (!linpattr_Get_Input_Selected(input))
		return;

	switch (input->id) {
		case 1:
			shipext_Last_Blueprint_Ship();
			break;
		case 2:
			/* Rotate pitch down */
			bpflight_pivotpitch[2] += 0x1000;
			break;
		case 3:
			shipext_Next_Blueprint_Ship();
			break;
		case 4:
			/* Rotate pitch up */
			bpflight_pivotpitch[2] -= 0x1000;
			break;
	}
}

/* ------------------------------------------------------------------ */

// FUNCTION: TIE 0x6E5DC
static int16_t blueprnt_iupdate_Blueprint_Door(Input* input, Rect* draw_rect, Rect* clip_rect, int16_t active,
											   uint8_t mouseState, uint8_t prevMouseState, int16_t key,
											   int16_t prevKey) {
	(void)draw_rect;
	(void)clip_rect;
	(void)key;
	(void)prevKey;

	if (active)
		return 0;

	door_actor->var1 = 1;
	if (mouseState == 3 || prevMouseState == 3)
		linpattr_Selected_Input(input);
	/* Binary BLUEPRNT_iupdate_Blueprint_Door at 0x6e605 returns 1. */
	return 1;
}

/* ------------------------------------------------------------------ */

// FUNCTION: TIE 0x6E610
static void blueprnt_iuser_Blueprint_Door(Input* input, int32_t time) {
	(void)time;
	if (linpattr_Get_Input_Selected(input))
		lerror_Set_Landru_Exit(SCENE_MAIN_MENU);
}

/* ------------------------------------------------------------------ */

/*
 * Projector actor callback. Animates the holographic ship rotation.
 * Each ship occupies 11 animation frames. On first frame, jumps to
 * the target position. On subsequent frames, interpolates by ±1.
 */
// FUNCTION: TIE 0x6E62C
static void blueprnt_user_Blueprint_Projector(Actor* the_actor, int32_t time) {
	int16_t new_state;

	if (time == 0) {
		new_state = 11 * the_actor->var2;
	} else {
		int16_t current_pos = the_actor->state % 11;
		int16_t target_pos = shipext_Get_Blueprint_Ship() % 11;
		if (current_pos == target_pos)
			return;
		if (current_pos >= target_pos)
			new_state = the_actor->state - 1;
		else
			new_state = the_actor->state + 1;
	}
	lactor_Set_Actor_State(the_actor, new_state, 0);
}

/* ------------------------------------------------------------------ */

/*
 * Door actor callback. Opens on var1 set (by iupdate_Blueprint_Door),
 * closes when var1 is 0. Controls title_actor->var2 to show/hide the
 * "Return to Concourse" label.
 */
// FUNCTION: TIE 0x6E6A8
static void blueprnt_user_Blueprint_Door(Actor* the_actor, int32_t time) {
	if (time == 0)
		the_actor->var1 = 0;

	if (the_actor->var1) {
		/* Opening */
		if (the_actor->state == 0)
			soundext_Play_SFX(sfxSmallDoorOpen, 80);
		if (the_actor->state < the_actor->arraySize - 1) {
			lactor_Set_Actor_State(the_actor, the_actor->state + 1, 0);
			lactor_Refresh_Actor(door_back_actor);
			lactor_Refresh_Actor(the_actor);
		}
		the_actor->var1 = 0;
		title_actor->var2 = 1;
	} else {
		/* Closing */
		if (the_actor->state > 0) {
			lactor_Set_Actor_State(the_actor, the_actor->state - 1, 0);
			lactor_Refresh_Actor(door_back_actor);
			lactor_Refresh_Actor(the_actor);
			if (the_actor->state == 0)
				soundext_Play_SFX(sfxSmallDoorShut, 80);
		}
		title_actor->var2 = 0;
	}
}

/* ------------------------------------------------------------------ */

// FUNCTION: TIE 0x6E768; TIE98 0x4049C0
static int blueprnt_draw_Blueprint_Text(Actor* the_actor, Rect* draw_rect, Rect* clip_rect, int16_t off_x,
										int16_t off_y, int16_t refresh) {
	(void)off_x;
	(void)off_y;
	char text[76];

	if (!refresh)
		return 0;

	if (the_actor->id)
		textext_Copy_Text(text, txtBlueRotate);
	else
		shipext_Get_Blueprint_Ship_Name(text);

	lfont_Print_Centered_Text(text, draw_rect, 15, blueprint_svga ? 2 : 0);

	if (lactor_Is_Actor_Dirty(the_actor))
		ldirty_Dirty_Rect(clip_rect);

	return 1;
}

/* ------------------------------------------------------------------ */

// FUNCTION: TIE 0x6E7CC
static void blueprnt_user_Blueprint_Info(Actor* the_actor, int32_t time) {
	(void)the_actor;

	if (time && blueprint_info_ship == shipext_Get_Blueprint_Ship()) {
		blueprint_info_time = (blueprint_info_time + 1) % 320;
	} else {
		blueprint_info_ship = shipext_Get_Blueprint_Ship();
		blueprint_info_time = 0;
		blueprint_info_size = compute_ship_size();
	}
}

/* ------------------------------------------------------------------ */

/*
 * Clamp a fade value to [0, 7], with fade-out starting at time 142.
 */
static int16_t fade_clamp(int16_t raw, int16_t info_time) {
	int16_t fade = raw;
	if (fade > 7)
		fade = 7;
	if (info_time >= 142)
		fade = 148 - info_time;
	return fade;
}

// FUNCTION: TIE 0x6E820; TIE98 0x404A80
static int blueprnt_draw_Blueprint_Info(Actor* the_actor, Rect* draw_rect, Rect* clip_rect, int16_t off_x,
										int16_t off_y, int16_t refresh) {
	(void)the_actor;
	(void)clip_rect;
	(void)off_x;
	(void)off_y;

	char fmt[64];
	char str[64];
	Rect dst;

	if (!refresh || blueprint_info_time > 148)
		return 1;

	int16_t t_name = blueprint_info_time - 8;
	const uint16_t font_id = blueprint_svga ? 3 : 1;
	const int16_t line_height = blueprint_svga ? lfont_Get_FontID_Height(font_id) : 8;
	if (t_name >= 0) {
		lrect_Copy_Rect(&dst, draw_rect);
		dst.bottom = dst.top + line_height;

		int16_t fade = fade_clamp(t_name, blueprint_info_time);
		shipext_Get_Blueprint_Ship_Name((char*)str);
		lfont_Print_Centered_Text(str, &dst, fade + 24, font_id);

		if (t_name >= 2) {
			int16_t t_size = t_name - 2;
			lrect_Offset_Rect(&dst, 0, line_height);
			fade = fade_clamp(t_size, blueprint_info_time);
			textext_Copy_Text(fmt, txtBlueMeters);
			snprintf((char*)str, sizeof(str), fmt, blueprint_info_size);
			lfont_Print_Centered_Text(str, &dst, fade + 24, font_id);
		}
	}

	int16_t t_lines = blueprint_info_time - 16;
	if (t_lines >= 0) {
		int16_t num_lines = shipext_Get_Num_Blueprint_Ship_Lines();
		lrect_Copy_Rect(&dst, draw_rect);
		if (blueprint_svga) {
			dst.bottom = 310;
			dst.top = dst.bottom - line_height;
		} else {
			dst.top = 152;
			dst.bottom = 160;
		}
		lrect_Offset_Rect(&dst, 0, -line_height * (num_lines + 1));

		for (int16_t i = 0; i < num_lines; i++) {
			if (t_lines < 0)
				break;
			int16_t fade = fade_clamp(t_lines, blueprint_info_time);
			shipext_Get_Blueprint_Ship_Line((char*)str, i);
			lfont_Print_Centered_Text(str, &dst, fade + 24, font_id);
			lrect_Offset_Rect(&dst, 0, line_height);
			t_lines -= 4;
		}
	}

	return 1;
}

/* ------------------------------------------------------------------ */

// FUNCTION: TIE 0x6EA5C
static int blueprnt_draw_Blueprint_Title(Actor* the_actor, Rect* draw_rect, Rect* clip_rect, int16_t off_x,
										 int16_t off_y, int16_t refresh) {
	if (!refresh || !the_actor->var2)
		return 1;

	lactdelt_Draw_Delta_Actor(the_actor, draw_rect, clip_rect, off_x, off_y, refresh);

	Rect bounds;
	lactor_Get_Actor_Bounds(the_actor, &bounds);
	const char* text = textext_Get_Text(txtTourMainMenu);
	lfont_Print_Centered_Text(text, &bounds, 15, blueprint_svga ? 2 : 0);

	return 1;
}

/* ------------------------------------------------------------------ */

// FUNCTION: TIE 0x6EAB4
int16_t blueprnt_Flight_Object_Size(void) { return (int16_t)compute_ship_size(); }

/* ------------------------------------------------------------------ */

typedef enum {
	BLUEPRNT_PHASE_BEGIN = 0,
	BLUEPRNT_PHASE_CLEANUP = 1,
} BlueprntPhase;

typedef struct BlueprntTask {
	SceneHeadStruct* the_head;
	BlueprntPhase phase;
} BlueprntTask;

static LandruTaskStepResult blueprnt_task_step(void* self) {
	BlueprntTask* t = (BlueprntTask*)self;

	if (t->phase == BLUEPRNT_PHASE_BEGIN) {
		Rect frame;
		blueprint_svga = TieProfile_FrontendId() == TIE_FRONTEND_PROFILE_TIE98;
		const int16_t width = blueprint_svga ? 640 : 320;
		const int16_t height = blueprint_svga ? 480 : 200;
		if (blueprint_svga) {
			(void)lsurface_Select_Surface_Set(LANDRU_SURFACE_SVGA);
			lview_Init_View(lview_Get_Current_View());
			lvesa_Erase_Video(16);
		}

		lio_Set_Mouse_Position(blueprint_svga ? 512 : 256, blueprint_svga ? 352 : 156);

		blueprint_file = shellext_Open_Empire_Resource(blueprint_str);
		lviewadd_Clear_View();
		lview_Disable_All_View_Erase();

		lrect_Set_Rect(&frame, 0, 0, width, height);
		blueprint_film =
			lfilm_Res_Callback_Film(blueprint_film_name, &frame, 0, 0, 0, blueprnt_film_Blueprint_Callback);
		lfilm_Set_Film_Def_Palette(blueprint_film, t->the_head->def_palette);

		/* World input (full screen) */
		lrect_Set_Rect(&frame, 0, 0, width, height);
		world_input = linput_Alloc_Input(NULL, &frame, 0, 0);

		/* Navigation buttons: previous ship, pitch down, next ship, pitch up. */
		Rect btn_rects[4];
		if (blueprint_svga) {
			lrect_Set_Rect(&btn_rects[0], 203, 350, 255, 380);
			lrect_Set_Rect(&btn_rects[1], 203, 385, 255, 415);
			lrect_Set_Rect(&btn_rects[2], 553, 350, 605, 380);
			lrect_Set_Rect(&btn_rects[3], 553, 385, 605, 415);
		} else {
			lrect_Set_Rect(&btn_rects[0], 62, 150, 92, 164);
			lrect_Set_Rect(&btn_rects[1], 62, 165, 92, 178);
			lrect_Set_Rect(&btn_rects[2], 263, 150, 295, 164);
			lrect_Set_Rect(&btn_rects[3], 263, 165, 295, 178);
		}
		for (int16_t i = 0; i < 4; i++) {
			button_input[i] = linput_Alloc_Input(world_input, &btn_rects[i], 0, 0);
			linpattr_Set_Input_Update_Function(button_input[i], blueprnt_iupdate_Blueprint);
			linpattr_Set_Input_User_Function(button_input[i], blueprnt_iuser_Blueprint);
			button_input[i]->id = i + 1;
		}

		/* Door input (left panel) */
		if (blueprint_svga)
			lrect_Set_Rect(&frame, 0, 73, 159, 296);
		else
			lrect_Set_Rect(&frame, 0, 30, 80, 116);
		door_input = linput_Alloc_Input(world_input, &frame, 0, 0);
		linpattr_Set_Input_Update_Function(door_input, blueprnt_iupdate_Blueprint_Door);
		linpattr_Set_Input_User_Function(door_input, blueprnt_iuser_Blueprint_Door);
		door_input->mouseUsage = 4;

		/* Ship name text actor */
		if (blueprint_svga)
			lrect_Set_Rect(&frame, 266, 355, 539, 375);
		else
			lrect_Set_Rect(&frame, 98, 153, 257, 161);
		ship_name_actor = lactcust_Alloc_Custom_Actor(0, &frame, 0, 0, 0);
		lactor_Set_Actor_Draw_Function(ship_name_actor, (lactorDrawFunc)blueprnt_draw_Blueprint_Text);
		ship_name_actor->id = 0;

		/* Component text actor ("Rotate Craft") */
		if (blueprint_svga)
			lrect_Set_Rect(&frame, 266, 392, 539, 412);
		else
			lrect_Set_Rect(&frame, 98, 167, 257, 175);
		ship_comp_actor = lactcust_Alloc_Custom_Actor(0, &frame, 0, 0, 0);
		lactor_Set_Actor_Draw_Function(ship_comp_actor, (lactorDrawFunc)blueprnt_draw_Blueprint_Text);
		ship_comp_actor->id = 1;

		/* Ship info overlay actor */
		if (blueprint_svga)
			lrect_Set_Rect(&frame, 222, 75, 570, 310);
		else
			lrect_Set_Rect(&frame, 131, 30, 278, 200);
		ship_info_actor = lactcust_Alloc_Custom_Actor(0, &frame, 0, 0, 10);
		lactor_Set_Actor_User_Function(ship_info_actor, blueprnt_user_Blueprint_Info);
		lactor_Set_Actor_Draw_Function(ship_info_actor, (lactorDrawFunc)blueprnt_draw_Blueprint_Info);

		shipext_Open_Blueprint_Ships();
		bpflight_Open_Flight_Engine(3);

		/* Push the modal view task */
		lview_Set_View_Update_Function(blueprnt_end_Blueprint_View);
		lviewadd_Push_Handle_View_Task();

		t->phase = BLUEPRNT_PHASE_CLEANUP;
		return LANDRU_TASK_STEP_CONTINUE;
	}

	/* CLEANUP */
	linpcall_Clear_Active_Input();
	lview_Clear_View_Update_Function();
	bpflight_Close_Flight_Engine();
	shipext_Close_Blueprint_Ships();
	lview_Enable_All_View_Erase();

	if (lcursor_Is_Cursor_Visible())
		lcursor_Hide_Cursor();

	lres_Close_Resource(blueprint_file);
	if (blueprint_svga) {
		lvesa_Erase_Video(16);
		lviewadd_Clear_View();
		(void)lsurface_Select_Surface_Set(LANDRU_SURFACE_VGA);
	}
	return LANDRU_TASK_STEP_DONE;
}

static const LandruTaskVtable blueprnt_task_vt = {
	.step = blueprnt_task_step,
};

void blueprnt_Push_Blueprint_Task(SceneHeadStruct* the_head) {
	BlueprntTask* t = (BlueprntTask*)landru_task_push(&blueprnt_task_vt);
	if (!t)
		return;
	t->the_head = the_head;
	t->phase = BLUEPRNT_PHASE_BEGIN;
}
