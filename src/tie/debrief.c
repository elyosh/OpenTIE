/*
 * DEBRIEF.C — Mission debrief screen.
 *
 * Film-driven debrief room with officer/priest characters, animated
 * doors, and exit routing based on mission outcome. Characters lean
 * forward when the mouse hovers their door area.
 *
 * 9 functions. Recovered from the TIE95 and TIE98 executables.
 */

#include <stdlib.h>
#include <string.h>

#include "tie/debrief.h"
#include "tie/shellext.h"
#include "tie/shipext.h"
#include "tie/soundext.h"
#include "tie/textext.h"
#include "tie/tie.h"
#include "tie_runtime/runtime/profile.h"
#include "tie_runtime/snapshot/snapshot.h"
#include "tie_runtime/snapshot/snapshot_internal.h"
#include <landru/task.h>

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
        .surface_set = LANDRU_SURFACE_VGA,
        .actor_variants = DEBRIEF_ACTORS_VGA,
        .width = 320, .height = 200,
        .success_mouse_x = 180, .success_mouse_y = 100,
        .talk_mouse_x = 280, .talk_mouse_y = 120,
        .failure_mouse_x = 74, .failure_mouse_y = 100,
        .input_bounds = {
            {133, 56, 193, 107},
            {85, 35, 133, 150},
            {248, 51, 290, 128},
            {0, 0, 70, 200},
        },
        .title_font = 0,
        .officer_side_max_state = 2,
        .title_variants = true,
    },
    {
        .surface_set = LANDRU_SURFACE_SVGA,
        .actor_variants = DEBRIEF_ACTORS_SVGA,
        .width = 640, .height = 480,
        .success_mouse_x = 360, .success_mouse_y = 200,
        .talk_mouse_x = 540, .talk_mouse_y = 240,
        .failure_mouse_x = 108, .failure_mouse_y = 200,
        .input_bounds = {
            {298, 129, 420, 288},
            {226, 102, 296, 322},
            {500, 134, 572, 316},
            {56, 26, 145, 345},
        },
        .title_font = 2,
        .officer_side_max_state = 3,
        .title_variants = false,
    },
};

static const DebriefSpec* active_spec;

/* ---- Static globals ---- */

static Actor* door_actors[2]; /* door[0]=brief door, door[1]=fly-again door */
static Input* priest;         /* priest widget (id=2) */
static Input* flyagain;       /* fly-again widget (id=3) */
static Film* debrief_film;
// GLOBAL: TIE 0xF5968
static Actor* title_actor;
static Input* parent;
static Input* brief_input;
static Input* officer; /* officer widget (id=1) */

/* ---- Forward declarations ---- */

static void user_Title(Actor* actor, int32_t time);
static int draw_Title(Actor* actor, Rect* bounds, Rect* clip, int16_t xoff, int16_t yoff, int16_t refresh);
static void user_Door(Actor* actor, int32_t time);
static void user_Officer(Actor* actor, int32_t time);

/* ================================================================
 * View update callback
 * ================================================================ */

// FUNCTION: TIE95 0x700E4; TIE98 0x415920
static void end_View(int32_t frame_num) {
	if (frame_num)
		return;
	if (!lcursor_Is_Cursor_Visible())
		lcursor_Show_Cursor();
}

/* ================================================================
 * Film callback — register actors by var1
 * ================================================================ */

// FUNCTION: TIE95 0x7012C; TIE98 0x415940
static int16_t film_Callback(Film* film, FilmObject* film_object) {
	if (film_object->id != 3) /* type_code: 3 = actor */
		return 0;

	lfilm_Rewind_Actor_Film(film, film_object, (void*)((char*)film_object + sizeof(FilmObject)));
	Actor* actor = (Actor*)film_object->object;

	switch (actor->var1) {
		case 1: /* Background */
			lactor_Non_Refreshable_Actor(actor);
			return 0;

		case 2: /* Door actor — stored by var2 index */
			lactor_Set_Actor_User_Function(actor, user_Door);
			door_actors[actor->var2] = actor;
			return 0;

		case 3: /* Officer/priest character */
			switch (actor->var2) {
				case 0:
				case 1:
				case 2:
					if (shipext_Get_Mission_Officer() == 2)
						return 1; /* hide if priest-only */
					lactor_Set_Actor_User_Function(actor, user_Officer);
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
					lactor_Set_Actor_User_Function(actor, user_Officer);
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
			lactor_Set_Actor_User_Function(actor, user_Title);
			lactor_Set_Actor_Draw_Function(actor, draw_Title);
			title_actor = actor;
			return 0;

		default:
			return 0;
	}
}

/* ================================================================
 * XINPUT callbacks
 * ================================================================ */

// FUNCTION: TIE95 0x702AC; TIE98 0x415AA0
static int16_t iupdate_Debrief(Input* input, Rect* bounds, Rect* clip, int16_t key, uint8_t left,
							   uint8_t right, int16_t mouse_x, int16_t mouse_y) {
	(void)bounds;
	(void)clip;
	(void)mouse_x;
	(void)mouse_y;

	if (key)
		return 0;

	/* Open the door actor for brief (id=0) and fly-again (id=3) */
	int16_t widget_id = input->id;
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

// FUNCTION: TIE95 0x703B4; TIE98 0x415B90
static void iuser_Debrief(Input* input, int32_t time) {
	(void)time;
	if (!input->var1)
		return; /* exit_pending */

	int16_t scene = input->var2; /* exit_code */

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

	lerror_Set_Landru_Exit(scene);
}

/* ================================================================
 * Title label callbacks
 * ================================================================ */

// FUNCTION: TIE95 0x70440; TIE98 0x415C60
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

// FUNCTION: TIE95 0x70494; TIE98 0x415CB0
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
	lrect_Offset_Rect(&r, 1, 1);
	lfont_Print_Centered_Text(label, &r, 16, active_spec->title_font);
	lrect_Offset_Rect(&r, -1, -1);
	lfont_Print_Centered_Text(label, &r, 15, active_spec->title_font);
	return 1;
}

/* ================================================================
 * Door callback
 * ================================================================ */

// FUNCTION: TIE95 0x70598; TIE98 0x415DE0
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
			lactor_Set_Actor_State(actor, actor->state + 1, 0);
		actor->var1 = 0;
	} else {
		/* Closing */
		if (actor->state > 0) {
			lactor_Set_Actor_State(actor, actor->state - 1, 0);
			if (!actor->state)
				soundext_Play_SFX(sfxSmallDoorShut, 80);
		}
	}
}

/* ================================================================
 * Officer/priest character animation
 * ================================================================ */

// FUNCTION: TIE95 0x7063C; TIE98 0x415E70
static void user_Officer(Actor* actor, int32_t time) {
	if (!time) {
		actor->var2 = 0;
		actor->var1 = 0;
	}

	int16_t char_id = actor->id;

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

			int16_t anim_state;
			if (actor->var1 <= 5)
				anim_state = 0;
			else
				anim_state = actor->var1 / 2 - 2;

			int16_t zplane = anim_state ? 15 : 30;
			lactor_Set_Actor_ZPlane(actor, zplane);
			lactor_Set_Actor_State(actor, anim_state, 0);
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

			int16_t anim_state;
			if (actor->var1 <= 5)
				anim_state = 0;
			else
				anim_state = actor->var1 / 2 - 2;

			if (anim_state > active_spec->officer_side_max_state)
				anim_state = active_spec->officer_side_max_state;
			lactor_Set_Actor_State(actor, anim_state, 0);
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

			int16_t anim_state;
			if (actor->var1 >= 4)
				anim_state = 3;
			else
				anim_state = actor->var1;

			lactor_Set_Actor_State(actor, anim_state, 0);
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

			lactor_Set_Actor_State(actor, actor->var1, 0);
			break;
		}

		default:
			break;
	}
}

/* ================================================================
 * Entry point
 * ================================================================ */

typedef enum {
	DEBRIEF_PHASE_BEGIN = 0,
	DEBRIEF_PHASE_CLEANUP = 1,
} DebriefPhase;

typedef struct DebriefTask {
	SceneHeadStruct* scene_head;
	ResFile* res_file;
	DebriefPhase phase;
	const DebriefSpec* spec;
} DebriefTask;

/* PORT: asynchronous adaptation of TIE95 DEBRIEF_Debrief (0x6FE60)
 * and TIE98 DEBRIEF_Debrief (0x4155F0). */
static LandruTaskStepResult debrief_task_step(void* self) {
	DebriefTask* t = (DebriefTask*)self;

	if (t->phase == DEBRIEF_PHASE_BEGIN) {
		Rect frame;
		const int16_t* bounds;

		active_spec = t->spec;
		if (active_spec->surface_set == LANDRU_SURFACE_SVGA) {
			(void)lsurface_Select_Surface_Set(active_spec->surface_set);
			lview_Init_View(lview_Get_Current_View());
		}

		/* Position mouse based on outcome and officer type */
		int16_t mouse_x, mouse_y;
		if (shipext_Is_Mission_Success()) {
			if (shellext_Get_Last_Scene() != SCENE_TALK_DEBRIEF_OFFICER ||
				shipext_Get_Mission_Officer() == 1) {
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
		lio_Set_Mouse_Position(mouse_x, mouse_y);

		/* Load resources */
		t->res_file = shellext_Open_Empire_Resource("debrief.lfd");
		lrect_Set_Rect(&frame, 0, 0, active_spec->width, active_spec->height);

		debrief_film = lfilm_Res_Callback_Film("debrief", &frame, 0, 0, 0, film_Callback);
		TieSnapshotBuilder_SetActiveFilm("DEBRIEF", "debrief");
		lfilm_Set_Film_Def_Palette(debrief_film, t->scene_head->def_palette);

		/* Create XINPUT widgets */
		parent = linput_Alloc_Input(NULL, &frame, 0, 0);

		/* Brief door (id=0) */
		bounds = active_spec->input_bounds[0];
		lrect_Set_Rect(&frame, bounds[0], bounds[1], bounds[2], bounds[3]);
		brief_input = linput_Alloc_Input(parent, &frame, 0, 0);
		linpattr_Set_Input_Update_Function(brief_input, iupdate_Debrief);
		linpattr_Set_Input_User_Function(brief_input, iuser_Debrief);
		brief_input->mouseUsage = allInput;
		brief_input->id = 0;

		/* Officer door (id=1) — skip if priest only */
		if (shipext_Get_Mission_Officer() != 2) {
			bounds = active_spec->input_bounds[1];
			lrect_Set_Rect(&frame, bounds[0], bounds[1], bounds[2], bounds[3]);
			officer = linput_Alloc_Input(parent, &frame, 0, 0);
			linpattr_Set_Input_Update_Function(officer, iupdate_Debrief);
			linpattr_Set_Input_User_Function(officer, iuser_Debrief);
			officer->mouseUsage = allInput;
			officer->id = 1;
		}

		/* Priest door (id=2) — skip if officer only */
		if (shipext_Get_Mission_Officer() != 1) {
			bounds = active_spec->input_bounds[2];
			lrect_Set_Rect(&frame, bounds[0], bounds[1], bounds[2], bounds[3]);
			priest = linput_Alloc_Input(parent, &frame, 0, 0);
			linpattr_Set_Input_Update_Function(priest, iupdate_Debrief);
			linpattr_Set_Input_User_Function(priest, iuser_Debrief);
			priest->mouseUsage = allInput;
			priest->id = 2;
		}

		/* Fly-again area (id=3) */
		bounds = active_spec->input_bounds[3];
		lrect_Set_Rect(&frame, bounds[0], bounds[1], bounds[2], bounds[3]);
		flyagain = linput_Alloc_Input(parent, &frame, 0, 0);
		linpattr_Set_Input_Update_Function(flyagain, iupdate_Debrief);
		linpattr_Set_Input_User_Function(flyagain, iuser_Debrief);
		flyagain->mouseUsage = allInput;
		flyagain->id = 3;

		/* Push the modal view task */
		lview_Set_View_Update_Function(end_View);
		lviewadd_Clear_View();
		lview_Disable_All_View_Erase();

		lviewadd_Push_Handle_View_Task();

		t->phase = DEBRIEF_PHASE_CLEANUP;
		return LANDRU_TASK_STEP_CONTINUE;
	}

	/* CLEANUP */
	lview_Enable_All_View_Erase();
	lview_Clear_View_Update_Function();

	if (lcursor_Is_Cursor_Visible())
		lcursor_Hide_Cursor();

	lres_Close_Resource(t->res_file);
	if (t->spec->surface_set == LANDRU_SURFACE_SVGA)
		(void)lsurface_Select_Surface_Set(LANDRU_SURFACE_VGA);
	return LANDRU_TASK_STEP_DONE;
}

static const LandruTaskVtable debrief_task_vt = {
	.step = debrief_task_step,
};

void debrief_Push_Debrief_Task(SceneHeadStruct* scene_head) {
	DebriefTask* t = (DebriefTask*)landru_task_push(&debrief_task_vt);
	if (!t)
		return;
	t->scene_head = scene_head;
	t->res_file = NULL;
	t->phase = DEBRIEF_PHASE_BEGIN;
	t->spec = &debrief_specs[TieProfile_FrontendId() == TIE_FRONTEND_PROFILE_TIE98 ? 1 : 0];
}
