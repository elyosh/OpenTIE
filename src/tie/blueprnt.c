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

#include "tie/blueprnt.h"
#ifdef TIE_MODERN
#include "tie_runtime/runtime/blueprint_task.h"
#endif
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
#include "tie/edition.h"
#include "tie/shellext.h"
#include "tie/shipext.h"
#include "tie/soundext.h"
#include "tie/textext.h"
#ifdef TIE_MODERN
#include "tie_runtime/runtime/profile.h"
#endif

#include "tie/bpflight.h"
#include "tie/modelbounds.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Ship size scaling factor: the engine multiplies the raw dimension
 * by 1605/65536 before display. Result is in whatever unit the
 * "Ship Statistics" panel labels — not anchored in the engine. */
enum {
	SIZE_SCALE_FACTOR = 1605,
};

/* Resource names: [0] = LFD file, [1] = film. */
// GLOBAL: TIE95 0xCE708
// GLOBAL: TIE98 0x4DF1B8
static const char blueprint_str[2][20] = { "blueprnt.lfd", "blueprnt" };

/* Module state */
// GLOBAL: TIE95 0xF598C
// GLOBAL: TIE98 0x4FA5B8
static ResFile* blueprint_file;
// GLOBAL: TIE95 0xF5994
// GLOBAL: TIE98 0x4FA5B0
static Film* blueprint_film;
// GLOBAL: TIE95 0xF5970
// GLOBAL: TIE98 0x4FA5A4
static Input* world_input;
// GLOBAL: TIE95 0xF5958
// GLOBAL: TIE98 0x4FA5C8
static Input* button_input[4];
// GLOBAL: TIE95 0xF5974
// GLOBAL: TIE98 0x4FA55C
static Input* door_input;
// GLOBAL: TIE95 0xF597C
// GLOBAL: TIE98 0x4FA5D8
static Actor* ship_name_actor;
// GLOBAL: TIE95 0xF5988
// GLOBAL: TIE98 0x4FA5E0
static Actor* ship_comp_actor;
// GLOBAL: TIE95 0xF5978
// GLOBAL: TIE98 0x4FA5AC
static Actor* ship_info_actor;
// GLOBAL: TIE95 0xF5984
// GLOBAL: TIE98 0x4FA5BC
static Actor* arrow_actor;
// GLOBAL: TIE95 0xF5980
// GLOBAL: TIE98 0x4FA5A0
static Actor* door_actor;
// GLOBAL: TIE95 0xF5990
// GLOBAL: TIE98 0x4FA558
static Actor* door_back_actor;
// GLOBAL: TIE95 0xF5968
// GLOBAL: TIE98 0x4FA5DC
static Actor* title_actor;
// GLOBAL: TIE95 0xF5998
// GLOBAL: TIE98 0x4FA5B4
static int16_t blueprint_info_ship;
// GLOBAL: TIE95 0xF599A
// GLOBAL: TIE98 0x4FA5A8
static int16_t blueprint_info_time;
// GLOBAL: TIE95 0xF596C
// GLOBAL: TIE98 0x4FA5C0
static int32_t blueprint_info_size;

/* Forward declarations (referenced by film_Blueprint_Callback before definition) */
static void blueprnt_user_Blueprint_Projector(Actor* the_actor, int32_t time);
static void blueprnt_user_Blueprint_Door(Actor* the_actor, int32_t time);
static int16_t blueprnt_draw_Blueprint_Title(Actor* the_actor, Rect* draw_rect, Rect* clip_rect,
											 int16_t off_x, int16_t off_y, int16_t refresh);

/* ------------------------------------------------------------------ */

// FUNCTION: TIE95 0x6E3D0
static void blueprnt_end_Blueprint_View(int32_t time) {
	if (time == 0 && !xcursor_Is_Cursor_Visible())
		xcursor_Show_Cursor();
}

/* ------------------------------------------------------------------ */

// FUNCTION: TIE95 0x6E3E4
// FUNCTION: TIE98 0x4045B0
static int16_t blueprnt_film_Blueprint_Callback(Film* the_film, FilmObject* film_object) {
	Actor* the_actor;
	int16_t var1;

	if (TIE_FRONTEND_TIE98 && film_object->id == FTC_PALETTE) {
		xfilm_Rewind_Palette_Film(the_film, film_object, (void*)(film_object + 1));
		return 0;
	}
	if (film_object->id != 3)
		return 0;

	xfilm_Rewind_Actor_Film(the_film, film_object, (void*)(film_object + 1));
	the_actor = (Actor*)film_object->object;
	var1 = the_actor->var1;

	switch (var1) {
		case 3:
			arrow_actor = the_actor;
			break;
		case 5:
			xactor_Set_Actor_User_Function(the_actor, blueprnt_user_Blueprint_Projector);
			break;
		case 10:
			xactor_Non_Refreshable_Actor(the_actor);
			door_back_actor = the_actor;
			break;
		case 15:
			xactor_Set_Actor_User_Function(the_actor, blueprnt_user_Blueprint_Door);
			xactor_Non_Refreshable_Actor(the_actor);
			door_actor = the_actor;
			break;
		case 20:
			xactor_Non_Refreshable_Actor(the_actor);
			break;
		case 25:
			title_actor = the_actor;
			xactor_Set_Actor_Draw_Function(title_actor, blueprnt_draw_Blueprint_Title);
			break;
	}

	return 0;
}

/* ------------------------------------------------------------------ */

// FUNCTION: TIE95 0x6E4B8
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
			xinpattr_Clear_Input_Flag1(input);
			xinpattr_Selected_Input(input);
			xactor_Hide_Actor(arrow_actor);
		}
		if (mouseState == 1 || prevMouseState == 1) {
			xinpattr_Set_Input_Flag1(input);
			xactor_Show_Actor(arrow_actor);
			xactor_Set_Actor_State(arrow_actor, input->id - 1, 0);
			if (!TIE_FRONTEND_TIE98)
				arrow_actor->x = input->id > 2 ? 19 : -32;
			soundext_Play_SFX(sfxButton, 80);
		}
	} else {
		/* World background (id 0) — select on click */
		if (mouseState == 1 || prevMouseState == 1)
			xinpattr_Selected_Input(input);
	}
	/* Binary BLUEPRNT_iupdate_Blueprint at 0x6e56f returns 1. A 0 return
	 * makes XINPCALL_Update_Mouse_Down call Set_InputActive_Ignore and
	 * swallow every subsequent mouse event on this input. */
	return 1;
}

/* ------------------------------------------------------------------ */

// FUNCTION: TIE95 0x6E588
static void blueprnt_iuser_Blueprint(Input* input, int32_t time) {
	(void)time;

	if (!xinpattr_Get_Input_Selected(input))
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

// FUNCTION: TIE95 0x6E5DC
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
		xinpattr_Selected_Input(input);
	/* Binary BLUEPRNT_iupdate_Blueprint_Door at 0x6e605 returns 1. */
	return 1;
}

/* ------------------------------------------------------------------ */

// FUNCTION: TIE95 0x6E610
static void blueprnt_iuser_Blueprint_Door(Input* input, int32_t time) {
	(void)time;
	if (xinpattr_Get_Input_Selected(input))
		xerror_Set_Landru_Exit(SCENE_MAIN_MENU);
}

/* ------------------------------------------------------------------ */

/*
 * Projector actor callback. Animates the holographic ship rotation.
 * Each ship occupies 11 animation frames. On first frame, jumps to
 * the target position. On subsequent frames, interpolates by ±1.
 */
// FUNCTION: TIE95 0x6E62C
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
	xactor_Set_Actor_State(the_actor, new_state, 0);
}

/* ------------------------------------------------------------------ */

/*
 * Door actor callback. Opens on var1 set (by iupdate_Blueprint_Door),
 * closes when var1 is 0. Controls title_actor->var2 to show/hide the
 * "Return to Concourse" label.
 */
// FUNCTION: TIE95 0x6E6A8
static void blueprnt_user_Blueprint_Door(Actor* the_actor, int32_t time) {
	if (time == 0)
		the_actor->var1 = 0;

	if (the_actor->var1) {
		/* Opening */
		if (the_actor->state == 0)
			soundext_Play_SFX(sfxSmallDoorOpen, 80);
		if (the_actor->state < the_actor->arraySize - 1) {
			xactor_Set_Actor_State(the_actor, the_actor->state + 1, 0);
			xactor_Refresh_Actor(door_back_actor);
			xactor_Refresh_Actor(the_actor);
		}
		the_actor->var1 = 0;
		title_actor->var2 = 1;
	} else {
		/* Closing */
		if (the_actor->state > 0) {
			xactor_Set_Actor_State(the_actor, the_actor->state - 1, 0);
			xactor_Refresh_Actor(door_back_actor);
			xactor_Refresh_Actor(the_actor);
			if (the_actor->state == 0)
				soundext_Play_SFX(sfxSmallDoorShut, 80);
		}
		title_actor->var2 = 0;
	}
}

/* ------------------------------------------------------------------ */

// FUNCTION: TIE95 0x6E768
// FUNCTION: TIE98 0x4049C0
static int16_t blueprnt_draw_Blueprint_Text(Actor* the_actor, Rect* draw_rect, Rect* clip_rect, int16_t off_x,
											int16_t off_y, int16_t refresh) {
	char text[76];

	(void)off_x;
	(void)off_y;

	if (!refresh)
		return 0;

	if (the_actor->id)
		textext_Copy_Text(text, txtBlueRotate);
	else
		shipext_Get_Blueprint_Ship_Name(text);

	xfont_Print_Centered_Text(text, draw_rect, TIE_FRONTEND_EDITION(0, 2), 15);

	if (xactor_Is_Actor_Dirty(the_actor))
		xdirty_Dirty_Rect(clip_rect);

	return 1;
}

/* ------------------------------------------------------------------ */

// FUNCTION: TIE95 0x6E7CC
static void blueprnt_user_Blueprint_Info(Actor* the_actor, int32_t time) {
	(void)the_actor;

	if (time && blueprint_info_ship == shipext_Get_Blueprint_Ship()) {
		blueprint_info_time = (blueprint_info_time + 1) % 320;
	} else {
		blueprint_info_ship = shipext_Get_Blueprint_Ship();
		blueprint_info_time = 0;
		blueprint_info_size = blueprnt_Flight_Object_Size();
	}
}

/* ------------------------------------------------------------------ */

// FUNCTION: TIE95 0x6E820
// FUNCTION: TIE98 0x404A80
static int16_t blueprnt_draw_Blueprint_Info(Actor* the_actor, Rect* draw_rect, Rect* clip_rect, int16_t off_x,
											int16_t off_y, int16_t refresh) {
	char fmt[64], str[64];
	Rect dst;
	int16_t t_name, t_lines, line_height;
	uint16_t font_id;

	(void)the_actor;
	(void)clip_rect;
	(void)off_x;
	(void)off_y;

	if (!refresh || blueprint_info_time > 148)
		return 1;

	t_name = blueprint_info_time - 8;
	if (TIE_FRONTEND_TIE98) {
		font_id = 3;
		line_height = xfont_Get_FontID_Height(font_id);
	} else {
		font_id = 1;
		line_height = 8;
	}
	if (t_name >= 0) {
		int16_t fade;
		xrect_Copy_Rect(&dst, draw_rect);
		dst.bottom = dst.top + line_height;

		fade = t_name;
		if (fade > 7)
			fade = 7;
		if (blueprint_info_time >= 142)
			fade = 148 - blueprint_info_time;
		shipext_Get_Blueprint_Ship_Name((char*)str);
		xfont_Print_Centered_Text(str, &dst, font_id, fade + 24);

		if (t_name >= 2) {
			int16_t t_size = t_name - 2;
			xrect_Offset_Rect(&dst, 0, line_height);
			fade = t_size;
			if (fade > 7)
				fade = 7;
			if (blueprint_info_time >= 142)
				fade = 148 - blueprint_info_time;
			textext_Copy_Text(fmt, txtBlueMeters);
			snprintf((char*)str, sizeof(str), fmt, blueprint_info_size);
			xfont_Print_Centered_Text(str, &dst, font_id, fade + 24);
		}
	}

	t_lines = blueprint_info_time - 16;
	if (t_lines >= 0) {
		int16_t num_lines = shipext_Get_Num_Blueprint_Ship_Lines();
		int16_t i;
		xrect_Copy_Rect(&dst, draw_rect);
		if (TIE_FRONTEND_TIE98) {
			dst.bottom = 310;
			dst.top = dst.bottom - line_height;
		} else {
			dst.top = 152;
			dst.bottom = 160;
		}
		xrect_Offset_Rect(&dst, 0, -line_height * (num_lines + 1));

		for (i = 0; i < num_lines; i++) {
			int16_t fade;
			if (t_lines < 0)
				break;
			fade = t_lines;
			if (fade > 7)
				fade = 7;
			if (blueprint_info_time >= 142)
				fade = 148 - blueprint_info_time;
			shipext_Get_Blueprint_Ship_Line((char*)str, i);
			xfont_Print_Centered_Text(str, &dst, font_id, fade + 24);
			xrect_Offset_Rect(&dst, 0, line_height);
			t_lines -= 4;
		}
	}

	return 1;
}

/* ------------------------------------------------------------------ */

// FUNCTION: TIE95 0x6EA5C
static int16_t blueprnt_draw_Blueprint_Title(Actor* the_actor, Rect* draw_rect, Rect* clip_rect,
											 int16_t off_x, int16_t off_y, int16_t refresh) {
	Rect bounds;
	const char* text;

	if (!refresh || !the_actor->var2)
		return 1;

	xactdelt_Draw_Delta_Actor(the_actor, draw_rect, clip_rect, off_x, off_y, refresh);

	xactor_Get_Actor_Bounds(the_actor, &bounds);
	text = textext_Get_Text(txtTourMainMenu);
	xfont_Print_Centered_Text(text, &bounds, TIE_FRONTEND_EDITION(0, 2), 15);

	return 1;
}

/* ------------------------------------------------------------------ */

// FUNCTION: TIE95 0x6EAB4
// FUNCTION: TIE98 0x404D00
int32_t blueprnt_Flight_Object_Size(void) {
	int32_t extent;
	int32_t size;
#if !defined(TIE_MODERN) && defined(TIE98)
	extent = modelbounds_getmaxextent(0);
#else
#ifdef TIE_MODERN
	if (TieProfile_UsesTie98Frontend()) {
		extent = tie98_preview_primary_model_max_extent();
	} else
#endif
	{
		const uint8_t* data = (const uint8_t*)bpflight_fltobj_data;
		uint16_t dimension = *(const uint16_t*)(data + 12);
		uint8_t shift = data[32];
		extent = (int32_t)((uint32_t)(dimension / 2) << (shift & 31));
	}
#endif
	/* Retail keeps the low 32 bits of the product before the signed shift. */
	size = (int32_t)((uint32_t)extent * SIZE_SCALE_FACTOR) >> 16;
	if (size < 100)
		return 5 * ((size + 2) / 5);
	return 50 * ((size + 25) / 50);
}

/* ------------------------------------------------------------------ */

// FUNCTION: TIE95 0x6E0E0
// FUNCTION: TIE98 0x404220
int16_t blueprnt_Blueprint(SceneHeadStruct* the_head) {
	Rect frame;
	int16_t i;
	xio_Set_Mouse_Position(TIE_FRONTEND_EDITION(256, 512), TIE_FRONTEND_EDITION(156, 352));

	blueprint_file = shellext_Open_Empire_Resource(blueprint_str[0]);
	xviewadd_Clear_View();
	xview_Disable_All_View_Erase();

	xrect_Set_Rect(&frame, 0, 0, TIE_FRONTEND_EDITION(320, 640), TIE_FRONTEND_EDITION(200, 480));
	blueprint_film =
		xfilm_Res_Callback_Film(blueprint_str[1], &frame, 0, 0, 0, blueprnt_film_Blueprint_Callback);
	xfilm_Set_Film_Def_Palette(blueprint_film, the_head->def_palette);

	/* World input (full screen) */
	xrect_Set_Rect(&frame, 0, 0, TIE_FRONTEND_EDITION(320, 640), TIE_FRONTEND_EDITION(200, 480));
	world_input = xinput_Alloc_Input(NULL, &frame, 0, 0);

	for (i = 0; i < 4; i++) {
		switch (i) {
			case 0:
				xrect_Set_Rect(&frame, TIE_FRONTEND_EDITION(62, 203), TIE_FRONTEND_EDITION(150, 350),
							   TIE_FRONTEND_EDITION(92, 255), TIE_FRONTEND_EDITION(164, 380));
				break;
			case 1:
				xrect_Set_Rect(&frame, TIE_FRONTEND_EDITION(62, 203), TIE_FRONTEND_EDITION(165, 385),
							   TIE_FRONTEND_EDITION(92, 255), TIE_FRONTEND_EDITION(178, 415));
				break;
			case 2:
				xrect_Set_Rect(&frame, TIE_FRONTEND_EDITION(263, 553), TIE_FRONTEND_EDITION(150, 350),
							   TIE_FRONTEND_EDITION(295, 605), TIE_FRONTEND_EDITION(164, 380));
				break;
			case 3:
				xrect_Set_Rect(&frame, TIE_FRONTEND_EDITION(263, 553), TIE_FRONTEND_EDITION(165, 385),
							   TIE_FRONTEND_EDITION(295, 605), TIE_FRONTEND_EDITION(178, 415));
				break;
		}
		button_input[i] = xinput_Alloc_Input(world_input, &frame, 0, 0);
		xinpattr_Set_Input_Update_Function(button_input[i], blueprnt_iupdate_Blueprint);
		xinpattr_Set_Input_User_Function(button_input[i], blueprnt_iuser_Blueprint);
		button_input[i]->id = i + 1;
	}

	/* Door input (left panel) */
	xrect_Set_Rect(&frame, 0, TIE_FRONTEND_EDITION(30, 73), TIE_FRONTEND_EDITION(80, 159),
				   TIE_FRONTEND_EDITION(116, 296));
	door_input = xinput_Alloc_Input(world_input, &frame, 0, 0);
	xinpattr_Set_Input_Update_Function(door_input, blueprnt_iupdate_Blueprint_Door);
	xinpattr_Set_Input_User_Function(door_input, blueprnt_iuser_Blueprint_Door);
	door_input->mouseUsage = 4;

	/* Ship name text actor */
	xrect_Set_Rect(&frame, TIE_FRONTEND_EDITION(98, 266), TIE_FRONTEND_EDITION(153, 355),
				   TIE_FRONTEND_EDITION(257, 539), TIE_FRONTEND_EDITION(161, 375));
	ship_name_actor = xactcust_Alloc_Custom_Actor(0, &frame, 0, 0, 0);
	xactor_Set_Actor_Draw_Function(ship_name_actor, blueprnt_draw_Blueprint_Text);
	ship_name_actor->id = 0;

	/* Component text actor ("Rotate Craft") */
	xrect_Set_Rect(&frame, TIE_FRONTEND_EDITION(98, 266), TIE_FRONTEND_EDITION(167, 392),
				   TIE_FRONTEND_EDITION(257, 539), TIE_FRONTEND_EDITION(175, 412));
	ship_comp_actor = xactcust_Alloc_Custom_Actor(0, &frame, 0, 0, 0);
	xactor_Set_Actor_Draw_Function(ship_comp_actor, blueprnt_draw_Blueprint_Text);
	ship_comp_actor->id = 1;

	/* Ship info overlay actor */
	xrect_Set_Rect(&frame, TIE_FRONTEND_EDITION(131, 222), TIE_FRONTEND_EDITION(30, 75),
				   TIE_FRONTEND_EDITION(278, 570), TIE_FRONTEND_EDITION(200, 310));
	ship_info_actor = xactcust_Alloc_Custom_Actor(0, &frame, 0, 0, 10);
	xactor_Set_Actor_User_Function(ship_info_actor, blueprnt_user_Blueprint_Info);
	xactor_Set_Actor_Draw_Function(ship_info_actor, blueprnt_draw_Blueprint_Info);

	shipext_Open_Blueprint_Ships();
	bpflight_Open_Flight_Engine(3);
	xview_Set_View_Update_Function(blueprnt_end_Blueprint_View);
#ifdef TIE_MODERN
	TieBlueprint_RunView(blueprint_file, TieProfile_UsesTie98Frontend());
	return 0;
#else
	shellext_Handle_TIE_View();
	xinpcall_Clear_Active_Input();
	xview_Clear_View_Update_Function();
	bpflight_Close_Flight_Engine();
	shipext_Close_Blueprint_Ships();
	xview_Enable_All_View_Erase();

	if (xcursor_Is_Cursor_Visible())
		xcursor_Hide_Cursor();

	xres_Close_Resource(blueprint_file);
	return xerror_Get_Landru_Exit();
#endif
}
