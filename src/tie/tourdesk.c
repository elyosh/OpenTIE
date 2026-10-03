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

#include "tie/tourdesk.h"
#ifdef TIE_MODERN
#include "tie_runtime/runtime/tourdesk_task.h"
#endif
#include "tie/edition.h"
#include "tie/shade.h"
#include "tie/shellext.h"
#include "tie/shipext.h"
#include "tie/soundext.h"
#include "tie/textext.h"
#include "tie/tie.h"
#include "tie_runtime/runtime/profile.h"
#ifdef TIE_MODERN
#include "tie_runtime/snapshot/snapshot_internal.h"
#endif

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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The original TU calls the library strcpy() rather than the inline form. */
#ifdef __WATCOMC__
#pragma function(strcpy)
#endif

/* ---- Static globals ---- */

// GLOBAL: TIE95 0xF6070
// GLOBAL: TIE98 0x58AA80
static Actor* galaxy_art_actor[20]; /* cached per-battle galaxy art */
// GLOBAL: TIE95 0xF6068
// GLOBAL: TIE98 0x58AAD8
static Actor* door[2]; /* left/right door actors */
// GLOBAL: TIE95 0xF60D4
// GLOBAL: TIE98 0x58AAD0
static Input* parent;
// GLOBAL: TIE95 0xF60DC
// GLOBAL: TIE98 0x58AAE0
static Actor* battle_text_actor; /* "Battle N" title text */
// GLOBAL: TIE95 0xF60C0
// GLOBAL: TIE98 0x58AAD4
static int32_t tour_time; /* animation frame counter */
// GLOBAL: TIE95 0xF60C4
// GLOBAL: TIE98 0x58AAE4
static Actor* title_actor;
/* TIE95: one arrow actor for next/previous; TIE98: the up (next) arrow. */
// GLOBAL: TIE95 0xF60C8
// GLOBAL: TIE98 0x58AA70
static Actor* buttons;
// GLOBAL: TIE98 0x58AA74
static Actor* down_button; /* previous-battle arrow */
// GLOBAL: TIE95 0xF60CC
// GLOBAL: TIE98 0x58AA78
static Actor* galaxy_actor; /* galaxy display custom actor */
// GLOBAL: TIE95 0xF60D0
// GLOBAL: TIE98 0x58AA60
static Actor* tourdesk_actor; /* desk background delta */
// GLOBAL: TIE95 0xF60D8
// GLOBAL: TIE98 0x58AA64
static Film* tourdesk_film;
// GLOBAL: TIE95 0xF60E0
// GLOBAL: TIE98 0x58AA68
static int16_t cur_tour_battle; /* battle at entry (for detecting changes) */

/* ---- Forward declarations ---- */

static void tourdesk_end_View(int32_t frame_num);
static int16_t tourdesk_iupdate_TourDesk(Input* input, Rect* bounds, Rect* clip, int16_t key, uint8_t left,
										 uint8_t right, int16_t mouse_x, int16_t mouse_y);
static int16_t tourdesk_iuser_TourDesk(Input* input, int32_t time);
static int tourdesk_user_Title(Actor* actor, int32_t time);
static int16_t tourdesk_draw_Title(Actor* actor, Rect* bounds, Rect* clip, int16_t xoff, int16_t yoff,
								   int16_t refresh);
static void tourdesk_user_Door(Actor* actor, int32_t time);
static int16_t tourdesk_user_Battle(Actor* actor, int32_t time);
static int16_t tourdesk_draw_Battle_Text(Actor* actor, Rect* r, Rect* clip_r, int16_t x, int16_t y,
										 int16_t refresh);
static int16_t tourdesk_Draw_Battle_One(Rect* galaxy_rect, int32_t tour_time);
static void tourdesk_Draw_Battle_Two(Rect* galaxy_rect, int16_t tour_time, Rect* clip_r);
static void tourdesk_Draw_Battle_Three(Rect* galaxy_rect, Rect* view_r, int16_t tour_time);
static int16_t tourdesk_Draw_Battle_Four(Rect* galaxy_rect, Rect* view_r, Rect* clip_r, int tour_time);
static int16_t tourdesk_Draw_Battle_Five(Rect* r, Rect* clip_r, int time);
static int16_t tourdesk_draw_Battle(Actor* actor, Rect* r, Rect* clip_r, int16_t x, int16_t y,
									int16_t refresh);

/* ================================================================
 * Entry point
 * ================================================================ */

// FUNCTION: TIE95 0x73790
// FUNCTION: TIE98 0x490A20
int16_t tourdesk_TourDesk(SceneHeadStruct* scene_head) {
	Rect frame;
	ResFile* res_file;
	Input* inp;
	int16_t i;

	xio_Set_Mouse_Position(TIE_FRONTEND_EDITION(150, 320), TIE_FRONTEND_EDITION(158, 415));
	cur_tour_battle = pilot_record.cur_battle;

	/* Load resources */
	res_file = shellext_Open_Empire_Resource("tourdesk.lfd");
	xrect_Set_Rect(&frame, 0, 0, TIE_FRONTEND_EDITION(320, 640), TIE_FRONTEND_EDITION(200, 480));

	tourdesk_film = xfilm_Res_Film("tourdesk", &frame, 0, 0, 0);
	xfilm_Set_Film_Def_Palette(tourdesk_film, scene_head->def_palette);

	/* Tag the snapshot with (lfd, film) so the cutscene compositor
	 * resolves the TOURDESK/tourdesk remaster bundle for this
	 * screen. INCREMENTAL redraw model is correct (default): the
	 * desk background and door anims are persistent, only the
	 * galaxy zoom + battle text refresh per frame. Auto-cleared at
	 * the next scene transition by shell_run_scene_dispatch. */
#ifdef TIE_MODERN
	TieSnapshotBuilder_SetActiveFilm("TOURDESK", "tourdesk");
#endif

	/* Find actors */
	tourdesk_actor = xactor_Find_Actor(FOURCC_DELT, "toddesk");
	xactor_Non_Refreshable_Actor(tourdesk_actor);

	door[0] = xactor_Find_Actor(FOURCC_ANIM, TIE_FRONTEND_EDITION("lhdoor", "lhdor"));
	door[1] = xactor_Find_Actor(FOURCC_ANIM, TIE_FRONTEND_EDITION("rhdoor", "rhdor"));
	for (i = 0; i < 2; i++) {
		xactor_Set_Actor_User_Function(door[i], (xactorCallback)tourdesk_user_Door);
		door[i]->id = i;
	}

	if (TIE_FRONTEND_TIE98) {
		buttons = xactor_Find_Actor(FOURCC_ANIM, "upbutton");
		down_button = xactor_Find_Actor(FOURCC_ANIM, "dnbutton");
	} else {
		buttons = xactor_Find_Actor(FOURCC_ANIM, "todbttn");
	}

	/* Title label */
	title_actor = xactdelt_Res_Delta_Actor("title", &frame, 0, 0, 0);
	xactor_Set_Actor_User_Function(title_actor, (xactorCallback)tourdesk_user_Title);
	xactor_Set_Actor_Draw_Function(title_actor, tourdesk_draw_Title);

	/* Battle text custom actor */
	xrect_Set_Rect(&frame, TIE_FRONTEND_EDITION(72, 169), TIE_FRONTEND_EDITION(7, 33),
				   TIE_FRONTEND_EDITION(256, 512), TIE_FRONTEND_EDITION(33, 90));
	battle_text_actor =
		xactcust_Alloc_Custom_Actor(LANDRU_NULL_HANDLE, &frame, 0, 0, TIE_FRONTEND_EDITION(50, 30));
	xactor_Set_Actor_Draw_Function(battle_text_actor, tourdesk_draw_Battle_Text);
	battle_text_actor->id = 0;

	/* Galaxy display custom actor */
	xrect_Set_Rect(&frame, TIE_FRONTEND_EDITION(72, 166), TIE_FRONTEND_EDITION(45, 116),
				   TIE_FRONTEND_EDITION(256, 512), TIE_FRONTEND_EDITION(120, 291));
	galaxy_actor = xactcust_Alloc_Custom_Actor(LANDRU_NULL_HANDLE, &frame, 0, 0, 50);
	xactor_Set_Actor_User_Function(galaxy_actor, (xactorCallback)tourdesk_user_Battle);
	xactor_Set_Actor_Draw_Function(galaxy_actor, tourdesk_draw_Battle);
	galaxy_actor->id = 1;

	/* Initialize galaxy art cache */
	for (i = 0; i < 20; i++)
		galaxy_art_actor[i] = NULL;
	galaxy_art_actor[pilot_record.cur_battle] = shipext_Get_Battle_Galaxy_Image();

	/* Create XINPUT widgets */
#ifdef TIE_MODERN
	parent = NULL;
	if (!TieProfile_UsesTie98Frontend()) {
		xrect_Set_Rect(&frame, 0, 0, 320, 200);
		parent = xinput_Alloc_Input(NULL, &frame, 0, 0);
	}
#endif

	/* Main Menu (id=0) */
	xrect_Set_Rect(&frame, 0, TIE_FRONTEND_EDITION(92, 229), TIE_FRONTEND_EDITION(54, 137),
				   TIE_FRONTEND_EDITION(162, 390));
	inp = xinput_Alloc_Input(parent, &frame, 0, 0);
	xinpattr_Set_Input_Update_Function(inp, tourdesk_iupdate_TourDesk);
	xinpattr_Set_Input_User_Function(inp, (InputUserFunc)tourdesk_iuser_TourDesk);
	inp->mouseUsage = allInput;
	inp->id = 0;

	/* Join/Cutscene (id=1) */
	xrect_Set_Rect(&frame, TIE_FRONTEND_EDITION(286, 552), TIE_FRONTEND_EDITION(64, 147),
				   TIE_FRONTEND_EDITION(320, 639), TIE_FRONTEND_EDITION(120, 302));
	inp = xinput_Alloc_Input(parent, &frame, 0, 0);
	xinpattr_Set_Input_Update_Function(inp, tourdesk_iupdate_TourDesk);
	xinpattr_Set_Input_User_Function(inp, (InputUserFunc)tourdesk_iuser_TourDesk);
	inp->mouseUsage = allInput;
	inp->id = 1;

	/* Next battle (id=2) */
	xrect_Set_Rect(&frame, TIE_FRONTEND_EDITION(130, 306), TIE_FRONTEND_EDITION(132, 401),
				   TIE_FRONTEND_EDITION(180, 338), TIE_FRONTEND_EDITION(164, 434));
	inp = xinput_Alloc_Input(parent, &frame, 0, 0);
	xinpattr_Set_Input_Update_Function(inp, tourdesk_iupdate_TourDesk);
	xinpattr_Set_Input_User_Function(inp, (InputUserFunc)tourdesk_iuser_TourDesk);
	inp->mouseUsage = allInput;
	inp->id = 2;

	/* Previous battle (id=3) */
	xrect_Set_Rect(&frame, TIE_FRONTEND_EDITION(130, 307), TIE_FRONTEND_EDITION(164, 436),
				   TIE_FRONTEND_EDITION(180, 338), TIE_FRONTEND_EDITION(196, 466));
	inp = xinput_Alloc_Input(parent, &frame, 0, 0);
	xinpattr_Set_Input_Update_Function(inp, tourdesk_iupdate_TourDesk);
	xinpattr_Set_Input_User_Function(inp, (InputUserFunc)tourdesk_iuser_TourDesk);
	inp->mouseUsage = allInput;
	inp->id = 3;

	xres_Close_Resource(res_file);

	xview_Set_View_Update_Function(tourdesk_end_View);
	xviewadd_Clear_View();
	xview_Disable_All_View_Erase();
#ifdef TIE_MODERN
	TieTourDesk_RunView(TieProfile_UsesTie98Frontend());
	return 0;
#else
	shellext_Handle_TIE_View();
	xview_Enable_All_View_Erase();
	xview_Clear_View_Update_Function();

	if (xcursor_Is_Cursor_Visible())
		xcursor_Hide_Cursor();
	return xerror_Get_Landru_Exit();
#endif
}

/* ================================================================
 * View update callback
 * ================================================================ */

// FUNCTION: TIE95 0x73AF4
// FUNCTION: TIE98 0x490E40
static void tourdesk_end_View(int32_t frame_num) {
	if (frame_num)
		return;
	if (TIE_FRONTEND_TIE98)
		xactor_Set_Actor_ZPlane(tourdesk_actor, 40);
	if (!xcursor_Is_Cursor_Visible())
		xcursor_Show_Cursor();
}

/* ================================================================
 * XINPUT callbacks
 * ================================================================ */

// FUNCTION: TIE95 0x73B18
// FUNCTION: TIE98 0x490E70
static int16_t tourdesk_iupdate_TourDesk(Input* input, Rect* bounds, Rect* clip, int16_t key, uint8_t left,
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
	if (left == (uint8_t)3 || right == (uint8_t)3) {
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
				xactor_Set_Actor_State(buttons, 1, 0);
				soundext_Play_SFX(sfxButton, 80);
				shipext_Next_Battle();
				tour_time = 0;
				if (!galaxy_art_actor[pilot_record.cur_battle])
					galaxy_art_actor[pilot_record.cur_battle] = shipext_Get_Battle_Galaxy_Image();
				break;
			case 3: /* Previous battle */
				xactor_Set_Actor_State(TIE_FRONTEND_EDITION(buttons, down_button), TIE_FRONTEND_EDITION(1, 0),
									   0);
				soundext_Play_SFX(sfxButton, 80);
				shipext_Last_Battle();
				tour_time = 0;
				if (!galaxy_art_actor[pilot_record.cur_battle])
					galaxy_art_actor[pilot_record.cur_battle] = shipext_Get_Battle_Galaxy_Image();
				break;
			default:
				break;
		}
	} else if (left || right) {
		/* Button hover state for next/prev arrows */
		switch (input->id) {
			case 2:
				xactor_Set_Actor_State(buttons, TIE_FRONTEND_EDITION(2, 0), 0);
				break;
			case 3:
				xactor_Set_Actor_State(TIE_FRONTEND_EDITION(buttons, down_button), TIE_FRONTEND_EDITION(0, 1),
									   0);
				break;
			default:
				break;
		}
	}
	return 1;
}

// FUNCTION: TIE95 0x73CB8
// FUNCTION: TIE98 0x491010
static int16_t tourdesk_iuser_TourDesk(Input* input, int32_t time) {
	(void)time;
	if (!input->var1)
		return 1;

	if (input->var2 == SCENE_BRIEF) {
		shipext_Set_Tour_Battle();
		if (pilot_record.cur_battle != cur_tour_battle)
			input->var2 = SCENE_TOUR_CUTSCENE;
	}
	xerror_Set_Landru_Exit(input->var2);
	return 1;
}

/* ================================================================
 * Actor callbacks
 * ================================================================ */

// FUNCTION: TIE95 0x73D04
// FUNCTION: TIE98 0x491060
static int tourdesk_user_Title(Actor* actor, int32_t time) {
	(void)time;
	if (actor->var1 == 1) {
		if (!xactor_Is_Actor_Visible(actor))
			xactor_Show_Actor(actor);
		actor->var1 = 0;
	} else {
		if (xactor_Is_Actor_Visible(actor))
			xactor_Hide_Actor(actor);
	}
	return 1;
}

// FUNCTION: TIE95 0x73D58
// FUNCTION: TIE98 0x4910B0
static int16_t tourdesk_draw_Title(Actor* actor, Rect* bounds, Rect* clip, int16_t xoff, int16_t yoff,
								   int16_t refresh) {
	int16_t offx, offy;
	Rect r;
	char label[32];
	if (!refresh)
		return 0;

	xactdelt_Draw_Delta_Actor(actor, bounds, clip, xoff, yoff, refresh);

	xactor_Get_Actor_Offset(actor, &offx, &offy);
	xrect_Set_Rect(&r, offx, offy, offx + actor->w, offy + actor->h);

	switch (actor->var2) {
		case 0:
			strcpy(label, textext_Get_Text(txtTourMainMenu));
			break;
		case 1:
			if (pilot_record.battle_status[pilot_record.cur_battle] == 3)
				strcpy(label, textext_Get_Text(txtTourCutscene));
			else
				strcpy(label, textext_Get_Text(txtTourJoin));
			break;
		case 2:
			strcpy(label, textext_Get_Text(txtTourNext));
			break;
		case 3:
			strcpy(label, textext_Get_Text(txtTourPrev));
			break;
#ifdef TIE_MODERN
		default:
			label[0] = 0;
			break;
#endif
	}

	xrect_Offset_Rect(&r, 1, 1);
	xfont_Print_Centered_Text(label, &r, TIE_FRONTEND_EDITION(0, 2), 16);
	xrect_Offset_Rect(&r, -1, -1);
	xfont_Print_Centered_Text(label, &r, TIE_FRONTEND_EDITION(0, 2), 15);
	return 1;
}

// FUNCTION: TIE95 0x73E6C
// FUNCTION: TIE98 0x491200
static void tourdesk_user_Door(Actor* actor, int32_t time) {
	(void)time;
	if (actor->var1) {
		if (!actor->state)
			soundext_Play_SFX(sfxAirLock, 90);
		if (actor->state < actor->arraySize - 1)
			xactor_Set_Actor_State(actor, actor->state + 1, 0);
		actor->var1 = 0;
	} else {
		if (actor->state > 0) {
			xactor_Set_Actor_State(actor, actor->state - 1, 0);
			if (!actor->state)
				soundext_Play_SFX(sfxLargeDoorShut, 90);
		}
	}
}

// FUNCTION: TIE95 0x73EEC
// FUNCTION: TIE98 0x491280
static int16_t tourdesk_user_Battle(Actor* actor, int32_t time) {
	(void)actor;
	if (!time) {
		tour_time = 0;
		shade_Build_Shaded_Palette();
	} else {
		tour_time++;
	}
	return 1;
}

/* ================================================================
 * Battle text draw
 * ================================================================ */

// FUNCTION: TIE95 0x73F0C
// FUNCTION: TIE98 0x4912B0
static int16_t tourdesk_draw_Battle_Text(Actor* actor, Rect* r, Rect* clip_r, int16_t x, int16_t y,
										 int16_t refresh) {
	Rect dst;
	int16_t line_height, i;
	int16_t font_id = TIE_FRONTEND_EDITION(0, 2);
	(void)actor;
	(void)clip_r;
	(void)x;
	(void)y;
	if (!refresh)
		return 0;

	xpaint_Paint_Clipped_Rect(r, 0);
	xrect_Copy_Rect(&dst, r);
	line_height = TIE_FRONTEND_EDITION(9, (int16_t)xfont_Get_FontID_Height(font_id));
	dst.bottom = dst.top + line_height;

	for (i = 0; i < 3; i++) {
		int16_t color;
		char buf[64];
		shipext_Get_Battle_Title(buf, i);
		color = i ? 2 : 15;
		xfont_Print_Centered_Text(buf, &dst, font_id, color);
		xrect_Offset_Rect(&dst, 0, line_height);
	}

	xdirty_Dirty_Rect(r);
	return 1;
}

/* ================================================================
 * Galaxy zoom animation phases (Draw_Battle helpers)
 * ================================================================ */

/* Phase 0-7: zoom from center to galaxy rect */
// FUNCTION: TIE95 0x73FA0
// FUNCTION: TIE98 0x491360
static int16_t tourdesk_Draw_Battle_One(Rect* galaxy_rect, int32_t time) {
	Rect dst, ra;
	char name[64];
	int16_t font_id = TIE_FRONTEND_EDITION(0, 2);
	bool dynamic_text_layout = TIE_FRONTEND_EDITION(false, true);
	if (time < 8) {
		xrect_Copy_Rect(&dst, galaxy_rect);
		xrect_Copy_Rect(&ra, galaxy_rect);

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

		if (!xrect_Empty_Rect(&dst))
			shade_Draw_Talk_Shade_Rect(&dst);

		xrect_Inset_Rect(&ra, -64, 0);
		if (dynamic_text_layout) {
			uint16_t font_height = (uint16_t)xfont_Get_FontID_Height(font_id);
			ra.top = ra.bottom + (font_height >> 1);
			ra.bottom = ra.top + (int16_t)xfont_Get_FontID_Height(font_id);
		} else {
			ra.top = ra.bottom + 2;
			ra.bottom += 10;
		}
		xrect_Offset_Rect(&ra, -4, 0);

		xfont_Enable_FontID_Shadow(font_id);
		shipext_Get_Battle_Galaxy_Name(name);
		xfont_Print_Centered_Text(name, &ra, font_id, 2 * time + 16);
		xfont_Disable_FontID_Shadow(font_id);
	}
	return 1;
}

/* Phase 8-23: hold at galaxy rect */
// FUNCTION: TIE95 0x74118
// FUNCTION: TIE98 0x491500
static void tourdesk_Draw_Battle_Two(Rect* galaxy_rect, int16_t time, Rect* clip_r) {
	Rect dst, ra;
	char name[64];
	int16_t font_id = TIE_FRONTEND_EDITION(0, 2);
	bool dynamic_text_layout = TIE_FRONTEND_EDITION(false, true);
	(void)clip_r;
	if (time < 8 || time >= 24)
		return;

	xrect_Copy_Rect(&dst, galaxy_rect);
	shade_Draw_Talk_Shade_Rect(&dst);

	xrect_Copy_Rect(&ra, &dst);
	xrect_Inset_Rect(&ra, -64, 0);
	if (dynamic_text_layout) {
		int16_t font_height = (int16_t)xfont_Get_FontID_Height(font_id);
		ra.top = ra.bottom + (font_height >> 1);
		ra.bottom = ra.top + (int16_t)xfont_Get_FontID_Height(font_id);
	} else {
		ra.top = ra.bottom + 2;
		ra.bottom += 10;
	}
	xrect_Offset_Rect(&ra, -4, 0);

	xfont_Enable_FontID_Shadow(font_id);
	shipext_Get_Battle_Galaxy_Name(name);
	xfont_Print_Centered_Text(name, &ra, font_id, 31);
	xfont_Disable_FontID_Shadow(font_id);
}

/* Phase 24-31: zoom from galaxy rect to actor bounds */
// FUNCTION: TIE95 0x741BC
// FUNCTION: TIE98 0x4915E0
static void tourdesk_Draw_Battle_Three(Rect* galaxy_rect, Rect* view_r, int16_t time) {
	Actor* art;
	Rect dst, art_bounds;
	int16_t t;
	if (time < 24 || time >= 32)
		return;

	art = galaxy_art_actor[pilot_record.cur_battle];
	xrect_Copy_Rect(&dst, galaxy_rect);
	xactor_Get_Actor_Bounds(art, &art_bounds);
	xrect_Offset_Rect(&art_bounds, view_r->left, view_r->top);

	t = time - 24;
	dst.left += (t * (art_bounds.left - dst.left)) >> 3;
	dst.top += (t * (art_bounds.top - dst.top)) >> 3;
	dst.right += (t * (art_bounds.right - dst.right)) >> 3;
	dst.bottom += (t * (art_bounds.bottom - dst.bottom)) >> 3;

	if (!xrect_Empty_Rect(&dst))
		shade_Draw_Talk_Shade_Rect(&dst);
}

/* Phase 32-39: reveal actor with vertical wipe */
// FUNCTION: TIE95 0x742A0
// FUNCTION: TIE98 0x4916F0
static int16_t tourdesk_Draw_Battle_Four(Rect* galaxy_rect, Rect* view_r, Rect* clip_r, int time) {
	Actor* art;
	Rect bounds;
	(void)galaxy_rect;
	if (time >= 32 && time < 40) {
		time -= 32;
		art = galaxy_art_actor[pilot_record.cur_battle];
		xactor_Get_Actor_Bounds(art, &bounds);
		xrect_Offset_Rect(&bounds, view_r->left, view_r->top);
		if (TIE_FRONTEND_TIE98)
			xrect_Clip_Rect(&bounds, clip_r);

		if (!xrect_Empty_Rect(&bounds))
			shade_Draw_Talk_Shade_Rect(&bounds);

		bounds.bottom = ((time * (bounds.bottom - bounds.top)) >> 3) + bounds.top;
		if (xrect_Clip_Rect(&bounds, clip_r)) {
			xcanvas_Set_Drawing_Canvas_Clip(&bounds);
			xactdelt_Draw_Delta_Actor(art, view_r, &bounds, view_r->left, view_r->top, 1);
		}
	}
	return 1;
}

/* Phase 40+: final state — full art + battle info text */
// FUNCTION: TIE95 0x7434C
// FUNCTION: TIE98 0x4917E0
static int16_t tourdesk_Draw_Battle_Five(Rect* r, Rect* clip_r, int time) {
	Actor* art;
	Rect art_bounds, dst;
	char text[64], label[16];
	if (time < 40)
		return 1;

	time -= 40;
	art = galaxy_art_actor[pilot_record.cur_battle];

	xactdelt_Draw_Delta_Actor(art, r, clip_r, r->left, r->top, 1);

	xactor_Get_Actor_Bounds(art, &art_bounds);
	xrect_Offset_Rect(&art_bounds, r->left, r->top);

	xfont_Enable_FontID_Shadow(TIE_FRONTEND_EDITION(0, 2));

	/* Galaxy name with fade-in */
	shipext_Get_Battle_Galaxy_Name(text);
	if (time < 8)
		xfont_Print_Centered_Text(text, &art_bounds, TIE_FRONTEND_EDITION(0, 2), time + time + 16);
	else
		xfont_Print_Centered_Text(text, &art_bounds, TIE_FRONTEND_EDITION(0, 2), 31);

	/* "Battle N" text */
	xrect_Copy_Rect(&dst, r);
	dst.left = art_bounds.right;
	if (TIE_FRONTEND_TIE98) {
		dst.top = art_bounds.top + 3 * xfont_Get_FontID_Height(2);
		dst.bottom = dst.top + xfont_Get_FontID_Height(2);
	} else {
		dst.top = art_bounds.top + 28;
		dst.bottom = art_bounds.top + 38;
	}

	textext_Copy_Text(label, txtCompInfoBattle);
	sprintf(text, label, pilot_record.cur_battle + 1);

	if (pilot_record.battle_status[pilot_record.cur_battle] == 3)
		xrect_Offset_Rect(&dst, 0, 4);

	if (time < 8)
		xfont_Print_Centered_Text(text, &dst, TIE_FRONTEND_EDITION(0, 2), time + time + 16);
	else
		xfont_Print_Centered_Text(text, &dst, TIE_FRONTEND_EDITION(0, 2), 31);

	/* "Mission N" text (only if battle not complete) */
	xrect_Offset_Rect(&dst, 0, TIE_FRONTEND_EDITION(10, xfont_Get_FontID_Height(2)));
	if (pilot_record.battle_status[pilot_record.cur_battle] != 3) {
		strcpy(label, textext_Get_Text(txtCombatMission));
		sprintf(text, "%s %d", label, pilot_record.battle_cursor[pilot_record.cur_battle] + 1);
		if (time < 8)
			xfont_Print_Centered_Text(text, &dst, TIE_FRONTEND_EDITION(0, 2), time + time + 16);
		else
			xfont_Print_Centered_Text(text, &dst, TIE_FRONTEND_EDITION(0, 2), 31);
	}

	xfont_Disable_FontID_Shadow(TIE_FRONTEND_EDITION(0, 2));
	return 1;
}

/* ================================================================
 * Galaxy zoom composite draw
 * ================================================================ */

// FUNCTION: TIE95 0x74534
// FUNCTION: TIE98 0x491A30
static int16_t tourdesk_draw_Battle(Actor* actor, Rect* r, Rect* clip_r, int16_t x, int16_t y,
									int16_t refresh) {
	Rect galaxy_rect;
	int16_t t;
	(void)actor;
	(void)x;
	(void)y;
	if (!refresh)
		return 0;

	shipext_Get_Battle_Galaxy_Rect(&galaxy_rect);
	if (TIE_FRONTEND_TIE98) {
		galaxy_rect.top *= 2;
		galaxy_rect.left *= 2;
		galaxy_rect.bottom *= 2;
		galaxy_rect.right *= 2;
	}
	xrect_Offset_Rect(&galaxy_rect, r->left, r->top);

	if (!galaxy_art_actor[pilot_record.cur_battle]) {
		if (TIE_FRONTEND_TIE98) {
			xactor_Refresh_Actor(tourdesk_actor);
			xactor_Dirty_Actor(tourdesk_actor);
		}
		xdirty_Dirty_Rect(r);
		return 1;
	}

	t = (int16_t)tour_time;

	tourdesk_Draw_Battle_One(&galaxy_rect, t);
	tourdesk_Draw_Battle_Two(&galaxy_rect, t, clip_r);
	tourdesk_Draw_Battle_Three(&galaxy_rect, r, t);
	tourdesk_Draw_Battle_Four(&galaxy_rect, r, clip_r, t);
	tourdesk_Draw_Battle_Five(r, clip_r, t);

	if (TIE_FRONTEND_TIE98) {
		xactor_Refresh_Actor(tourdesk_actor);
		xactor_Dirty_Actor(tourdesk_actor);
	}
	xdirty_Dirty_Rect(r);
	return 1;
}
