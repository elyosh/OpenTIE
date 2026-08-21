#include <stdlib.h>
#include <string.h>

#include "tie/mainmenu.h"
#include "tie/shellext.h"
#include "tie/shipext.h"
#include "tie/soundext.h"
#include "tie/textext.h"
#include "tie_runtime/diagnostics/diagnostics.h"
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
#include "landru/fourcc.h"
#include "landru/inpattr.h"
#include "landru/input.h"
#include "landru/io.h"
#include "landru/rect.h"
#include "landru/res.h"
#include "landru/surface.h"
#include "landru/view.h"
#include "landru/viewadd.h"

/* ---- Static data ---- */

/* Per-door SFX volume (indexed by door actor id 0..7) */
static const int16_t door_volume[8] = { 92, 64, 64, 64, 72, 80, 68, 68 };

typedef struct MainMenuSpec {
	LandruSurfaceSet surface_set;
	const char* archive;
	const char* film;
	const char* snapshot_lfd;
	const char* background;
	const char* door_names[8];
	int16_t width, height;
	int16_t mouse_x, mouse_y;
	int16_t button_bounds[8][4];
	int16_t exit_scene[8];
	TIEText title_text[8];
	int16_t title_font;
	int16_t outcome_input_id;
} MainMenuSpec;

static const MainMenuSpec mainmenu_specs[] = {
    {
        .surface_set = LANDRU_SURFACE_VGA,
        .archive = "mainmenu.lfd", .film = "mainmenu",
        .snapshot_lfd = "MAINMENU", .background = "main-1",
        .door_names = {"m-hang-d", "m-door-1", "m-door-2", "m-door-3",
                       "m-door-4", "m-door-5", "m-door-6", "m-door-7"},
        .width = 320, .height = 200, .mouse_x = 190, .mouse_y = 100,
        .button_bounds = {
            {14, 28, 170, 64}, {0}, {82, 64, 128, 90},
            {220, 64, 260, 90}, {272, 68, 304, 94},
            {0, 110, 34, 146}, {44, 96, 82, 126}, {130, 86, 168, 106},
        },
        .exit_scene = {SCENE_BRIEF, 0, SCENE_TOUR_DESK, SCENE_BLUEPRINT,
                       SCENE_FILM_VIEWER, SCENE_EXIT, SCENE_TRAIN_TRANSITION,
                       SCENE_COMBAT_TRANSITION},
        .title_text = {0, txtMainCustom, 0, txtMainTrain, txtMainCombat,
                       txtMainRegister, txtMainTech, txtMainFilm},
        .title_font = 0, .outcome_input_id = 4,
    },
    {
        .surface_set = LANDRU_SURFACE_SVGA,
        .archive = "mm640.lfd", .film = "main_00",
        .snapshot_lfd = "MM640", .background = NULL,
        .door_names = {"hr_mhngr", "hr_md3_1", "hr_md2_1", "hr_md1_2",
                       "hr_md1_3", "hr_md1_1", "hr_md2_2", "hr_md2_3"},
        .width = 640, .height = 480, .mouse_x = 387, .mouse_y = 387,
        .button_bounds = {
            {19, 66, 340, 142}, {0}, {172, 163, 248, 210},
            {104, 237, 154, 299}, {266, 212, 331, 260},
            {0, 268, 55, 357}, {433, 159, 512, 215}, {549, 168, 604, 226},
        },
        .exit_scene = {SCENE_BRIEF, 0, SCENE_TOUR_DESK,
                       SCENE_TRAIN_TRANSITION, SCENE_COMBAT_TRANSITION,
                       SCENE_EXIT, SCENE_BLUEPRINT, SCENE_FILM_VIEWER},
        .title_text = {0, txtMainCustom, 0, txtMainTech, txtMainFilm,
                       txtMainRegister, txtMainTrain, txtMainCombat},
        .title_font = 2, .outcome_input_id = 7,
    },
};

static const MainMenuSpec* active_spec;

/* ---- Static globals ---- */

static Actor* door[8]; /* 8 door animation actors */
static Film* mainmenu_film;
// GLOBAL: TIE 0xF5968
static Actor* title_actor; /* title text overlay delta actor */
static Input* parent;      /* root XINPUT for menu buttons */
static Input* tour_input;  /* Tour Battle (id=0, conditional) */
static Input* menu_input[8];
static Actor* mainmenu_actor; /* main background delta actor */

/* ---- Forward declarations ---- */

static void end_View(int32_t frame_num);
static int16_t iupdate_MainMenu(Input* input, Rect* bounds, Rect* clip, int16_t key, uint8_t left,
								uint8_t right, int16_t mouse_x, int16_t mouse_y);
static void iuser_MainMenu(Input* input, int32_t time);
static void user_Title(Actor* actor, int32_t time);
static int draw_Title(Actor* actor, Rect* bounds, Rect* clip, int16_t xoff, int16_t yoff, int16_t refresh);
static void user_Door(Actor* actor, int32_t time);

/* ================================================================
 * View update callback
 * ================================================================ */

/* On frame 0, shows the cursor if hidden. Shared epilogue with Main_Menu
 * in the binary (JUMPOUT to retn). */
static void end_View(int32_t frame_num) {
	if (frame_num)
		return;
	if (!lcursor_Is_Cursor_Visible())
		lcursor_Show_Cursor();
}

/* ================================================================
 * XINPUT callbacks
 * ================================================================ */

/* iupdate: hover highlights door + title, click dispatches scene exit. */
static int16_t iupdate_MainMenu(Input* input, Rect* bounds, Rect* clip, int16_t key, uint8_t left,
								uint8_t right, int16_t mouse_x, int16_t mouse_y) {
	(void)bounds;
	(void)clip;
	(void)mouse_x;
	(void)mouse_y;

	if (key)
		return 0;

	/* Highlight the door for this menu button */
	door[input->id]->var1 = 1;

	/* Show the title overlay with this button's text id */
	title_actor->var1 = 1;
	title_actor->var2 = input->id;

	/* Check for mouse click (button state 3 = released) */
	if (left != 3 && right != 3)
		return 1;

	switch (input->id) {
		case 0: { /* Tour Battle */
			int16_t ok = shipext_Set_Tour_Battle();
			if (!ok) {
				linpattr_Hide_Input(input);
				return 1;
			}
			input->var2 = SCENE_BRIEF; /* exit_code */
			input->var1 = 1;           /* exit_pending */
			break;
		}
		case 2:
		case 3:
		case 4:
		case 5:
		case 6:
		case 7:
			input->var2 = active_spec->exit_scene[input->id];
			input->var1 = 1;
			if (input->id == active_spec->outcome_input_id)
				shipext_Set_Mission_Outcome(16);
			break;
		default:
			break;
	}
	return 1;
}

/* iuser: when exit_pending, triggers the scene transition. */
static void iuser_MainMenu(Input* input, int32_t time) {
	(void)time;
	if (!input->var1)
		return; /* exit_pending */

	if (input->var2 == 180) { /* exit_code == Tour Battle */
		char name[68];
		shipext_Get_Battle_Mission_Name(name);
		shipext_Set_Mission_Name(name);
	}
	lerror_Set_Landru_Exit(input->var2); /* exit_code */
}

/* ================================================================
 * Actor callbacks
 * ================================================================ */

/* Title overlay: show on hover frame, hide otherwise. */
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

/* Title overlay draw: render the delta actor + centered text label. */
static int draw_Title(Actor* actor, Rect* bounds, Rect* clip, int16_t xoff, int16_t yoff, int16_t refresh) {
	if (!refresh)
		return 0;

	lactdelt_Draw_Delta_Actor(actor, bounds, clip, xoff, yoff, refresh);

	int16_t offx, offy;
	lactor_Get_Actor_Offset(actor, &offx, &offy);

	Rect r;
	lrect_Set_Rect(&r, offx, offy, actor->w + offx, actor->h + offy);

	char label[32];
	switch (actor->var2) {
		case 0: /* Continue Battle N */
			textext_Copy_Text(label, txtMainContBattle);
			strcat(label, " ");
			{
				char num[16];
				textext_Copy_Text(num, (TIEText)(txtMainOne + pilot_record.cur_battle));
				strcat(label, num);
			}
			break;
		case 1:
			strcpy(label, textext_Get_Text(active_spec->title_text[1]));
			break;
		case 2: /* New/Change/View TOD */
			if (shipext_Find_Battle()) {
				if (pilot_record.battle_status[pilot_record.cur_battle] == 1)
					strcpy(label, textext_Get_Text(txtMainChangeBattle));
				else
					strcpy(label, textext_Get_Text(txtMainNewBattle));
			} else {
				strcpy(label, textext_Get_Text(txtMainViewTOD));
			}
			break;
		case 3:
		case 4:
		case 5:
		case 6:
		case 7:
			strcpy(label, textext_Get_Text(active_spec->title_text[actor->var2]));
			break;
		default:
			label[0] = 0;
			break;
	}

	/* Drop shadow: dark color at (1,1) offset, then bright at (0,0) */
	lrect_Offset_Rect(&r, 1, 1);
	lfont_Print_Centered_Text(label, &r, 16, active_spec->title_font);
	lrect_Offset_Rect(&r, -1, -1);
	lfont_Print_Centered_Text(label, &r, 15, active_spec->title_font);
	return 1;
}

/* Door animation: open when var1 set (hover), close when cleared. */
static void user_Door(Actor* actor, int32_t time) {
	(void)time;
	if (actor->var1) {
		/* Opening */
		if (!actor->state)
			soundext_Play_SFX(sfxSmallDoorOpen, door_volume[actor->id]);
		if (actor->state < actor->arraySize - 1)
			lactor_Set_Actor_State(actor, actor->state + 1, 0);
		actor->var1 = 0;
	} else {
		/* Closing */
		if (actor->state > 0) {
			lactor_Set_Actor_State(actor, actor->state - 1, 0);
			if (!actor->state)
				soundext_Play_SFX(sfxSmallDoorShut, door_volume[actor->id]);
		}
	}
}

/* ================================================================
 * Entry point
 * ================================================================ */

typedef enum {
	MAINMENU_PHASE_BEGIN = 0,
	MAINMENU_PHASE_CLEANUP = 1,
} MainMenuPhase;

typedef struct MainMenuTask {
	SceneHeadStruct* scene_head;
	ResFile* res_file;
	MainMenuPhase phase;
	const MainMenuSpec* spec;
} MainMenuTask;

static LandruTaskStepResult mainmenu_setup_failed(MainMenuTask* t, const char* resource) {
	TieDiagnostics_Log(TIE_LOG_ERROR, "[MAINMENU] missing frontend resource: %s\n",
					   resource ? resource : "unknown");
	lerror_Set_Landru_Error(6);
	t->phase = MAINMENU_PHASE_CLEANUP;
	return LANDRU_TASK_STEP_CONTINUE;
}

static LandruTaskStepResult mainmenu_task_step(void* self) {
	MainMenuTask* t = (MainMenuTask*)self;

	if (t->phase == MAINMENU_PHASE_BEGIN) {
		Rect frame;
		int16_t i;

		active_spec = t->spec;
		if (!lsurface_Select_Surface_Set(active_spec->surface_set))
			return mainmenu_setup_failed(t, "surface set");
		lview_Init_View(lview_Get_Current_View());
		lio_Set_Mouse_Position(active_spec->mouse_x, active_spec->mouse_y);

		t->res_file = shellext_Open_Empire_Resource(active_spec->archive);
		if (!t->res_file)
			return mainmenu_setup_failed(t, active_spec->archive);
		lrect_Set_Rect(&frame, 0, 0, active_spec->width, active_spec->height);

		/* Load main menu film. Tag the snapshot with the (lfd, film)
		 * tuple so the cutscene compositor can resolve a remaster
		 * bundle for this screen. Default INCREMENTAL redraw model is
		 * correct here (dirty-rect refresh, persistent RT) — same as
		 * register. The tag is auto-cleared at the next scene
		 * transition by shell_run_scene_dispatch. */
		mainmenu_film = lfilm_Res_Film(active_spec->film, &frame, 0, 0, 0);
		if (!mainmenu_film)
			return mainmenu_setup_failed(t, active_spec->film);
		TieSnapshotBuilder_SetActiveFilm(active_spec->snapshot_lfd, active_spec->film);
		lfilm_Set_Film_Def_Palette(mainmenu_film, t->scene_head->def_palette);

		/* Find the background actor */
		mainmenu_actor =
			active_spec->background ? lactor_Find_Actor(FOURCC_DELT, active_spec->background) : NULL;
		if (active_spec->background && !mainmenu_actor)
			return mainmenu_setup_failed(t, active_spec->background);
		if (mainmenu_actor)
			lactor_Non_Refreshable_Actor(mainmenu_actor);

		/* Find the 8 door actors */
		for (i = 0; i < 8; i++) {
			door[i] = lactor_Find_Actor(FOURCC_ANIM, active_spec->door_names[i]);
			if (!door[i])
				return mainmenu_setup_failed(t, active_spec->door_names[i]);
			lactor_Set_Actor_User_Function(door[i], (lactorCallback)user_Door);
			door[i]->id = i;
		}

		/* Create title text overlay */
		title_actor = lactdelt_Res_Delta_Actor("title", &frame, 0, 0, 0);
		if (!title_actor)
			return mainmenu_setup_failed(t, "title");
		lactor_Set_Actor_User_Function(title_actor, (lactorCallback)user_Title);
		lactor_Set_Actor_Draw_Function(title_actor, (lactorDrawFunc)draw_Title);

		/* Create XINPUT button regions */
		parent = linput_Alloc_Input(NULL, &frame, 0, 0);
		if (!parent)
			return mainmenu_setup_failed(t, "main-menu input root");

		/* Tour Battle button (only if current battle is active) */
		if (pilot_record.battle_status[pilot_record.cur_battle] == 1) {
			const int16_t* b = active_spec->button_bounds[0];
			lrect_Set_Rect(&frame, b[0], b[1], b[2], b[3]);
			tour_input = linput_Alloc_Input(parent, &frame, 0, 0);
			if (!tour_input)
				return mainmenu_setup_failed(t, "tour input");
			linpattr_Set_Input_Update_Function(tour_input, iupdate_MainMenu);
			linpattr_Set_Input_User_Function(tour_input, iuser_MainMenu);
			tour_input->mouseUsage = allInput;
			tour_input->id = 0;
		}

		for (i = 2; i < 8; i++) {
			const int16_t* b = active_spec->button_bounds[i];
			lrect_Set_Rect(&frame, b[0], b[1], b[2], b[3]);
			menu_input[i] = linput_Alloc_Input(parent, &frame, 0, 0);
			if (!menu_input[i])
				return mainmenu_setup_failed(t, "main-menu input");
			linpattr_Set_Input_Update_Function(menu_input[i], iupdate_MainMenu);
			linpattr_Set_Input_User_Function(menu_input[i], iuser_MainMenu);
			menu_input[i]->mouseUsage = allInput;
			menu_input[i]->id = i;
		}

		/* Push the modal view task */
		lview_Set_View_Update_Function(end_View);
		lviewadd_Clear_View();
		lview_Disable_All_View_Erase();

		lviewadd_Push_Handle_View_Task();

		t->phase = MAINMENU_PHASE_CLEANUP;
		return LANDRU_TASK_STEP_CONTINUE;
	}

	/* CLEANUP */
	lview_Enable_All_View_Erase();
	lview_Clear_View_Update_Function();

	if (lcursor_Is_Cursor_Visible())
		lcursor_Hide_Cursor();

	if (t->res_file)
		lres_Close_Resource(t->res_file);
	if (t->spec->surface_set == LANDRU_SURFACE_SVGA)
		(void)lsurface_Select_Surface_Set(LANDRU_SURFACE_VGA);

	return LANDRU_TASK_STEP_DONE;
}

static const LandruTaskVtable mainmenu_task_vt = {
	.step = mainmenu_task_step,
};

void mainmenu_Push_Main_Menu_Task(SceneHeadStruct* scene_head) {
	MainMenuTask* t = (MainMenuTask*)landru_task_push(&mainmenu_task_vt);
	if (!t)
		return;
	t->scene_head = scene_head;
	t->res_file = NULL;
	t->phase = MAINMENU_PHASE_BEGIN;
	t->spec = &mainmenu_specs[TieProfile_FrontendId() == TIE_FRONTEND_PROFILE_TIE98 ? 1 : 0];
}
