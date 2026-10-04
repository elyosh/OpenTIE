/*
 * DEBRIEF.C — Mission debrief screen.
 *
 * Film-driven debrief room with officer/priest characters, animated
 * doors, and exit routing based on mission outcome. Characters lean
 * forward when the mouse hovers their door area.
 *
 * 9 functions. Recovered from the TIE95 and TIE98 executables.
 */

#include "tie/debrief.h"
#ifdef TIE_MODERN
#include "tie_runtime/runtime/debrief_task.h"
#endif
#include "tie/edition.h"
#include "tie/shellext.h"
#include "tie/shipext.h"
#include "tie/soundext.h"
#include "tie/textext.h"
#include "tie/tie.h"
#include "tie_runtime/runtime/profile.h"
#ifdef TIE_MODERN
#include "tie_runtime/snapshot/snapshot_internal.h"
#endif

#include "landru/actdelt.h"
#include "landru/actor.h"
#include "landru/cursor.h"
#include "landru/error.h"
#include "landru/film.h"
#include "landru/font.h"
#include "landru/inpattr.h"
#include "landru/input.h"
#include "landru/io.h"
#include "landru/rect.h"
#include "landru/res.h"
#include "landru/surface.h"
#include "landru/view.h"
#include "landru/viewadd.h"

#include <stdlib.h>
#include <string.h>

/* The original TU calls the library strcpy() rather than the inline form. */
#ifdef __WATCOMC__
#pragma function(strcpy)
#endif

/* ---- Static globals ---- */

// GLOBAL: TIE95 0xF5D68
// GLOBAL: TIE98 0x50F800
static Actor* door_actors[2]; /* door[0]=brief door, door[1]=fly-again door */
// GLOBAL: TIE95 0xF5D84
// GLOBAL: TIE98 0x50F7EC
static Input* priest; /* priest widget (id=2) */
// GLOBAL: TIE95 0xF5D8C
// GLOBAL: TIE98 0x50F7E0
static Input* flyagain; /* fly-again widget (id=3) */
// GLOBAL: TIE95 0xF5D70
// GLOBAL: TIE98 0x50F7E8
static Film* debrief_film;
// GLOBAL: TIE95 0xF5D74
// GLOBAL: TIE98 0x50F808
static Actor* title_actor;
// GLOBAL: TIE95 0xF5D78
// GLOBAL: TIE98 0x50F7F0
static Input* parent;
// GLOBAL: TIE95 0xF5D7C
// GLOBAL: TIE98 0x50F7F8
static Input* brief_input;
// GLOBAL: TIE95 0xF5D80
// GLOBAL: TIE98 0x50F7F4
static Input* officer; /* officer widget (id=1) */

/* ---- Forward declarations ---- */

static int16_t debrief_user_Title(Actor* actor, int32_t time);
static int16_t debrief_draw_Title(Actor* actor, Rect* bounds, Rect* clip, int16_t xoff, int16_t yoff,
								  int16_t refresh);
static int16_t debrief_user_Door(Actor* actor, int32_t time);
static int16_t debrief_user_Officer(Actor* actor, int32_t time);

static void debrief_end_View(int32_t frame_num);
static int16_t debrief_film_Callback(Film* film, FilmObject* film_object);
static int16_t debrief_iupdate_Debrief(Input* input, Rect* bounds, Rect* clip, int16_t key, uint8_t left,
									   uint8_t right, int16_t mouse_x, int16_t mouse_y);
static int16_t debrief_iuser_Debrief(Input* input, int32_t time);

/* ================================================================
 * Entry point
 * ================================================================ */

// FUNCTION: TIE95 0x6FE60
// FUNCTION: TIE98 0x4155F0
int16_t debrief_Debrief(SceneHeadStruct* scene_head) {
	Rect frame;
	ResFile* resource;
	int16_t mouse_x, mouse_y;

	/* Position mouse based on outcome and officer type */
	if (shipext_Is_Mission_Success()) {
		if (shellext_Get_Last_Scene() != SCENE_TALK_DEBRIEF_OFFICER || shipext_Get_Mission_Officer() == 1) {
			mouse_x = TIE_FRONTEND_EDITION(180, 360);
			mouse_y = TIE_FRONTEND_EDITION(100, 200);
		} else {
			mouse_x = TIE_FRONTEND_EDITION(280, 540);
			mouse_y = TIE_FRONTEND_EDITION(120, 240);
		}
	} else {
		mouse_x = TIE_FRONTEND_EDITION(74, 108);
		mouse_y = TIE_FRONTEND_EDITION(100, 200);
	}
	xio_Set_Mouse_Position(mouse_x, mouse_y);

	/* Load resources */
	resource = shellext_Open_Empire_Resource("debrief.lfd");
	xrect_Set_Rect(&frame, 0, 0, TIE_FRONTEND_EDITION(320, 640), TIE_FRONTEND_EDITION(200, 480));

	debrief_film = xfilm_Res_Callback_Film("debrief", &frame, 0, 0, 0, debrief_film_Callback);
#ifdef TIE_MODERN
	TieSnapshotBuilder_SetActiveFilm("DEBRIEF", "debrief");
#endif
	xfilm_Set_Film_Def_Palette(debrief_film, scene_head->def_palette);

	/* Create XINPUT widgets */
	parent = xinput_Alloc_Input(NULL, &frame, 0, 0);

	/* Brief door (id=0) */
	xrect_Set_Rect(&frame, TIE_FRONTEND_EDITION(133, 298), TIE_FRONTEND_EDITION(56, 129),
				   TIE_FRONTEND_EDITION(193, 420), TIE_FRONTEND_EDITION(107, 288));
	brief_input = xinput_Alloc_Input(parent, &frame, 0, 0);
	xinpattr_Set_Input_Update_Function(brief_input, debrief_iupdate_Debrief);
	xinpattr_Set_Input_User_Function(brief_input, (InputUserFunc)(void (*)(void))debrief_iuser_Debrief);
	brief_input->mouseUsage = allInput;
	brief_input->id = 0;

	/* Officer door (id=1) — skip if priest only */
	if (shipext_Get_Mission_Officer() != 2) {
		xrect_Set_Rect(&frame, TIE_FRONTEND_EDITION(85, 226), TIE_FRONTEND_EDITION(35, 102),
					   TIE_FRONTEND_EDITION(133, 296), TIE_FRONTEND_EDITION(150, 322));
		officer = xinput_Alloc_Input(parent, &frame, 0, 0);
		xinpattr_Set_Input_Update_Function(officer, debrief_iupdate_Debrief);
		xinpattr_Set_Input_User_Function(officer, (InputUserFunc)(void (*)(void))debrief_iuser_Debrief);
		officer->mouseUsage = allInput;
		officer->id = 1;
	}

	/* Priest door (id=2) — skip if officer only */
	if (shipext_Get_Mission_Officer() != 1) {
		xrect_Set_Rect(&frame, TIE_FRONTEND_EDITION(248, 500), TIE_FRONTEND_EDITION(51, 134),
					   TIE_FRONTEND_EDITION(290, 572), TIE_FRONTEND_EDITION(128, 316));
		priest = xinput_Alloc_Input(parent, &frame, 0, 0);
		xinpattr_Set_Input_Update_Function(priest, debrief_iupdate_Debrief);
		xinpattr_Set_Input_User_Function(priest, (InputUserFunc)(void (*)(void))debrief_iuser_Debrief);
		priest->mouseUsage = allInput;
		priest->id = 2;
	}

	/* Fly-again area (id=3) */
	xrect_Set_Rect(&frame, TIE_FRONTEND_EDITION(0, 56), TIE_FRONTEND_EDITION(0, 26),
				   TIE_FRONTEND_EDITION(70, 145), TIE_FRONTEND_EDITION(200, 345));
	flyagain = xinput_Alloc_Input(parent, &frame, 0, 0);
	xinpattr_Set_Input_Update_Function(flyagain, debrief_iupdate_Debrief);
	xinpattr_Set_Input_User_Function(flyagain, (InputUserFunc)(void (*)(void))debrief_iuser_Debrief);
	flyagain->mouseUsage = allInput;
	flyagain->id = 3;

	xview_Set_View_Update_Function(debrief_end_View);
	xviewadd_Clear_View();
	xview_Disable_All_View_Erase();
#ifdef TIE_MODERN
	TieDebrief_RunView(resource, TieProfile_UsesTie98Frontend());
	return 0;
#else
	shellext_Handle_TIE_View();
	xview_Enable_All_View_Erase();
	xview_Clear_View_Update_Function();

	if (xcursor_Is_Cursor_Visible())
		xcursor_Hide_Cursor();

	xres_Close_Resource(resource);
	return xerror_Get_Landru_Exit();
#endif
}

/* ================================================================
 * View update callback
 * ================================================================ */

// FUNCTION: TIE95 0x700E4
// FUNCTION: TIE98 0x415920
static void debrief_end_View(int32_t frame_num) {
	if (frame_num)
		return;
	if (!xcursor_Is_Cursor_Visible())
		xcursor_Show_Cursor();
}

/* ================================================================
 * Film callback — register actors by var1
 * ================================================================ */

// FUNCTION: TIE95 0x7012C
// FUNCTION: TIE98 0x415940
static int16_t debrief_film_Callback(Film* film, FilmObject* film_object) {
	Actor* actor;
	int16_t hide = 0;

	if (film_object->id != 3) /* type_code: 3 = actor */
		return hide;

	xfilm_Rewind_Actor_Film(film, film_object, (void*)((char*)film_object + sizeof(FilmObject)));
	actor = (Actor*)film_object->object;

	switch (actor->var1) {
		case 1: /* Background */
			xactor_Non_Refreshable_Actor(actor);
			break;

		case 2: /* Door actor — stored by var2 index */
			xactor_Set_Actor_User_Function(actor, (xactorCallback)(void (*)(void))debrief_user_Door);
			door_actors[actor->var2] = actor;
			break;

		case 3: /* Officer/priest character */
			switch (actor->var2) {
				case 0:
				case 1:
				case 2:
					if (shipext_Get_Mission_Officer() != 2) {
						xactor_Set_Actor_User_Function(actor,
													   (xactorCallback)(void (*)(void))debrief_user_Officer);
						actor->id = actor->var2;
					} else {
						hide = 1; /* hide if priest-only */
					}
					break;

				case 3:
				case 7:
					if (shipext_Get_Mission_Officer() == 2)
						hide = 1;
					break;

				case 4:
					if (shipext_Get_Mission_Officer() != 1) {
						xactor_Set_Actor_User_Function(actor,
													   (xactorCallback)(void (*)(void))debrief_user_Officer);
						actor->id = 3;
					} else {
						hide = 1; /* hide if officer-only */
					}
					break;

				case 8:
					if (shipext_Get_Mission_Officer() == 1)
						hide = 1;
					break;

				case 5:
					/* TIE98 reverses the officer-specific door variants 5 and 6. */
					if (TIE_FRONTEND_EDITION(shipext_Get_Mission_Officer() != 2,
											 shipext_Get_Mission_Officer() == 2))
						hide = 1;
					break;

				case 6:
					if (TIE_FRONTEND_EDITION(shipext_Get_Mission_Officer() != 1,
											 shipext_Get_Mission_Officer() == 1))
						hide = 1;
					break;
			}
			break;

		case 4: /* Title label */
			/* TIE95 shows only the title variant matching the mission's officer. */
			if (!TIE_FRONTEND_TIE98) {
				if (shipext_Get_Mission_Officer() == 2) {
					if (!actor->var2)
						hide = 1;
				} else if (actor->var2) {
					hide = 1;
				}
			}
			if (!hide) {
				xactor_Set_Actor_User_Function(actor, (xactorCallback)(void (*)(void))debrief_user_Title);
				xactor_Set_Actor_Draw_Function(actor, debrief_draw_Title);
				title_actor = actor;
			}
			break;
	}
	return hide;
}

/* ================================================================
 * XINPUT callbacks
 * ================================================================ */

// FUNCTION: TIE95 0x702AC
// FUNCTION: TIE98 0x415AA0
static int16_t debrief_iupdate_Debrief(Input* input, Rect* bounds, Rect* clip, int16_t key, uint8_t left,
									   uint8_t right, int16_t mouse_x, int16_t mouse_y) {
	int16_t widget_id;

	(void)bounds;
	(void)clip;
	(void)mouse_x;
	(void)mouse_y;

	if (key)
		return 0;

	/* Open the door actor for brief (id=0) and fly-again (id=3) */
	widget_id = input->id;
	if (widget_id == 0)
		door_actors[0]->var1 = 1;
	else if (widget_id == 3)
		door_actors[1]->var1 = 1;

	/* Show title label */
	title_actor->var1 = 1;
	title_actor->var2 = input->id;

	/* Check for mouse click */
	if (left != 3 && right != 3)
		return 1;

	switch (input->id) {
		case 0: /* Briefing / Main Menu */
			input->var2 = SCENE_BRIEF;
			input->var1 = 1;
			break;
		case 1: /* Officer */
			input->var2 = SCENE_TALK_DEBRIEF_OFFICER;
			input->var1 = 1;
			break;
		case 2: /* Priest */
			input->var2 = SCENE_TALK_DEBRIEF_PRIEST;
			input->var1 = 1;
			break;
		case 3: /* Fly Again */
			input->var1 = 1;
			if (mission.beam_used || mission.torp_used) {
				input->var2 = SCENE_ARM_SHIP;
			} else if (shipext_Is_Mission_Launch()) {
				input->var2 = SCENE_CUT_BATTLE_270;
			} else {
				input->var2 = SCENE_FLIGHT_BATTLE;
			}
			break;
		default:
			break;
	}
	return 1;
}

// FUNCTION: TIE95 0x703B4
// FUNCTION: TIE98 0x415B90
static int16_t debrief_iuser_Debrief(Input* input, int32_t time) {
	int16_t scene;

	(void)time;
	if (input->var1) {       /* exit_pending */
		scene = input->var2; /* exit_code */
		switch (scene) {
			case SCENE_BRIEF:
				/* Tour battle — commit and check if done */
				if (!shipext_Set_Tour_Battle())
					scene = SCENE_MAIN_MENU;
				break;
			case SCENE_CUT_BATTLE_270:
			case SCENE_FLIGHT_BATTLE:
				/* Refly / simulator */
				if (shipext_Is_Mission_Success()) {
					if (shipext_Read_Temp_Pilot())
						shipext_Update_Pilot();
					else
						shipext_Refly_Tour_Mission();
				}
				break;
			case SCENE_ARM_SHIP:
				/* Alternative refly */
				if (shipext_Is_Mission_Success() && !shipext_Read_Temp_Pilot())
					shipext_Refly_Tour_Mission();
				break;
		}
		xerror_Set_Landru_Exit(scene);
	}
	return 1;
}

/* ================================================================
 * Title label callbacks
 * ================================================================ */

// FUNCTION: TIE95 0x70440
// FUNCTION: TIE98 0x415C60
static int16_t debrief_user_Title(Actor* actor, int32_t time) {
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

// FUNCTION: TIE95 0x70494
// FUNCTION: TIE98 0x415CB0
static int16_t debrief_draw_Title(Actor* actor, Rect* bounds, Rect* clip, int16_t xoff, int16_t yoff,
								  int16_t refresh) {
	int16_t offy, offx;
	Rect r;
	char label[32];

	if (!refresh)
		return 0;

	xactdelt_Draw_Delta_Actor(actor, bounds, clip, xoff, yoff, refresh);

	xactor_Get_Actor_Offset(actor, &offx, &offy);

	xrect_Set_Rect(&r, offx, offy, offx + actor->w, offy + actor->h);

	switch (actor->var2) {
		case 0:
			if (shipext_Is_Tour_Battle_End())
				strcpy(label, textext_Get_Text(txtBriefMainMenu));
			else
				strcpy(label, textext_Get_Text(txtDebriefBrief));
			break;
		case 1:
			strcpy(label, textext_Get_Text(txtDebriefOfficer));
			break;
		case 2:
			strcpy(label, textext_Get_Text(txtDebriefPriest));
			break;
		case 3:
			strcpy(label, textext_Get_Text(txtDebriefAgain));
			break;
	}

	/* Drop shadow */
	xrect_Offset_Rect(&r, 1, 1);
	xfont_Print_Centered_Text(label, &r, TIE_FRONTEND_EDITION(0, 2), 16);
	xrect_Offset_Rect(&r, -1, -1);
	xfont_Print_Centered_Text(label, &r, TIE_FRONTEND_EDITION(0, 2), 15);
	return 1;
}

/* ================================================================
 * Door callback
 * ================================================================ */

// FUNCTION: TIE95 0x70598
// FUNCTION: TIE98 0x415DE0
static int16_t debrief_user_Door(Actor* actor, int32_t time) {
	if (!time) {
		actor->var2 = 0;
		actor->var1 = 0;
	}

	if (actor->var1) {
		/* Opening */
		if (!actor->state)
			soundext_Play_SFX(sfxSmallDoorOpen, 80);
		if (actor->state < actor->arraySize - 1)
			xactor_Set_Actor_State(actor, actor->state + 1, 0);
		actor->var1 = 0;
	} else {
		/* Closing */
		if (actor->state > 0) {
			xactor_Set_Actor_State(actor, actor->state - 1, 0);
			if (!actor->state)
				soundext_Play_SFX(sfxSmallDoorShut, 80);
		}
	}
	return 1;
}

/* ================================================================
 * Officer/priest character animation
 * ================================================================ */

// FUNCTION: TIE95 0x7063C
// FUNCTION: TIE98 0x415E70
static int16_t debrief_user_Officer(Actor* actor, int32_t time) {
	int16_t anim_state;

	if (!time) {
		actor->var2 = 0;
		actor->var1 = 0;
	}

	switch (actor->id) {
		case 0: /* Officer facing */
			if (title_actor->var1 && title_actor->var2 == 1) {
				if (actor->var1 < 10)
					actor->var1++;
			} else {
				if (actor->var1 > 0)
					actor->var1--;
			}

			if (actor->var1 > 5)
				anim_state = actor->var1 / 2 - 2;
			else
				anim_state = 0;

			if (anim_state)
				xactor_Set_Actor_ZPlane(actor, 15);
			else
				xactor_Set_Actor_ZPlane(actor, 30);
			xactor_Set_Actor_State(actor, anim_state, 0);
			return 1;

		case 1: { /* Officer side */
			int side_state;

			if (title_actor->var1 && title_actor->var2 == 1) {
				if (actor->var1 < 10)
					actor->var1++;
			} else {
				if (actor->var1 > 0)
					actor->var1--;
			}

			if (actor->var1 > 5)
				side_state = actor->var1 / 2 - 2;
			else
				side_state = 0;

			if (!TIE_FRONTEND_TIE98 && (int16_t)side_state == 3)
				side_state = 2;
			xactor_Set_Actor_State(actor, side_state, 0);
			return 1;
		}

		case 2: /* Priest */
			if (title_actor->var1 && title_actor->var2 == 1) {
				if (actor->var1 < 10)
					actor->var1++;
			} else {
				if (actor->var1 > 0)
					actor->var1--;
			}

			if (actor->var1 < 4)
				anim_state = actor->var1;
			else
				anim_state = 3;

			xactor_Set_Actor_State(actor, anim_state, 0);
			return 1;

		case 3: /* Alternate (priest variant) */
			if (title_actor->var1 && title_actor->var2 == 2) {
				if (actor->var1 < 1)
					actor->var1++;
			} else {
				if (actor->var1 > 0)
					actor->var1--;
			}

			anim_state = actor->var1;
			xactor_Set_Actor_State(actor, anim_state, 0);
			return 1;
	}
	return 1;
}
