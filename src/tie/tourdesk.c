/*
 * TOURDESK.C — Tour of Duty battle selection screen.
 *
 * Film-driven desk with left/right doors, galaxy zoom animation,
 * battle text display, and 4 navigation widgets (main menu, join/
 * cutscene, next battle, previous battle). The galaxy display uses
 * a 5-phase zoom-in animation driven by tour_time.
 *
 * 15 functions. Recovered from the TIE95 and TIE98 executables.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tie/shade.h"
#include "tie/shellext.h"
#include "tie/shipext.h"
#include "tie/soundext.h"
#include "tie/textext.h"
#include "tie/tie.h"
#include "tie/tourdesk.h"
#include "tie_runtime/runtime/profile.h"
#include "tie_runtime/snapshot/snapshot.h"
#include "tie_runtime/snapshot/snapshot_internal.h"
#include <landru/task.h>

#include "landru/actcust.h"
#include "landru/actdelt.h"
#include "landru/actor.h"
#include "landru/canvas.h"
#include "landru/cursor.h"
#include "landru/dirty.h"
#include "landru/error.h"
#include "landru/film.h"
#include "landru/font.h"
#include "landru/fourcc.h"
#include "landru/inpattr.h"
#include "landru/input.h"
#include "landru/io.h"
#include "landru/paint.h"
#include "landru/rect.h"
#include "landru/res.h"
#include "landru/surface.h"
#include "landru/vesa.h"
#include "landru/view.h"
#include "landru/viewadd.h"

/* ---- Edition data ---- */

typedef struct TourDeskSpec {
	LandruSurfaceSet surface_set;
	int16_t width, height;
	int16_t mouse_x, mouse_y;
	const char* door_names[2];
	const char* button_names[2];
	int16_t battle_text_bounds[4];
	int16_t galaxy_bounds[4];
	int16_t input_bounds[4][4];
	int16_t battle_text_zplane;
	int16_t font_id;
	int16_t battle_text_line_height;
	int16_t galaxy_rect_scale;
	int16_t first_frame_background_zplane;
	int16_t button_click_state[2];
	int16_t button_hover_state[2];
	bool create_input_parent;
	bool dynamic_text_layout;
	bool clip_reveal_before_shade;
	bool refresh_background;
} TourDeskSpec;

/* DATA: TIE95 TOURDESK_TourDesk 0x73790; TIE98 0x490A20. */
static const TourDeskSpec tourdesk_specs[] = {
    {
        .surface_set = LANDRU_SURFACE_VGA,
        .width = 320, .height = 200,
        .mouse_x = 150, .mouse_y = 158,
        .door_names = {"lhdoor", "rhdoor"},
        .button_names = {"todbttn", NULL},
        .battle_text_bounds = {72, 7, 256, 33},
        .galaxy_bounds = {72, 45, 256, 120},
        .input_bounds = {
            {0, 92, 54, 162},
            {286, 64, 320, 120},
            {130, 132, 180, 164},
            {130, 164, 180, 196},
        },
        .battle_text_zplane = 50,
        .font_id = 0,
        .battle_text_line_height = 9,
        .galaxy_rect_scale = 1,
        .first_frame_background_zplane = -1,
        .button_click_state = {1, 1},
        .button_hover_state = {2, 0},
        .create_input_parent = true,
        .dynamic_text_layout = false,
        .clip_reveal_before_shade = false,
        .refresh_background = false,
    },
    {
        .surface_set = LANDRU_SURFACE_SVGA,
        .width = 640, .height = 480,
        .mouse_x = 320, .mouse_y = 415,
        .door_names = {"lhdor", "rhdor"},
        .button_names = {"upbutton", "dnbutton"},
        .battle_text_bounds = {169, 33, 512, 90},
        .galaxy_bounds = {166, 116, 512, 291},
        .input_bounds = {
            {0, 229, 137, 390},
            {552, 147, 639, 302},
            {306, 401, 338, 434},
            {307, 436, 338, 466},
        },
        .battle_text_zplane = 30,
        .font_id = 2,
        .battle_text_line_height = 0,
        .galaxy_rect_scale = 2,
        .first_frame_background_zplane = 40,
        .button_click_state = {1, 0},
        .button_hover_state = {0, 1},
        .create_input_parent = false,
        .dynamic_text_layout = true,
        .clip_reveal_before_shade = true,
        .refresh_background = true,
    },
};

static const TourDeskSpec* active_spec;

/* ---- Static globals ---- */

// GLOBAL: TIE 0xF6070
static Actor* galaxy_art_actor[20]; /* cached per-battle galaxy art */
static Actor* door[2];              /* left/right door actors */
static Input* parent;
static Actor* battle_text_actor; /* "Battle N" title text */
// GLOBAL: TIE 0xF60C0
static int32_t tour_time; /* animation frame counter */
// GLOBAL: TIE 0xF5968
static Actor* title_actor;
static Actor* button_actor[2]; /* next/previous battle buttons */
static Actor* galaxy_actor;    /* galaxy display custom actor */
static Actor* tourdesk_actor;  /* desk background delta */
static Film* tourdesk_film;
// GLOBAL: TIE 0xF60E0
static int32_t cur_tour_battle; /* battle at entry (for detecting changes) */

/* ---- Forward declarations ---- */

static void end_View(int32_t frame_num);
static int16_t iupdate_TourDesk(Input* input, Rect* bounds, Rect* clip, int16_t key, uint8_t left,
								uint8_t right, int16_t mouse_x, int16_t mouse_y);
static void iuser_TourDesk(Input* input, int32_t time);
static void user_Title(Actor* actor, int32_t time);
static int draw_Title(Actor* actor, Rect* bounds, Rect* clip, int16_t xoff, int16_t yoff, int16_t refresh);
static void user_Door(Actor* actor, int32_t time);
static void user_Battle(Actor* actor, int32_t time);
static int draw_Battle_Text(Actor* actor, Rect* r, Rect* clip_r, int16_t x, int16_t y, int16_t refresh);
static void Draw_Battle_One(Rect* galaxy_rect, int16_t tour_time);
static void Draw_Battle_Two(Rect* galaxy_rect, int16_t tour_time, Rect* clip_r);
static void Draw_Battle_Three(Rect* galaxy_rect, Rect* view_r, int16_t tour_time);
static void Draw_Battle_Four(Rect* galaxy_rect, Rect* view_r, Rect* clip_r, int16_t tour_time);
static int Draw_Battle_Five(Rect* r, Rect* clip_r, int16_t time);
static int draw_Battle(Actor* actor, Rect* r, Rect* clip_r, int16_t x, int16_t y, int16_t refresh);

/* ================================================================
 * View update callback
 * ================================================================ */

// FUNCTION: TIE95 0x73AF4; TIE98 0x490E40
static void end_View(int32_t frame_num) {
	if (frame_num)
		return;
	if (active_spec->first_frame_background_zplane >= 0)
		lactor_Set_Actor_ZPlane(tourdesk_actor, active_spec->first_frame_background_zplane);
	if (!lcursor_Is_Cursor_Visible())
		lcursor_Show_Cursor();
}

/* ================================================================
 * XINPUT callbacks
 * ================================================================ */

// FUNCTION: TIE95 0x73B18; TIE98 0x490E70
static int16_t iupdate_TourDesk(Input* input, Rect* bounds, Rect* clip, int16_t key, uint8_t left,
								uint8_t right, int16_t mouse_x, int16_t mouse_y) {
	(void)bounds;
	(void)clip;
	(void)mouse_x;
	(void)mouse_y;
	if (key)
		return 0;

	/* Open door for ids 0,1 */
	if (input->id < 2)
		door[input->id]->var1 = 1;

	/* Show title label */
	title_actor->var1 = 1;
	title_actor->var2 = input->id;

	/* Check for click */
	if (left != 3 && right != 3) {
		/* Button hover state for next/prev arrows */
		if (left || right) {
			if (input->id == 2 || input->id == 3) {
				int16_t button_index = input->id - 2;
				lactor_Set_Actor_State(button_actor[button_index],
									   active_spec->button_hover_state[button_index], 0);
			}
		}
		return 1;
	}

	switch (input->id) {
		case 0: /* Main Menu */
			input->var2 = SCENE_MAIN_MENU;
			input->var1 = 1;
			break;
		case 1: /* Join/Cutscene */
			input->var1 = 1;
			input->var2 = shipext_Set_Tourdesk_Cutscene();
			break;
		case 2: /* Next battle */
			lactor_Set_Actor_State(button_actor[0], active_spec->button_click_state[0], 0);
			soundext_Play_SFX(sfxButton, 80);
			shipext_Next_Battle();
			tour_time = 0;
			if (!galaxy_art_actor[pilot_record.cur_battle])
				galaxy_art_actor[pilot_record.cur_battle] = shipext_Get_Battle_Galaxy_Image();
			break;
		case 3: /* Previous battle */
			lactor_Set_Actor_State(button_actor[1], active_spec->button_click_state[1], 0);
			soundext_Play_SFX(sfxButton, 80);
			shipext_Last_Battle();
			tour_time = 0;
			if (!galaxy_art_actor[pilot_record.cur_battle])
				galaxy_art_actor[pilot_record.cur_battle] = shipext_Get_Battle_Galaxy_Image();
			break;
		default:
			break;
	}
	return 1;
}

// FUNCTION: TIE95 0x73CB8; TIE98 0x491010
static void iuser_TourDesk(Input* input, int32_t time) {
	(void)time;
	if (!input->var1)
		return;

	if (input->var2 == SCENE_BRIEF) {
		shipext_Set_Tour_Battle();
		if (pilot_record.cur_battle != (uint8_t)cur_tour_battle)
			input->var2 = SCENE_TOUR_CUTSCENE;
	}
	lerror_Set_Landru_Exit(input->var2);
}

/* ================================================================
 * Actor callbacks
 * ================================================================ */

// FUNCTION: TIE95 0x73D04; TIE98 0x491060
static void user_Title(Actor* actor, int32_t time) {
	(void)time;
	if (actor->var1 == 1) {
		if (!lactor_Is_Actor_Visible(actor))
			lactor_Show_Actor(actor);
		actor->var1 = 0;
	} else {
		if (lactor_Is_Actor_Visible(actor))
			lactor_Hide_Actor(actor);
	}
}

// FUNCTION: TIE95 0x73D58; TIE98 0x4910B0
static int draw_Title(Actor* actor, Rect* bounds, Rect* clip, int16_t xoff, int16_t yoff, int16_t refresh) {
	if (!refresh)
		return 0;

	lactdelt_Draw_Delta_Actor(actor, bounds, clip, xoff, yoff, refresh);

	int16_t offx, offy;
	lactor_Get_Actor_Offset(actor, &offx, &offy);
	Rect r;
	lrect_Set_Rect(&r, offx, offy, actor->w + offx, actor->h + offy);

	char label[32];
	TIEText text_id;
	switch (actor->var2) {
		case 0:
			text_id = txtTourMainMenu;
			break;
		case 1:
			text_id =
				(pilot_record.battle_status[pilot_record.cur_battle] == 3) ? txtTourCutscene : txtTourJoin;
			break;
		case 2:
			text_id = txtTourNext;
			break;
		case 3:
			text_id = txtTourPrev;
			break;
		default:
			label[0] = 0;
			goto draw_text;
	}
	strcpy(label, textext_Get_Text(text_id));

draw_text:
	lrect_Offset_Rect(&r, 1, 1);
	lfont_Print_Centered_Text(label, &r, 16, active_spec->font_id);
	lrect_Offset_Rect(&r, -1, -1);
	lfont_Print_Centered_Text(label, &r, 15, active_spec->font_id);
	return 1;
}

// FUNCTION: TIE95 0x73E6C; TIE98 0x491200
static void user_Door(Actor* actor, int32_t time) {
	(void)time;
	if (actor->var1) {
		if (!actor->state)
			soundext_Play_SFX(sfxAirLock, 90);
		if (actor->state < actor->arraySize - 1)
			lactor_Set_Actor_State(actor, actor->state + 1, 0);
		actor->var1 = 0;
	} else {
		if (actor->state > 0) {
			lactor_Set_Actor_State(actor, actor->state - 1, 0);
			if (!actor->state)
				soundext_Play_SFX(sfxLargeDoorShut, 90);
		}
	}
}

// FUNCTION: TIE95 0x73EEC; TIE98 0x491280
static void user_Battle(Actor* actor, int32_t time) {
	(void)actor;
	if (!time) {
		tour_time = 0;
		shade_Build_Shaded_Palette();
	} else {
		tour_time++;
	}
}

/* ================================================================
 * Battle text draw
 * ================================================================ */

// FUNCTION: TIE95 0x73F0C; TIE98 0x4912B0
static int draw_Battle_Text(Actor* actor, Rect* r, Rect* clip_r, int16_t x, int16_t y, int16_t refresh) {
	(void)actor;
	(void)clip_r;
	(void)x;
	(void)y;
	if (!refresh)
		return 0;

	lpaint_Paint_Clipped_Rect(r, 0);
	Rect dst;
	lrect_Copy_Rect(&dst, r);
	int16_t line_height = active_spec->battle_text_line_height;
	if (!line_height)
		line_height = (int16_t)lfont_Get_FontID_Height(active_spec->font_id);
	dst.bottom = dst.top + line_height;

	for (int16_t i = 0; i < 3; i++) {
		char buf[64];
		shipext_Get_Battle_Title(buf, i);
		int16_t color = i ? 2 : 15;
		lfont_Print_Centered_Text(buf, &dst, color, active_spec->font_id);
		lrect_Offset_Rect(&dst, 0, line_height);
	}

	ldirty_Dirty_Rect(r);
	return 1;
}

/* ================================================================
 * Galaxy zoom animation phases (Draw_Battle helpers)
 * ================================================================ */

/* Phase 0-7: zoom from center to galaxy rect */
// FUNCTION: TIE95 0x73FA0; TIE98 0x491360
static void Draw_Battle_One(Rect* galaxy_rect, int16_t time) {
	if (time >= 8)
		return;

	Rect dst, ra;
	lrect_Copy_Rect(&dst, galaxy_rect);
	lrect_Copy_Rect(&ra, galaxy_rect);

	/* Start from center point */
	dst.left += (dst.right - dst.left) >> 1;
	dst.top += (dst.bottom - dst.top) >> 1;
	dst.right -= (dst.right - dst.left) >> 1;
	dst.bottom -= (dst.bottom - dst.top) >> 1;

	/* Interpolate toward full rect */
	dst.left += (time * (ra.left - dst.left)) >> 3;
	dst.top += (time * (ra.top - dst.top)) >> 3;
	dst.right += (time * (ra.right - dst.right)) >> 3;
	dst.bottom += (time * (ra.bottom - dst.bottom)) >> 3;

	if (!lrect_Empty_Rect(&dst))
		shade_Draw_Talk_Shade_Rect(&dst);

	lrect_Inset_Rect(&ra, -64, 0);
	if (active_spec->dynamic_text_layout) {
		int16_t font_height = (int16_t)lfont_Get_FontID_Height(active_spec->font_id);
		ra.top = ra.bottom + (font_height >> 1);
		ra.bottom = ra.top + (int16_t)lfont_Get_FontID_Height(active_spec->font_id);
	} else {
		ra.top = ra.bottom + 2;
		ra.bottom += 10;
	}
	lrect_Offset_Rect(&ra, -4, 0);

	lfont_Enable_FontID_Shadow(active_spec->font_id);
	char name[64];
	shipext_Get_Battle_Galaxy_Name(name);
	lfont_Print_Centered_Text(name, &ra, 2 * time + 16, active_spec->font_id);
	lfont_Disable_FontID_Shadow(active_spec->font_id);
}

/* Phase 8-23: hold at galaxy rect */
// FUNCTION: TIE95 0x74118; TIE98 0x491500
static void Draw_Battle_Two(Rect* galaxy_rect, int16_t time, Rect* clip_r) {
	(void)clip_r;
	if (time < 8 || time >= 24)
		return;

	Rect dst;
	lrect_Copy_Rect(&dst, galaxy_rect);
	shade_Draw_Talk_Shade_Rect(&dst);

	Rect ra;
	lrect_Copy_Rect(&ra, &dst);
	lrect_Inset_Rect(&ra, -64, 0);
	if (active_spec->dynamic_text_layout) {
		int16_t font_height = (int16_t)lfont_Get_FontID_Height(active_spec->font_id);
		ra.top = ra.bottom + (font_height >> 1);
		ra.bottom = ra.top + (int16_t)lfont_Get_FontID_Height(active_spec->font_id);
	} else {
		ra.top = ra.bottom + 2;
		ra.bottom += 10;
	}
	lrect_Offset_Rect(&ra, -4, 0);

	lfont_Enable_FontID_Shadow(active_spec->font_id);
	char name[64];
	shipext_Get_Battle_Galaxy_Name(name);
	lfont_Print_Centered_Text(name, &ra, 31, active_spec->font_id);
	lfont_Disable_FontID_Shadow(active_spec->font_id);
}

/* Phase 24-31: zoom from galaxy rect to actor bounds */
// FUNCTION: TIE95 0x741BC; TIE98 0x4915E0
static void Draw_Battle_Three(Rect* galaxy_rect, Rect* view_r, int16_t time) {
	if (time < 24 || time >= 32)
		return;

	Actor* art = galaxy_art_actor[pilot_record.cur_battle];
	Rect dst, art_bounds;
	lrect_Copy_Rect(&dst, galaxy_rect);
	lactor_Get_Actor_Bounds(art, &art_bounds);
	lrect_Offset_Rect(&art_bounds, view_r->left, view_r->top);

	int16_t t = time - 24;
	dst.left += (t * (art_bounds.left - dst.left)) >> 3;
	dst.top += (t * (art_bounds.top - dst.top)) >> 3;
	dst.right += (t * (art_bounds.right - dst.right)) >> 3;
	dst.bottom += (t * (art_bounds.bottom - dst.bottom)) >> 3;

	if (!lrect_Empty_Rect(&dst))
		shade_Draw_Talk_Shade_Rect(&dst);
}

/* Phase 32-39: reveal actor with vertical wipe */
// FUNCTION: TIE95 0x742A0; TIE98 0x4916F0
static void Draw_Battle_Four(Rect* galaxy_rect, Rect* view_r, Rect* clip_r, int16_t time) {
	(void)galaxy_rect;
	if (time < 32 || time >= 40)
		return;

	Actor* art = galaxy_art_actor[pilot_record.cur_battle];
	Rect bounds;
	lactor_Get_Actor_Bounds(art, &bounds);
	lrect_Offset_Rect(&bounds, view_r->left, view_r->top);
	if (active_spec->clip_reveal_before_shade)
		lrect_Clip_Rect(&bounds, clip_r);

	int16_t t = time - 32;
	if (!lrect_Empty_Rect(&bounds))
		shade_Draw_Talk_Shade_Rect(&bounds);

	bounds.bottom = ((t * (bounds.bottom - bounds.top)) >> 3) + bounds.top;
	if (lrect_Clip_Rect(&bounds, clip_r)) {
		lcanvas_Set_Drawing_Canvas_Clip(&bounds);
		lactdelt_Draw_Delta_Actor(art, view_r, &bounds, view_r->left, view_r->top, 1);
	}
}

/* Phase 40+: final state — full art + battle info text */
// FUNCTION: TIE95 0x7434C; TIE98 0x4917E0
static int Draw_Battle_Five(Rect* r, Rect* clip_r, int16_t time) {
	if (time < 40)
		return 1;

	int16_t fade = time - 40;
	Actor* art = galaxy_art_actor[pilot_record.cur_battle];

	lactdelt_Draw_Delta_Actor(art, r, clip_r, r->left, r->top, 1);

	Rect art_bounds;
	lactor_Get_Actor_Bounds(art, &art_bounds);
	lrect_Offset_Rect(&art_bounds, r->left, r->top);

	lfont_Enable_FontID_Shadow(active_spec->font_id);

	/* Galaxy name with fade-in */
	char name[64];
	shipext_Get_Battle_Galaxy_Name(name);
	int16_t name_color = (fade >= 8) ? 31 : 2 * fade + 16;
	lfont_Print_Centered_Text(name, &art_bounds, name_color, active_spec->font_id);

	/* "Battle N" text */
	Rect dst;
	lrect_Copy_Rect(&dst, r);
	dst.left = art_bounds.right;
	if (active_spec->dynamic_text_layout) {
		int16_t font_height = (int16_t)lfont_Get_FontID_Height(active_spec->font_id);
		dst.top = art_bounds.top + 3 * font_height;
		dst.bottom = dst.top + (int16_t)lfont_Get_FontID_Height(active_spec->font_id);
	} else {
		dst.top = art_bounds.top + 28;
		dst.bottom = art_bounds.top + 38;
	}

	char battle_str[16], buf[64];
	textext_Copy_Text(battle_str, txtCompInfoBattle);
	snprintf(buf, sizeof(buf), battle_str, pilot_record.cur_battle + 1);

	if (pilot_record.battle_status[pilot_record.cur_battle] == 3)
		lrect_Offset_Rect(&dst, 0, 4);

	int16_t battle_color = (fade >= 8) ? 31 : 2 * fade + 16;
	lfont_Print_Centered_Text(buf, &dst, battle_color, active_spec->font_id);

	/* "Mission N" text (only if battle not complete) */
	int16_t line_height =
		active_spec->dynamic_text_layout ? (int16_t)lfont_Get_FontID_Height(active_spec->font_id) : 10;
	lrect_Offset_Rect(&dst, 0, line_height);
	if (pilot_record.battle_status[pilot_record.cur_battle] != 3) {
		char mission_label[16];
		strcpy(mission_label, textext_Get_Text(txtCombatMission));
		snprintf(buf, sizeof(buf), "%s %d", mission_label,
				 pilot_record.battle_cursor[pilot_record.cur_battle] + 1);
		int16_t mission_color = (fade >= 8) ? 31 : 2 * fade + 16;
		lfont_Print_Centered_Text(buf, &dst, mission_color, active_spec->font_id);
	}

	lfont_Disable_FontID_Shadow(active_spec->font_id);
	return 1;
}

/* ================================================================
 * Galaxy zoom composite draw
 * ================================================================ */

// FUNCTION: TIE95 0x74534; TIE98 0x491A30
static int draw_Battle(Actor* actor, Rect* r, Rect* clip_r, int16_t x, int16_t y, int16_t refresh) {
	(void)actor;
	(void)x;
	(void)y;
	if (!refresh)
		return 0;

	Rect galaxy_rect;
	shipext_Get_Battle_Galaxy_Rect(&galaxy_rect);
	galaxy_rect.top *= active_spec->galaxy_rect_scale;
	galaxy_rect.left *= active_spec->galaxy_rect_scale;
	galaxy_rect.bottom *= active_spec->galaxy_rect_scale;
	galaxy_rect.right *= active_spec->galaxy_rect_scale;
	lrect_Offset_Rect(&galaxy_rect, r->left, r->top);

	if (!galaxy_art_actor[pilot_record.cur_battle]) {
		if (active_spec->refresh_background) {
			lactor_Refresh_Actor(tourdesk_actor);
			lactor_Dirty_Actor(tourdesk_actor);
		}
		ldirty_Dirty_Rect(r);
		return 1;
	}

	int16_t t = (int16_t)tour_time;

	Draw_Battle_One(&galaxy_rect, t);
	Draw_Battle_Two(&galaxy_rect, t, clip_r);
	Draw_Battle_Three(&galaxy_rect, r, t);
	Draw_Battle_Four(&galaxy_rect, r, clip_r, t);
	Draw_Battle_Five(r, clip_r, t);

	if (active_spec->refresh_background) {
		lactor_Refresh_Actor(tourdesk_actor);
		lactor_Dirty_Actor(tourdesk_actor);
	}
	ldirty_Dirty_Rect(r);
	return 1;
}

/* ================================================================
 * Entry point
 * ================================================================ */

typedef enum {
	TOURDESK_PHASE_BEGIN = 0,
	TOURDESK_PHASE_CLEANUP = 1,
} TourDeskPhase;

typedef struct TourDeskTask {
	SceneHeadStruct* scene_head;
	TourDeskPhase phase;
	const TourDeskSpec* spec;
} TourDeskTask;

/* PORT: asynchronous adaptation of TIE95 TOURDESK_TourDesk (0x73790)
 * and TIE98 TOURDESK_TourDesk (0x490A20). */
static LandruTaskStepResult tourdesk_task_step(void* self) {
	TourDeskTask* t = (TourDeskTask*)self;

	if (t->phase == TOURDESK_PHASE_BEGIN) {
		Rect frame;
		const int16_t* bounds;

		active_spec = t->spec;
		if (active_spec->surface_set == LANDRU_SURFACE_SVGA) {
			(void)lsurface_Select_Surface_Set(active_spec->surface_set);
			lview_Init_View(lview_Get_Current_View());
		}
		lio_Set_Mouse_Position(active_spec->mouse_x, active_spec->mouse_y);
		cur_tour_battle = pilot_record.cur_battle;

		/* Load resources */
		ResFile* res_file = shellext_Open_Empire_Resource("tourdesk.lfd");
		lrect_Set_Rect(&frame, 0, 0, active_spec->width, active_spec->height);

		tourdesk_film = lfilm_Res_Film("tourdesk", &frame, 0, 0, 0);
		lfilm_Set_Film_Def_Palette(tourdesk_film, t->scene_head->def_palette);

		/* Tag the snapshot with (lfd, film) so the cutscene compositor
		 * resolves the TOURDESK/tourdesk remaster bundle for this
		 * screen. INCREMENTAL redraw model is correct (default): the
		 * desk background and door anims are persistent, only the
		 * galaxy zoom + battle text refresh per frame. Auto-cleared at
		 * the next scene transition by shell_run_scene_dispatch. */
		TieSnapshotBuilder_SetActiveFilm("TOURDESK", "tourdesk");

		/* Find actors */
		tourdesk_actor = lactor_Find_Actor(FOURCC_DELT, "toddesk");
		lactor_Non_Refreshable_Actor(tourdesk_actor);

		door[0] = lactor_Find_Actor(FOURCC_ANIM, active_spec->door_names[0]);
		door[1] = lactor_Find_Actor(FOURCC_ANIM, active_spec->door_names[1]);
		for (int16_t i = 0; i < 2; i++) {
			lactor_Set_Actor_User_Function(door[i], (lactorCallback)user_Door);
			door[i]->id = i;
		}

		button_actor[0] = lactor_Find_Actor(FOURCC_ANIM, active_spec->button_names[0]);
		button_actor[1] = active_spec->button_names[1]
							  ? lactor_Find_Actor(FOURCC_ANIM, active_spec->button_names[1])
							  : button_actor[0];

		/* Title label */
		title_actor = lactdelt_Res_Delta_Actor("title", &frame, 0, 0, 0);
		lactor_Set_Actor_User_Function(title_actor, (lactorCallback)user_Title);
		lactor_Set_Actor_Draw_Function(title_actor, (lactorDrawFunc)draw_Title);

		/* Battle text custom actor */
		bounds = active_spec->battle_text_bounds;
		lrect_Set_Rect(&frame, bounds[0], bounds[1], bounds[2], bounds[3]);
		battle_text_actor = lactcust_Alloc_Custom_Actor(NULL, &frame, 0, 0, active_spec->battle_text_zplane);
		lactor_Set_Actor_Draw_Function(battle_text_actor, (lactorDrawFunc)draw_Battle_Text);
		battle_text_actor->id = 0;

		/* Galaxy display custom actor */
		bounds = active_spec->galaxy_bounds;
		lrect_Set_Rect(&frame, bounds[0], bounds[1], bounds[2], bounds[3]);
		galaxy_actor = lactcust_Alloc_Custom_Actor(NULL, &frame, 0, 0, 50);
		lactor_Set_Actor_User_Function(galaxy_actor, (lactorCallback)user_Battle);
		lactor_Set_Actor_Draw_Function(galaxy_actor, (lactorDrawFunc)draw_Battle);
		galaxy_actor->id = 1;

		/* Initialize galaxy art cache */
		for (int16_t i = 0; i < 20; i++)
			galaxy_art_actor[i] = NULL;
		galaxy_art_actor[pilot_record.cur_battle] = shipext_Get_Battle_Galaxy_Image();

		/* Create XINPUT widgets */
		parent = NULL;
		if (active_spec->create_input_parent) {
			lrect_Set_Rect(&frame, 0, 0, active_spec->width, active_spec->height);
			parent = linput_Alloc_Input(NULL, &frame, 0, 0);
		}

		/* Main Menu (id=0) */
		bounds = active_spec->input_bounds[0];
		lrect_Set_Rect(&frame, bounds[0], bounds[1], bounds[2], bounds[3]);
		Input* inp = linput_Alloc_Input(parent, &frame, 0, 0);
		linpattr_Set_Input_Update_Function(inp, iupdate_TourDesk);
		linpattr_Set_Input_User_Function(inp, iuser_TourDesk);
		inp->mouseUsage = allInput;
		inp->id = 0;

		/* Join/Cutscene (id=1) */
		bounds = active_spec->input_bounds[1];
		lrect_Set_Rect(&frame, bounds[0], bounds[1], bounds[2], bounds[3]);
		inp = linput_Alloc_Input(parent, &frame, 0, 0);
		linpattr_Set_Input_Update_Function(inp, iupdate_TourDesk);
		linpattr_Set_Input_User_Function(inp, iuser_TourDesk);
		inp->mouseUsage = allInput;
		inp->id = 1;

		/* Next battle (id=2) */
		bounds = active_spec->input_bounds[2];
		lrect_Set_Rect(&frame, bounds[0], bounds[1], bounds[2], bounds[3]);
		inp = linput_Alloc_Input(parent, &frame, 0, 0);
		linpattr_Set_Input_Update_Function(inp, iupdate_TourDesk);
		linpattr_Set_Input_User_Function(inp, iuser_TourDesk);
		inp->mouseUsage = allInput;
		inp->id = 2;

		/* Previous battle (id=3) */
		bounds = active_spec->input_bounds[3];
		lrect_Set_Rect(&frame, bounds[0], bounds[1], bounds[2], bounds[3]);
		inp = linput_Alloc_Input(parent, &frame, 0, 0);
		linpattr_Set_Input_Update_Function(inp, iupdate_TourDesk);
		linpattr_Set_Input_User_Function(inp, iuser_TourDesk);
		inp->mouseUsage = allInput;
		inp->id = 3;

		lres_Close_Resource(res_file);

		/* Push the modal view task */
		lview_Set_View_Update_Function(end_View);
		lviewadd_Clear_View();
		lview_Disable_All_View_Erase();

		lviewadd_Push_Handle_View_Task();

		t->phase = TOURDESK_PHASE_CLEANUP;
		return LANDRU_TASK_STEP_CONTINUE;
	}

	/* CLEANUP */
	lview_Enable_All_View_Erase();
	lview_Clear_View_Update_Function();

	if (lcursor_Is_Cursor_Visible())
		lcursor_Hide_Cursor();

	if (t->spec->surface_set == LANDRU_SURFACE_SVGA) {
		lvesa_Erase_Video(16);
		(void)lsurface_Select_Surface_Set(LANDRU_SURFACE_VGA);
	}

	return LANDRU_TASK_STEP_DONE;
}

static const LandruTaskVtable tourdesk_task_vt = {
	.step = tourdesk_task_step,
};

void tourdesk_Push_TourDesk_Task(SceneHeadStruct* scene_head) {
	TourDeskTask* t = (TourDeskTask*)landru_task_push(&tourdesk_task_vt);
	if (!t)
		return;
	t->scene_head = scene_head;
	t->phase = TOURDESK_PHASE_BEGIN;
	t->spec = &tourdesk_specs[TieProfile_FrontendId() == TIE_FRONTEND_PROFILE_TIE98 ? 1 : 0];
}
