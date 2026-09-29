/*
 * BRIEF.C — Mission briefing scene.
 *
 * Film-driven briefing room with officer/priest characters, animated
 * doors, tactical map display (via PLAYER polygon projection), and
 * mission launch routing. Includes a "pilot restored" notice dialog
 * shown when entering from the restore path (scene 179).
 *
 * 11 functions. Recovered from the TIE95 and TIE98 executables.
 */

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "tie/brief.h"
#include "tie/player.h"
#include "tie/shellext.h"
#include "tie/shipext.h"
#include "tie/soundext.h"
#include "tie/textext.h"
#include "tie/tie.h"
#include "tie_runtime/runtime/profile.h"
#include "tie_runtime/snapshot/snapshot.h"
#include "tie_runtime/snapshot/snapshot_internal.h"
#include "tie_runtime/snapshot/snapshot_map.h"
#include <landru/task.h>

#include "landru/actdelt.h"
#include "landru/actor.h"
#include "landru/btnpush.h"
#include "landru/cursor.h"
#include "landru/dialog.h"
#include "landru/dirty.h"
#include "landru/error.h"
#include "landru/film.h"
#include "landru/font.h"
#include "landru/inpattr.h"
#include "landru/input.h"
#include "landru/io.h"
#include "landru/rect.h"
#include "landru/res.h"
#include "landru/style.h"
#include "landru/surface.h"
#include "landru/vesa.h"
#include "landru/view.h"
#include "landru/viewadd.h"

/* ---- Edition data ---- */

typedef struct BriefSpec {
	LandruSurfaceSet surface_set;
	const char* archive;
	const char* snapshot_lfd;
	int16_t width, height;
	int16_t default_mouse_x, default_mouse_y;
	int16_t officer_mouse_x, officer_mouse_y;
	int16_t map_mouse_x, map_mouse_y;
	int16_t priest_mouse_x, priest_mouse_y;
	int16_t notice_mouse_x, notice_mouse_y;
	int16_t notice_return_mouse_x, notice_return_mouse_y;
	int16_t input_bounds[5][4];
	int16_t map_poly[8];
	int16_t title_font;
	int16_t notice_width, notice_height;
	int16_t notice_text_height;
	int16_t notice_font;
	bool film_actor_case_8;
} BriefSpec;

/* DATA: TIE95 BRIEF_Brief 0x72E20; TIE98 0x405FF0. */
static const BriefSpec brief_specs[] = {
    {
        .surface_set = LANDRU_SURFACE_VGA,
        .archive = "brief.lfd", .snapshot_lfd = "BRIEF",
        .width = 320, .height = 200,
        .default_mouse_x = 78, .default_mouse_y = 80,
        .officer_mouse_x = 218, .officer_mouse_y = 126,
        .map_mouse_x = 160, .map_mouse_y = 90,
        .priest_mouse_x = 282, .priest_mouse_y = 90,
        .notice_mouse_x = 190, .notice_mouse_y = 110,
        .notice_return_mouse_x = 218, .notice_return_mouse_y = 126,
        .input_bounds = {
            {84, 132, 122, 172},
            {190, 118, 248, 138},
            {28, 46, 128, 124},
            {140, 66, 174, 114},
            {242, 66, 314, 102},
        },
        .map_poly = {34, 50, 127, 58, 125, 106, 36, 120},
        .title_font = 0,
        .notice_width = 180, .notice_height = 40,
        .notice_text_height = 20,
        .notice_font = 0,
        .film_actor_case_8 = false,
    },
    {
        .surface_set = LANDRU_SURFACE_SVGA,
        .archive = "brief640.lfd", .snapshot_lfd = "BRIEF640",
        .width = 640, .height = 480,
        .default_mouse_x = 156, .default_mouse_y = 160,
        .officer_mouse_x = 416, .officer_mouse_y = 302,
        .map_mouse_x = 265, .map_mouse_y = 190,
        .priest_mouse_x = 564, .priest_mouse_y = 180,
        .notice_mouse_x = 330, .notice_mouse_y = 260,
        .notice_return_mouse_x = 416, .notice_return_mouse_y = 302,
        .input_bounds = {
            {123, 329, 184, 459},
            {385, 256, 448, 350},
            {50, 80, 230, 290},
            {230, 157, 274, 251},
            {498, 125, 604, 220},
        },
        .map_poly = {56, 83, 223, 93, 225, 244, 67, 277},
        .title_font = 2,
        .notice_width = 280, .notice_height = 60,
        .notice_text_height = 30,
        .notice_font = 2,
        .film_actor_case_8 = true,
    },
};

static const BriefSpec* active_spec;

/* ---- Static globals ---- */

// GLOBAL: TIE95 0xF6038; TIE98 0x50AA40
static Actor* door[2]; /* door[0]=mainmenu, door[1]=mission */
// GLOBAL: TIE95 0xF5FF8; TIE98 0x50A9F8
static char notice_str[64]; /* OK button label buffer */
// GLOBAL: TIE95 0xF6054; TIE98 0x50AA48
static Actor* title_actor;
// GLOBAL: TIE95 0xF6058; TIE98 0x50A9EC
static Input* parent;
// GLOBAL: TIE95 0xF6050; TIE98 0x50A9DC
static Input* mainmenu_input; /* id=0: main menu */
// GLOBAL: TIE95 0xF6040; TIE98 0x50A9E4
static Input* mission_input; /* id=1: enter mission */
// GLOBAL: TIE95 0xF6044; TIE98 0x50A9F0
static Input* officer_input; /* id=3: officer */
// GLOBAL: TIE95 0xF6048; TIE98 0x50A9E8
static Input* priest_input; /* id=4: priest */
// GLOBAL: TIE95 0xF604C; TIE98 0x50A9D8
static Input* map_input; /* id=2: map */
// GLOBAL: TIE95 0xF6060; TIE98 0x50A9E0
static Film* brief_film;

/* ---- Forward declarations ---- */

static void user_Title(Actor* actor, int32_t time);
static int16_t draw_Title(Actor* actor, Rect* bounds, Rect* clip, int16_t xoff, int16_t yoff,
						  int16_t refresh);
static void user_Door(Actor* actor, int32_t time);
static Input* Build_Notice(const char* text);
static void idraw_Notice(Input* input, Rect* r, Rect* clip, int16_t refresh);
static void iuser_Notice(Input* input, int32_t time);

/* ================================================================
 * View update callback
 * ================================================================ */

/* Show the cursor on the first view update. */
// FUNCTION: TIE95 0x7316C; TIE98 0x406520
static void end_View(int32_t frame_num) {
	if (frame_num)
		return;

	if (!xcursor_Is_Cursor_Visible())
		xcursor_Show_Cursor();
}

/* ================================================================
 * Film callback
 * ================================================================ */

// FUNCTION: TIE95 0x731E0; TIE98 0x406580
static int16_t film_Callback(Film* film, FilmObject* fo) {
	if (fo->id != 3)
		return 0; /* type_code: 3 = actor */

	xfilm_Rewind_Actor_Film(film, fo, (void*)((char*)fo + sizeof(FilmObject)));
	Actor* actor = (Actor*)fo->object;

	switch (actor->var1) {
		case 1: /* Hide if officer type is not 1 (officer-only actor) */
			return (shipext_Get_Mission_Officer() != 1) ? 0 : 1;

		case 2: /* Background — non-refreshable */
			xactor_Non_Refreshable_Actor(actor);
			return 0;

		case 3: /* Officer/priest filter by var2 */
			if (shipext_Get_Mission_Officer() == 1) {
				return actor->var2 ? 0 : 1;
			} else {
				return actor->var2 ? 1 : 0;
			}

		case 4: /* Door actor */
			xactor_Set_Actor_User_Function(actor, (xactorCallback)user_Door);
			door[actor->var2] = actor;
			actor->id = actor->var2;
			return 0;

		case 5: /* Hide if officer type is not 2 (priest-only actor) */
			return (shipext_Get_Mission_Officer() != 2) ? 0 : 1;

		case 6: /* Title label */
			title_actor = actor;
			xactor_Set_Actor_User_Function(actor, (xactorCallback)user_Title);
			xactor_Set_Actor_Draw_Function(actor, draw_Title);
			return 0;

		case 7: /* Returns 1 when officer != 2 (i.e. show for officer kind 1). */
			return shipext_Get_Mission_Officer() != 2;

		case 8: /* Additional TIE98 officer-room actor variant. */
			if (active_spec->film_actor_case_8)
				return shipext_Get_Mission_Officer() != 2;
			return 0;

		default:
			return 0;
	}
}

/* ================================================================
 * XINPUT callbacks
 * ================================================================ */

// FUNCTION: TIE95 0x7330C; TIE98 0x4066E0
static int16_t iupdate_Brief(Input* input, Rect* bounds, Rect* clip, int16_t key, uint8_t left, uint8_t right,
							 int16_t mouse_x, int16_t mouse_y) {
	(void)bounds;
	(void)clip;
	(void)mouse_x;
	(void)mouse_y;
	if (key)
		return 0;

	/* Open door for ids 0 (mainmenu) and 1 (mission) */
	if (input->id < 2)
		door[input->id]->var1 = 1;

	/* Show title label */
	title_actor->var1 = 1;
	title_actor->var2 = input->id;

	/* Check for click */
	if (left != 3 && right != 3)
		return 1;

	switch (input->id) {
		case 0: /* Main Menu */
			input->var2 = SCENE_MAIN_MENU;
			input->var1 = 1;
			break;
		case 1: /* Enter Mission */
			input->var1 = 1;
			if (player_Get_Torp_Used() || player_Get_Beam_Used()) {
				input->var2 = SCENE_ARM_SHIP;
			} else if (shipext_Is_Mission_Launch()) {
				input->var2 = SCENE_CUT_BATTLE_270;
			} else {
				input->var2 = SCENE_FLIGHT_BATTLE;
			}
			break;
		case 2: /* Map */
			input->var2 = SCENE_BRIEF_MAP;
			input->var1 = 1;
			break;
		case 3: /* Officer */
			input->var2 = SCENE_TALK_BRIEF_OFFICER;
			input->var1 = 1;
			break;
		case 4: /* Priest */
			input->var2 = SCENE_TALK_BRIEF_PRIEST;
			input->var1 = 1;
			break;
		default:
			break;
	}
	return 1;
}

// FUNCTION: TIE95 0x73410; TIE98 0x4067F0
static void iuser_Brief(Input* input, int32_t time) {
	(void)time;

	/* Map widget (id=2) drives the briefing map animation */
	if (input->id == 2) {
		player_Move_Display_Map();
		player_Step_Display_Map();
		TieMapSnapshot_Capture();
	}

	if (!input->var1)
		return; /* exit_pending */

	int16_t scene = input->var2; /* exit_code */

	/* Scenes 270, 4, 275: save pilot state before launching */
	if (scene == SCENE_CUT_BATTLE_270 || scene == SCENE_FLIGHT_BATTLE || scene == SCENE_ARM_SHIP) {
		if (options_gbl.auto_backup)
			shipext_Backup_Pilot();
		else
			shipext_Update_Pilot();
		shipext_Write_Temp_Pilot();
	}

	soundext_Stop_SFX(sfxText);
	xerror_Set_Landru_Exit(scene);
}

/* ================================================================
 * Actor callbacks
 * ================================================================ */

// FUNCTION: TIE95 0x73480; TIE98 0x406860
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

// FUNCTION: TIE95 0x734D8; TIE98 0x4068B0
static int16_t draw_Title(Actor* actor, Rect* bounds, Rect* clip, int16_t xoff, int16_t yoff,
						  int16_t refresh) {
	if (!refresh)
		return 0;

	xactdelt_Draw_Delta_Actor(actor, bounds, clip, xoff, yoff, refresh);

	int16_t offx, offy;
	xactor_Get_Actor_Offset(actor, &offx, &offy);

	Rect r;
	xrect_Set_Rect(&r, offx, offy, actor->w + offx, actor->h + offy);

	char label[32];
	TIEText text_id;
	switch (actor->var2) {
		case 0:
			text_id = txtBriefMainMenu;
			break;
		case 1:
			text_id = txtBriefEnter;
			break;
		case 2:
			text_id = txtBriefMap;
			break;
		case 3:
			text_id = txtBriefOfficer;
			break;
		case 4:
			text_id = txtBriefPriest;
			break;
		default:
			label[0] = 0;
			goto draw_text;
	}
	strcpy(label, textext_Get_Text(text_id));

draw_text:
	xrect_Offset_Rect(&r, 1, 1);
	xfont_Print_Centered_Text(label, &r, 16, active_spec->title_font);
	xrect_Offset_Rect(&r, -1, -1);
	xfont_Print_Centered_Text(label, &r, 15, active_spec->title_font);
	return 1;
}

// FUNCTION: TIE95 0x735D4; TIE98 0x4069E0
static void user_Door(Actor* actor, int32_t time) {
	if (!time) {
		actor->var2 = 0;
		actor->var1 = 0;
	}

	if (actor->var1) {
		if (!actor->state)
			soundext_Play_SFX(sfxSmallDoorOpen, 80);
		if (actor->state < actor->arraySize - 1)
			xactor_Set_Actor_State(actor, actor->state + 1, 0);
		actor->var1 = 0;
	} else {
		if (actor->state > 0) {
			xactor_Set_Actor_State(actor, actor->state - 1, 0);
			if (!actor->state)
				soundext_Play_SFX(sfxSmallDoorShut, 80);
		}
	}
}

/* ================================================================
 * Notice dialog ("Your pilot has been restored!")
 * ================================================================ */

// FUNCTION: TIE95 0x73668; TIE98 0x406A70
static Input* Build_Notice(const char* text) {
	(void)text;
	Rect r;

	xrect_Set_Rect(&r, 0, 0, active_spec->notice_width, active_spec->notice_height);
	Input* dlg = xinput_Alloc_Dialog_Input(NULL, &r, 0, 0);
	xinpattr_Set_Input_Draw_Function(dlg, idraw_Notice);
	xinpattr_Set_Input_Allign(dlg, 1, 1);
	xinpattr_Start_Input(dlg);

	textext_Copy_Text(notice_str, txtRegProtOK); /* "OK" */
	xrect_Set_Rect(&r, 0, 4, 80, 20);
	PushButton* btn = xbtnpush_Alloc_Button(dlg, &r, 0, iuser_Notice, notice_str, 1);
	xinpattr_Set_Input_Allign(&btn->header, 1, 2);

	return dlg;
}

// FUNCTION: TIE95 0x7370C; TIE98 0x406B20
static void idraw_Notice(Input* input, Rect* r, Rect* clip, int16_t refresh) {
	if (!refresh)
		return;

	Rect tr;
	xrect_Copy_Rect(&tr, r);
	xstyle_Style_Paint_Border(r, 0);
	tr.bottom = tr.top + active_spec->notice_text_height;

	xfont_Enable_FontID_Shadow(active_spec->notice_font);
	xfont_Print_Centered_Text(textext_Get_Text(txtBriefRestore), &tr, 15, active_spec->notice_font);
	xfont_Disable_FontID_Shadow(active_spec->notice_font);

	if (xinpattr_Is_Input_Dirty(input))
		xdirty_Dirty_Rect(clip);
}

// FUNCTION: TIE95 0x7377C; TIE98 0x406BB0
static void iuser_Notice(Input* input, int32_t time) {
	(void)time;
	if (xinpattr_Get_Input_Selected(input))
		xdialog_Set_Dialog_Exit(1);
}

/* ================================================================
 * Entry point
 * ================================================================ */

typedef enum {
	BRIEF_PHASE_BEGIN = 0,
	BRIEF_PHASE_NOTICE_DIALOG, /* SCENE_BRIEF_PRE: notice dialog pushed; resume on its pop */
	BRIEF_PHASE_PUSH_VIEW,     /* push the modal view task */
	BRIEF_PHASE_CLEANUP,
} BriefPhase;

typedef struct BriefTask {
	SceneHeadStruct* scene_head;
	Input* notice_dlg; /* allocated for the SCENE_BRIEF_PRE branch */
	BriefPhase phase;
	const BriefSpec* spec;
} BriefTask;

/* PORT: asynchronous adaptation of TIE95 BRIEF_Brief (0x72E20)
 * and TIE98 BRIEF_Brief (0x405FF0). */
static LandruTaskStepResult brief_task_step(void* self) {
	BriefTask* t = (BriefTask*)self;

	if (t->phase == BRIEF_PHASE_BEGIN) {
		Rect frame;
		const int16_t* bounds;

		active_spec = t->spec;
		if (active_spec->surface_set == LANDRU_SURFACE_SVGA) {
			(void)xsurface_Select_Surface_Set(active_spec->surface_set);
			xview_Init_View(xview_Get_Current_View());
			xvesa_Erase_Video(16);
		}

		/* Position mouse based on last scene and officer type */
		int16_t mouse_x, mouse_y;
		int16_t last = shellext_Get_Last_Scene();

		if (last == SCENE_TALK_BRIEF_OFFICER) {
			/* From officer */
			if (shipext_Get_Mission_Officer() == 1) {
				mouse_x = active_spec->officer_mouse_x;
				mouse_y = active_spec->officer_mouse_y;
			} else {
				mouse_x = active_spec->priest_mouse_x;
				mouse_y = active_spec->priest_mouse_y;
			}
		} else if (last == SCENE_BRIEF_MAP) {
			/* From map */
			if (shipext_Get_Mission_Officer() != 2) {
				mouse_x = active_spec->map_mouse_x;
				mouse_y = active_spec->map_mouse_y;
			} else {
				mouse_x = active_spec->priest_mouse_x;
				mouse_y = active_spec->priest_mouse_y;
			}
		} else if (last == SCENE_TALK_BRIEF_PRIEST) {
			/* From priest */
			mouse_x = active_spec->officer_mouse_x;
			mouse_y = active_spec->officer_mouse_y;
		} else {
			/* Default */
			mouse_x = active_spec->default_mouse_x;
			mouse_y = active_spec->default_mouse_y;
		}
		xio_Set_Mouse_Position(mouse_x, mouse_y);

		/* Load resources */
		ResFile* brief_res = shellext_Open_Empire_Resource(active_spec->archive);
		ResFile* player_res = shellext_Open_Empire_Resource("player.lfd");

		xrect_Set_Rect(&frame, 0, 0, active_spec->width, active_spec->height);

		/* Load brief film. Tag the snapshot with the (lfd, film) tuple
		 * so the cutscene compositor can resolve a remaster bundle for
		 * this screen. Default INCREMENTAL redraw model is correct —
		 * dirty-rect refresh, persistent RT (only the briefing map
		 * polygon animates). The tag covers both SCENE_BRIEF_PRE
		 * (notice-dialog branch) and SCENE_BRIEF, since both run the
		 * same film. Auto-cleared at the next scene transition by
		 * shell_run_scene_dispatch. */
		brief_film = xfilm_Res_Callback_Film("brief", &frame, 0, 0, 0, film_Callback);
		TieSnapshotBuilder_SetActiveFilm(active_spec->snapshot_lfd, "brief");
		xfilm_Set_Film_Def_Palette(brief_film, t->scene_head->def_palette);

		/* Create XINPUT widgets */
		parent = xinput_Alloc_Input(NULL, &frame, 0, 0);

		/* Main menu button (id=0) */
		bounds = active_spec->input_bounds[0];
		xrect_Set_Rect(&frame, bounds[0], bounds[1], bounds[2], bounds[3]);
		mainmenu_input = xinput_Alloc_Input(parent, &frame, 0, 0);
		xinpattr_Set_Input_Update_Function(mainmenu_input, iupdate_Brief);
		xinpattr_Set_Input_User_Function(mainmenu_input, iuser_Brief);
		mainmenu_input->mouseUsage = allInput;
		mainmenu_input->id = 0;

		/* Enter mission button (id=1) */
		bounds = active_spec->input_bounds[1];
		xrect_Set_Rect(&frame, bounds[0], bounds[1], bounds[2], bounds[3]);
		mission_input = xinput_Alloc_Input(parent, &frame, 0, 0);
		xinpattr_Set_Input_Update_Function(mission_input, iupdate_Brief);
		xinpattr_Set_Input_User_Function(mission_input, iuser_Brief);
		mission_input->mouseUsage = allInput;
		mission_input->id = 1;

		/* Map area (id=2) */
		bounds = active_spec->input_bounds[2];
		xrect_Set_Rect(&frame, bounds[0], bounds[1], bounds[2], bounds[3]);
		map_input = xinput_Alloc_Input(parent, &frame, 0, 0);
		xinpattr_Set_Input_Update_Function(map_input, iupdate_Brief);
		xinpattr_Set_Input_User_Function(map_input, iuser_Brief);
		map_input->mouseUsage = allInput;
		map_input->id = 2;

		/* Officer door (id=3) — skip if priest only */
		if (shipext_Get_Mission_Officer() != 2) {
			bounds = active_spec->input_bounds[3];
			xrect_Set_Rect(&frame, bounds[0], bounds[1], bounds[2], bounds[3]);
			officer_input = xinput_Alloc_Input(parent, &frame, 0, 0);
			xinpattr_Set_Input_Update_Function(officer_input, iupdate_Brief);
			xinpattr_Set_Input_User_Function(officer_input, iuser_Brief);
			officer_input->mouseUsage = allInput;
			officer_input->id = 3;
		}

		/* Priest door (id=4) — skip if officer only */
		if (shipext_Get_Mission_Officer() != 1) {
			bounds = active_spec->input_bounds[4];
			xrect_Set_Rect(&frame, bounds[0], bounds[1], bounds[2], bounds[3]);
			priest_input = xinput_Alloc_Input(parent, &frame, 0, 0);
			xinpattr_Set_Input_Update_Function(priest_input, iupdate_Brief);
			xinpattr_Set_Input_User_Function(priest_input, iuser_Brief);
			priest_input->mouseUsage = allInput;
			priest_input->id = 4;
		}

		xres_Close_Resource(player_res);
		xres_Close_Resource(brief_res);

		/* Initialize the briefing map with polygon projection */
		Poly p;
		const int16_t* poly = active_spec->map_poly;
		xrect_Set_Poly(&p, poly[0], poly[1], poly[2], poly[3], poly[4], poly[5], poly[6], poly[7]);
		player_Init_Brief_Display(map_input, &p);

		/* Scene 179 = restore path: show "pilot restored" notice
		 * BEFORE pushing the view (was inside end_View at frame 0).
		 * The notice dialog runs as a sub-task; once it pops, we
		 * fall through to PUSH_VIEW. */
		if (shellext_Get_Cur_Scene() == SCENE_BRIEF_PRE) {
			t->notice_dlg = Build_Notice(NULL);
			xio_Set_Mouse_Position(active_spec->notice_mouse_x, active_spec->notice_mouse_y);
			xdialog_Push_Dialog_View_Task(t->notice_dlg);
			t->phase = BRIEF_PHASE_NOTICE_DIALOG;
			return LANDRU_TASK_STEP_CONTINUE;
		}

		t->phase = BRIEF_PHASE_PUSH_VIEW;
		return LANDRU_TASK_STEP_CONTINUE;
	}

	if (t->phase == BRIEF_PHASE_NOTICE_DIALOG) {
		/* Notice dialog popped — clean up its Input chain, reposition
		 * the cursor for the brief view, fall through to PUSH_VIEW. */
		xdialog_Clear_Dialog_Exit();
		xinput_Free_Inputs(t->notice_dlg);
		t->notice_dlg = NULL;
		xio_Set_Mouse_Position(active_spec->notice_return_mouse_x, active_spec->notice_return_mouse_y);
		t->phase = BRIEF_PHASE_PUSH_VIEW;
		return LANDRU_TASK_STEP_CONTINUE;
	}

	if (t->phase == BRIEF_PHASE_PUSH_VIEW) {
		/* Push the modal view task */
		xview_Set_View_Update_Function(end_View);
		xviewadd_Clear_View();
		xview_Disable_All_View_Erase();

		xviewadd_Push_Handle_View_Task();

		t->phase = BRIEF_PHASE_CLEANUP;
		return LANDRU_TASK_STEP_CONTINUE;
	}

	/* CLEANUP */
	player_Free_Brief_Display();
	xview_Enable_All_View_Erase();
	xview_Clear_View_Update_Function();

	if (xcursor_Is_Cursor_Visible())
		xcursor_Hide_Cursor();

	if (t->spec->surface_set == LANDRU_SURFACE_SVGA) {
		xvesa_Erase_Video(16);
		xviewadd_Clear_View();
		(void)xsurface_Select_Surface_Set(LANDRU_SURFACE_VGA);
	}

	return LANDRU_TASK_STEP_DONE;
}

static const LandruTaskVtable brief_task_vt = {
	.step = brief_task_step,
};

void brief_Push_Brief_Task(SceneHeadStruct* scene_head) {
	BriefTask* t = (BriefTask*)landru_task_push(&brief_task_vt);
	if (!t)
		return;
	t->scene_head = scene_head;
	t->notice_dlg = NULL;
	t->phase = BRIEF_PHASE_BEGIN;
	t->spec = &brief_specs[TieProfile_FrontendId() == TIE_FRONTEND_PROFILE_TIE98 ? 1 : 0];
}
