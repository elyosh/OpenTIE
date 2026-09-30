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

/* ---- Edition data ---- */

typedef enum DebriefActorVariantSet {
	DEBRIEF_ACTORS_VGA,
	DEBRIEF_ACTORS_SVGA,
} DebriefActorVariantSet;

typedef struct DebriefSpec {
	LandruSurfaceSet surface_set;
	DebriefActorVariantSet actor_variants;
	int16_t width, height;
	int16_t success_mouse_x, success_mouse_y;
	int16_t talk_mouse_x, talk_mouse_y;
	int16_t failure_mouse_x, failure_mouse_y;
	int16_t input_bounds[4][4];
	int16_t title_font;
	int16_t officer_side_max_state;
	bool title_variants;
} DebriefSpec;

/* DATA: TIE95 DEBRIEF_Debrief 0x6FE60; TIE98 0x4155F0. */
static const DebriefSpec debrief_specs[] = {
	{
		/* surface_set */ LANDRU_SURFACE_VGA,
		/* actor_variants */ DEBRIEF_ACTORS_VGA,
		/* width */ 320,
		/* height */ 200,
		/* success_mouse_x */ 180,
		/* success_mouse_y */ 100,
		/* talk_mouse_x */ 280,
		/* talk_mouse_y */ 120,
		/* failure_mouse_x */ 74,
		/* failure_mouse_y */ 100,
		/* input_bounds */
		{
			{ 133, 56, 193, 107 },
			{ 85, 35, 133, 150 },
			{ 248, 51, 290, 128 },
			{ 0, 0, 70, 200 },
		},
		/* title_font */ 0,
		/* officer_side_max_state */ 2,
		/* title_variants */ true,
	},
	{
		/* surface_set */ LANDRU_SURFACE_SVGA,
		/* actor_variants */ DEBRIEF_ACTORS_SVGA,
		/* width */ 640,
		/* height */ 480,
		/* success_mouse_x */ 360,
		/* success_mouse_y */ 200,
		/* talk_mouse_x */ 540,
		/* talk_mouse_y */ 240,
		/* failure_mouse_x */ 108,
		/* failure_mouse_y */ 200,
		/* input_bounds */
		{
			{ 298, 129, 420, 288 },
			{ 226, 102, 296, 322 },
			{ 500, 134, 572, 316 },
			{ 56, 26, 145, 345 },
		},
		/* title_font */ 2,
		/* officer_side_max_state */ 3,
		/* title_variants */ false,
	},
};

static const DebriefSpec* active_spec;

/* ---- Static globals ---- */

static Actor* door_actors[2]; /* door[0]=brief door, door[1]=fly-again door */
static Input* priest;         /* priest widget (id=2) */
static Input* flyagain;       /* fly-again widget (id=3) */
static Film* debrief_film;
// GLOBAL: TIE95 0xF5D74
static Actor* title_actor;
static Input* parent;
static Input* brief_input;
static Input* officer; /* officer widget (id=1) */

/* ---- Forward declarations ---- */

static void user_Title(Actor* actor, int32_t time);
static int16_t draw_Title(Actor* actor, Rect* bounds, Rect* clip, int16_t xoff, int16_t yoff,
						  int16_t refresh);
static void user_Door(Actor* actor, int32_t time);
static void user_Officer(Actor* actor, int32_t time);

/* ================================================================
 * View update callback
 * ================================================================ */

// FUNCTION: TIE95 0x700E4
// FUNCTION: TIE98 0x415920
static void end_View(int32_t frame_num) {
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
static int16_t film_Callback(Film* film, FilmObject* film_object) {
	Actor* actor;

	if (film_object->id != 3) /* type_code: 3 = actor */
		return 0;

	xfilm_Rewind_Actor_Film(film, film_object, (void*)((char*)film_object + sizeof(FilmObject)));
	actor = (Actor*)film_object->object;

	switch (actor->var1) {
		case 1: /* Background */
			xactor_Non_Refreshable_Actor(actor);
			return 0;

		case 2: /* Door actor — stored by var2 index */
			xactor_Set_Actor_User_Function(actor, user_Door);
			door_actors[actor->var2] = actor;
			return 0;

		case 3: /* Officer/priest character */
			switch (actor->var2) {
				case 0:
				case 1:
				case 2:
					if (shipext_Get_Mission_Officer() == 2)
						return 1; /* hide if priest-only */
					xactor_Set_Actor_User_Function(actor, user_Officer);
					actor->id = actor->var2;
					return 0;

				case 3:
				case 7:
					if (shipext_Get_Mission_Officer() != 2)
						return 0; /* keep if officer present */
					return 1;

				case 4:
					if (shipext_Get_Mission_Officer() == 1)
						return 1; /* hide if officer-only */
					xactor_Set_Actor_User_Function(actor, user_Officer);
					actor->id = 3;
					return 0;

				case 5:
					/* TIE98 reverses the officer-specific door variants 5 and 6. */
					if (active_spec->actor_variants == DEBRIEF_ACTORS_SVGA)
						return (shipext_Get_Mission_Officer() == 2) ? 1 : 0;
					return (shipext_Get_Mission_Officer() == 2) ? 0 : 1;

				case 6:
					if (active_spec->actor_variants == DEBRIEF_ACTORS_SVGA)
						return (shipext_Get_Mission_Officer() == 1) ? 1 : 0;
					/* Binary @ 0x7024F: return mission_officer != 1.
					 * Hide this variant when mission is officer-only. */
					return (shipext_Get_Mission_Officer() != 1) ? 1 : 0;

				case 8:
					/* Binary @ 0x7021F: hide when mission_officer == 1. */
					return (shipext_Get_Mission_Officer() != 1) ? 0 : 1;

				default:
					return 0;
			}

		case 4: /* Title label */
			if (active_spec->title_variants) {
				if (shipext_Get_Mission_Officer() == 2) {
					if (!actor->var2)
						return 1;
				} else {
					if (actor->var2)
						return 1;
				}
			}
			xactor_Set_Actor_User_Function(actor, user_Title);
			xactor_Set_Actor_Draw_Function(actor, draw_Title);
			title_actor = actor;
			return 0;

		default:
			return 0;
	}
}

/* ================================================================
 * XINPUT callbacks
 * ================================================================ */

// FUNCTION: TIE95 0x702AC
// FUNCTION: TIE98 0x415AA0
static int16_t iupdate_Debrief(Input* input, Rect* bounds, Rect* clip, int16_t key, uint8_t left,
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
static void iuser_Debrief(Input* input, int32_t time) {
	int16_t scene;

	(void)time;
	if (!input->var1)
		return; /* exit_pending */

	scene = input->var2; /* exit_code */

	if (scene == SCENE_BRIEF) {
		/* Tour battle — commit and check if done */
		if (!shipext_Set_Tour_Battle())
			scene = SCENE_MAIN_MENU;
	} else if (scene == SCENE_CUT_BATTLE_270 || scene == SCENE_FLIGHT_BATTLE) {
		/* Refly / simulator */
		if (shipext_Is_Mission_Success()) {
			if (!shipext_Read_Temp_Pilot()) {
				shipext_Refly_Tour_Mission();
			} else {
				shipext_Update_Pilot();
			}
		}
	} else if (scene == SCENE_ARM_SHIP) {
		/* Alternative refly */
		if (shipext_Is_Mission_Success() && !shipext_Read_Temp_Pilot())
			shipext_Refly_Tour_Mission();
	}

	xerror_Set_Landru_Exit(scene);
}

/* ================================================================
 * Title label callbacks
 * ================================================================ */

// FUNCTION: TIE95 0x70440
// FUNCTION: TIE98 0x415C60
static void user_Title(Actor* actor, int32_t time) {
	(void)time;
	if (actor->var1 == 1) {
		if (!xactor_Is_Actor_Visible(actor))
			xactor_Show_Actor(actor);
		actor->var1 = 0;
	} else {
		if (xactor_Is_Actor_Visible(actor))
			xactor_Hide_Actor(actor);
	}
}

// FUNCTION: TIE95 0x70494
// FUNCTION: TIE98 0x415CB0
static int16_t draw_Title(Actor* actor, Rect* bounds, Rect* clip, int16_t xoff, int16_t yoff,
						  int16_t refresh) {
	int16_t offx, offy;
	Rect r;
	char label[32];
	TIEText text_id;

	if (!refresh)
		return 0;

	xactdelt_Draw_Delta_Actor(actor, bounds, clip, xoff, yoff, refresh);

	xactor_Get_Actor_Offset(actor, &offx, &offy);

	xrect_Set_Rect(&r, offx, offy, actor->w + offx, actor->h + offy);

	switch (actor->var2) {
		case 0:
			text_id = shipext_Is_Tour_Battle_End() ? txtBriefMainMenu : txtDebriefBrief;
			strcpy(label, textext_Get_Text(text_id));
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
		default:
			label[0] = 0;
			break;
	}

	/* Drop shadow */
	xrect_Offset_Rect(&r, 1, 1);
	xfont_Print_Centered_Text(label, &r, 16, active_spec->title_font);
	xrect_Offset_Rect(&r, -1, -1);
	xfont_Print_Centered_Text(label, &r, 15, active_spec->title_font);
	return 1;
}

/* ================================================================
 * Door callback
 * ================================================================ */

// FUNCTION: TIE95 0x70598
// FUNCTION: TIE98 0x415DE0
static void user_Door(Actor* actor, int32_t time) {
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
}

/* ================================================================
 * Officer/priest character animation
 * ================================================================ */

// FUNCTION: TIE95 0x7063C
// FUNCTION: TIE98 0x415E70
static void user_Officer(Actor* actor, int32_t time) {
	int16_t char_id, anim_state, zplane;

	if (!time) {
		actor->var2 = 0;
		actor->var1 = 0;
	}

	char_id = actor->id;

	switch (char_id) {
		case 0: {                      /* Officer facing */
			int16_t target_widget = 1; /* officer widget */
			if (title_actor->var1 && title_actor->var2 == target_widget) {
				if (actor->var1 < 10)
					actor->var1++;
			} else {
				if (actor->var1 > 0)
					actor->var1--;
			}

			if (actor->var1 <= 5)
				anim_state = 0;
			else
				anim_state = actor->var1 / 2 - 2;

			zplane = anim_state ? 15 : 30;
			xactor_Set_Actor_ZPlane(actor, zplane);
			xactor_Set_Actor_State(actor, anim_state, 0);
			break;
		}

		case 1: { /* Officer side */
			if (title_actor->var1 && title_actor->var2 == 1) {
				if (actor->var1 < 10)
					actor->var1++;
			} else {
				if (actor->var1 > 0)
					actor->var1--;
			}

			if (actor->var1 <= 5)
				anim_state = 0;
			else
				anim_state = actor->var1 / 2 - 2;

			if (anim_state > active_spec->officer_side_max_state)
				anim_state = active_spec->officer_side_max_state;
			xactor_Set_Actor_State(actor, anim_state, 0);
			break;
		}

		case 2: { /* Priest */
			if (title_actor->var1 && title_actor->var2 == 1) {
				if (actor->var1 < 10)
					actor->var1++;
			} else {
				if (actor->var1 > 0)
					actor->var1--;
			}

			if (actor->var1 >= 4)
				anim_state = 3;
			else
				anim_state = actor->var1;

			xactor_Set_Actor_State(actor, anim_state, 0);
			break;
		}

		case 3: { /* Alternate (priest variant) */
			if (title_actor->var1 && title_actor->var2 == 2) {
				if (actor->var1 < 1)
					actor->var1++;
			} else {
				if (actor->var1 > 0)
					actor->var1--;
			}

			xactor_Set_Actor_State(actor, actor->var1, 0);
			break;
		}

		default:
			break;
	}
}

/* ================================================================
 * Entry point
 * ================================================================ */

ResFile* debrief_OpenScene(SceneHeadStruct* scene_head, bool svga) {
	Rect frame;
	ResFile* resource;
	int16_t mouse_x, mouse_y;
	const int16_t* bounds;

	active_spec = &debrief_specs[svga ? 1 : 0];
	if (active_spec->surface_set == LANDRU_SURFACE_SVGA) {
		(void)xsurface_Select_Surface_Set(active_spec->surface_set);
		xview_Init_View(xview_Get_Current_View());
	}

	/* Position mouse based on outcome and officer type */
	if (shipext_Is_Mission_Success()) {
		if (shellext_Get_Last_Scene() != SCENE_TALK_DEBRIEF_OFFICER || shipext_Get_Mission_Officer() == 1) {
			mouse_x = active_spec->success_mouse_x;
			mouse_y = active_spec->success_mouse_y;
		} else {
			mouse_x = active_spec->talk_mouse_x;
			mouse_y = active_spec->talk_mouse_y;
		}
	} else {
		mouse_x = active_spec->failure_mouse_x;
		mouse_y = active_spec->failure_mouse_y;
	}
	xio_Set_Mouse_Position(mouse_x, mouse_y);

	/* Load resources */
	resource = shellext_Open_Empire_Resource("debrief.lfd");
	xrect_Set_Rect(&frame, 0, 0, active_spec->width, active_spec->height);

	debrief_film = xfilm_Res_Callback_Film("debrief", &frame, 0, 0, 0, film_Callback);
#ifdef TIE_MODERN
	TieSnapshotBuilder_SetActiveFilm("DEBRIEF", "debrief");
#endif
	xfilm_Set_Film_Def_Palette(debrief_film, scene_head->def_palette);

	/* Create XINPUT widgets */
	parent = xinput_Alloc_Input(NULL, &frame, 0, 0);

	/* Brief door (id=0) */
	bounds = active_spec->input_bounds[0];
	xrect_Set_Rect(&frame, bounds[0], bounds[1], bounds[2], bounds[3]);
	brief_input = xinput_Alloc_Input(parent, &frame, 0, 0);
	xinpattr_Set_Input_Update_Function(brief_input, iupdate_Debrief);
	xinpattr_Set_Input_User_Function(brief_input, iuser_Debrief);
	brief_input->mouseUsage = allInput;
	brief_input->id = 0;

	/* Officer door (id=1) — skip if priest only */
	if (shipext_Get_Mission_Officer() != 2) {
		bounds = active_spec->input_bounds[1];
		xrect_Set_Rect(&frame, bounds[0], bounds[1], bounds[2], bounds[3]);
		officer = xinput_Alloc_Input(parent, &frame, 0, 0);
		xinpattr_Set_Input_Update_Function(officer, iupdate_Debrief);
		xinpattr_Set_Input_User_Function(officer, iuser_Debrief);
		officer->mouseUsage = allInput;
		officer->id = 1;
	}

	/* Priest door (id=2) — skip if officer only */
	if (shipext_Get_Mission_Officer() != 1) {
		bounds = active_spec->input_bounds[2];
		xrect_Set_Rect(&frame, bounds[0], bounds[1], bounds[2], bounds[3]);
		priest = xinput_Alloc_Input(parent, &frame, 0, 0);
		xinpattr_Set_Input_Update_Function(priest, iupdate_Debrief);
		xinpattr_Set_Input_User_Function(priest, iuser_Debrief);
		priest->mouseUsage = allInput;
		priest->id = 2;
	}

	/* Fly-again area (id=3) */
	bounds = active_spec->input_bounds[3];
	xrect_Set_Rect(&frame, bounds[0], bounds[1], bounds[2], bounds[3]);
	flyagain = xinput_Alloc_Input(parent, &frame, 0, 0);
	xinpattr_Set_Input_Update_Function(flyagain, iupdate_Debrief);
	xinpattr_Set_Input_User_Function(flyagain, iuser_Debrief);
	flyagain->mouseUsage = allInput;
	flyagain->id = 3;

	xview_Set_View_Update_Function(end_View);
	xviewadd_Clear_View();
	xview_Disable_All_View_Erase();
	return resource;
}

void debrief_CloseScene(ResFile* resource, bool svga) {
	xview_Enable_All_View_Erase();
	xview_Clear_View_Update_Function();

	if (xcursor_Is_Cursor_Visible())
		xcursor_Hide_Cursor();

	xres_Close_Resource(resource);
	if (svga)
		(void)xsurface_Select_Surface_Set(LANDRU_SURFACE_VGA);
}
