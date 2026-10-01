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

#include "tie/brief.h"
#ifdef TIE_MODERN
#include "tie_runtime/runtime/brief_task.h"
#endif
#include "tie/edition.h"
#include "tie/player.h"
#include "tie/shellext.h"
#include "tie/shipext.h"
#include "tie/soundext.h"
#include "tie/textext.h"
#include "tie/tie.h"
#include "tie_runtime/runtime/profile.h"
#ifdef TIE_MODERN
#include "tie_runtime/snapshot/snapshot_internal.h"
#endif
#include "tie_runtime/snapshot/snapshot_map.h"

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

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

/* ---- Static globals ---- */

// GLOBAL: TIE95 0xF6038
// GLOBAL: TIE98 0x50AA40
static Actor* door[2]; /* door[0]=mainmenu, door[1]=mission */
// GLOBAL: TIE95 0xF5FF8
// GLOBAL: TIE98 0x50A9F8
static char notice_str[64]; /* OK button label buffer */
// GLOBAL: TIE95 0xF6054
// GLOBAL: TIE98 0x50AA48
static Actor* title_actor;
// GLOBAL: TIE95 0xF6058
// GLOBAL: TIE98 0x50A9EC
static Input* parent;
// GLOBAL: TIE95 0xF6050
// GLOBAL: TIE98 0x50A9DC
static Input* mainmenu_input; /* id=0: main menu */
// GLOBAL: TIE95 0xF6040
// GLOBAL: TIE98 0x50A9E4
static Input* mission_input; /* id=1: enter mission */
// GLOBAL: TIE95 0xF6044
// GLOBAL: TIE98 0x50A9F0
static Input* officer_input; /* id=3: officer */
// GLOBAL: TIE95 0xF6048
// GLOBAL: TIE98 0x50A9E8
static Input* priest_input; /* id=4: priest */
// GLOBAL: TIE95 0xF604C
// GLOBAL: TIE98 0x50A9D8
static Input* map_input; /* id=2: map */
// GLOBAL: TIE95 0xF6060
// GLOBAL: TIE98 0x50A9E0
static Film* brief_film;

/* ---- Forward declarations ---- */

static void brief_user_Title(Actor* actor, int32_t time);
static int16_t brief_draw_Title(Actor* actor, Rect* bounds, Rect* clip, int16_t xoff, int16_t yoff,
								int16_t refresh);
static void brief_user_Door(Actor* actor, int32_t time);
static Input* brief_Build_Notice(const char* text);
static void brief_idraw_Notice(Input* input, Rect* r, Rect* clip, int16_t refresh);
static void brief_iuser_Notice(Input* input, int32_t time);

/* ================================================================
 * View update callback
 * ================================================================ */

/* On the first view update, show the cursor and the pilot-restored notice. */
// FUNCTION: TIE95 0x7316C
// FUNCTION: TIE98 0x406520
void brief_end_View(int32_t frame_num) {
	if (frame_num == 0) {
		if (!xcursor_Is_Cursor_Visible())
			xcursor_Show_Cursor();
#ifndef TIE_MODERN
		/* Modern builds open the notice through the briefing task. */
		if (shellext_Get_Cur_Scene() == SCENE_BRIEF_PRE) {
			Input* notice = brief_Build_Notice(NULL);
#ifdef TIE98
			xio_Set_Mouse_Position(330, 260);
#else
			xio_Set_Mouse_Position(190, 110);
#endif
			xdialog_Handle_Dialog_View(notice);
			xdialog_Clear_Dialog_Exit();
#ifdef TIE98
			xio_Set_Mouse_Position(416, 302);
#else
			xio_Set_Mouse_Position(218, 126);
#endif
		}
#endif
	}
}

/* ================================================================
 * Film callback
 * ================================================================ */

// FUNCTION: TIE95 0x731E0
// FUNCTION: TIE98 0x406580
static int16_t brief_film_Callback(Film* film, FilmObject* fo) {
	Actor* actor;
	if (fo->id != 3)
		return 0; /* type_code: 3 = actor */

	xfilm_Rewind_Actor_Film(film, fo, (void*)((char*)fo + sizeof(FilmObject)));
	actor = (Actor*)fo->object;

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
			xactor_Set_Actor_User_Function(actor, (xactorCallback)brief_user_Door);
			door[actor->var2] = actor;
			actor->id = actor->var2;
			return 0;

		case 5: /* Hide if officer type is not 2 (priest-only actor) */
			return (shipext_Get_Mission_Officer() != 2) ? 0 : 1;

		case 6: /* Title label */
			title_actor = actor;
			xactor_Set_Actor_User_Function(actor, (xactorCallback)brief_user_Title);
			xactor_Set_Actor_Draw_Function(actor, brief_draw_Title);
			return 0;

		case 7: /* Returns 1 when officer != 2 (i.e. show for officer kind 1). */
			return shipext_Get_Mission_Officer() != 2;

		case 8: /* Additional TIE98 officer-room actor variant. */
			return TIE_FRONTEND_EDITION(0, shipext_Get_Mission_Officer() != 2);

		default:
			return 0;
	}
}

/* ================================================================
 * XINPUT callbacks
 * ================================================================ */

// FUNCTION: TIE95 0x7330C
// FUNCTION: TIE98 0x4066E0
static int16_t brief_iupdate_Brief(Input* input, Rect* bounds, Rect* clip, int16_t key, uint8_t left,
								   uint8_t right, int16_t mouse_x, int16_t mouse_y) {
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

// FUNCTION: TIE95 0x73410
// FUNCTION: TIE98 0x4067F0
static void brief_iuser_Brief(Input* input, int32_t time) {
	int16_t scene;
	(void)time;

	/* Map widget (id=2) drives the briefing map animation */
	if (input->id == 2) {
		player_Move_Display_Map();
		player_Step_Display_Map();
		TieMapSnapshot_Capture();
	}

	if (!input->var1)
		return; /* exit_pending */

	scene = input->var2; /* exit_code */

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

// FUNCTION: TIE95 0x73480
// FUNCTION: TIE98 0x406860
static void brief_user_Title(Actor* actor, int32_t time) {
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

// FUNCTION: TIE95 0x734D8
// FUNCTION: TIE98 0x4068B0
static int16_t brief_draw_Title(Actor* actor, Rect* bounds, Rect* clip, int16_t xoff, int16_t yoff,
								int16_t refresh) {
	int16_t offx, offy;
	Rect r;
	char label[32];
	int16_t font_id = TIE_FRONTEND_EDITION(0, 2);
	if (!refresh)
		return 0;

	xactdelt_Draw_Delta_Actor(actor, bounds, clip, xoff, yoff, refresh);

	xactor_Get_Actor_Offset(actor, &offx, &offy);

	xrect_Set_Rect(&r, offx, offy, actor->w + offx, actor->h + offy);

	switch (actor->var2) {
		case 0:
			strcpy(label, textext_Get_Text(txtBriefMainMenu));
			break;
		case 1:
			strcpy(label, textext_Get_Text(txtBriefEnter));
			break;
		case 2:
			strcpy(label, textext_Get_Text(txtBriefMap));
			break;
		case 3:
			strcpy(label, textext_Get_Text(txtBriefOfficer));
			break;
		case 4:
			strcpy(label, textext_Get_Text(txtBriefPriest));
			break;
		default:
			label[0] = 0;
			break;
	}

	xrect_Offset_Rect(&r, 1, 1);
	xfont_Print_Centered_Text(label, &r, 16, font_id);
	xrect_Offset_Rect(&r, -1, -1);
	xfont_Print_Centered_Text(label, &r, 15, font_id);
	return 1;
}

// FUNCTION: TIE95 0x735D4
// FUNCTION: TIE98 0x4069E0
static void brief_user_Door(Actor* actor, int32_t time) {
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

// FUNCTION: TIE95 0x73668
// FUNCTION: TIE98 0x406A70
static Input* brief_Build_Notice(const char* text) {
	Rect r;
	Input* dlg;
	PushButton* btn;
	(void)text;

	xrect_Set_Rect(&r, 0, 0, TIE_FRONTEND_EDITION(180, 280), TIE_FRONTEND_EDITION(40, 60));
	dlg = xinput_Alloc_Dialog_Input(NULL, &r, 0, 0);
	xinpattr_Set_Input_Draw_Function(dlg, brief_idraw_Notice);
	xinpattr_Set_Input_Allign(dlg, 1, 1);
	xinpattr_Start_Input(dlg);

	textext_Copy_Text(notice_str, txtRegProtOK); /* "OK" */
	xrect_Set_Rect(&r, 0, 4, 80, 20);
	btn = xbtnpush_Alloc_Button(dlg, &r, 0, brief_iuser_Notice, notice_str, 1);
	xinpattr_Set_Input_Allign(&btn->header, 1, 2);

	return dlg;
}

// FUNCTION: TIE95 0x7370C
// FUNCTION: TIE98 0x406B20
static void brief_idraw_Notice(Input* input, Rect* r, Rect* clip, int16_t refresh) {
	Rect tr;
	int16_t font_id = TIE_FRONTEND_EDITION(0, 2);
	if (!refresh)
		return;

	xrect_Copy_Rect(&tr, r);
	xstyle_Style_Paint_Border(r, 0);
	tr.bottom = tr.top + TIE_FRONTEND_EDITION(20, 30);

	xfont_Enable_FontID_Shadow(font_id);
	xfont_Print_Centered_Text(textext_Get_Text(txtBriefRestore), &tr, 15, font_id);
	xfont_Disable_FontID_Shadow(font_id);

	if (xinpattr_Is_Input_Dirty(input))
		xdirty_Dirty_Rect(clip);
}

// FUNCTION: TIE95 0x7377C
// FUNCTION: TIE98 0x406BB0
static void brief_iuser_Notice(Input* input, int32_t time) {
	(void)time;
	if (xinpattr_Get_Input_Selected(input))
		xdialog_Set_Dialog_Exit(1);
}

/* ================================================================
 * Entry point
 * ================================================================ */

// FUNCTION: TIE95 0x72E20
// FUNCTION: TIE98 0x405FF0
int16_t brief_Brief(SceneHeadStruct* scene_head) {
#ifdef TIE_MODERN
	Input* notice = NULL;
#endif
	int16_t mouse_x, mouse_y, last;
	ResFile* brief_res;
	ResFile* player_res;
	Poly p;
	Rect frame;

	/* Position mouse based on last scene and officer type */
	last = shellext_Get_Last_Scene();

	if (last == SCENE_TALK_BRIEF_OFFICER) {
		/* From officer */
		if (shipext_Get_Mission_Officer() == 1) {
			mouse_x = TIE_FRONTEND_EDITION(218, 416);
			mouse_y = TIE_FRONTEND_EDITION(126, 302);
		} else {
			mouse_x = TIE_FRONTEND_EDITION(282, 564);
			mouse_y = TIE_FRONTEND_EDITION(90, 180);
		}
	} else if (last == SCENE_BRIEF_MAP) {
		/* From map */
		if (shipext_Get_Mission_Officer() != 2) {
			mouse_x = TIE_FRONTEND_EDITION(160, 265);
			mouse_y = TIE_FRONTEND_EDITION(90, 190);
		} else {
			mouse_x = TIE_FRONTEND_EDITION(282, 564);
			mouse_y = TIE_FRONTEND_EDITION(90, 180);
		}
	} else if (last == SCENE_TALK_BRIEF_PRIEST) {
		/* From priest */
		mouse_x = TIE_FRONTEND_EDITION(218, 416);
		mouse_y = TIE_FRONTEND_EDITION(126, 302);
	} else {
		/* Default */
		mouse_x = TIE_FRONTEND_EDITION(78, 156);
		mouse_y = TIE_FRONTEND_EDITION(80, 160);
	}
	xio_Set_Mouse_Position(mouse_x, mouse_y);

	/* Load resources */
	brief_res = shellext_Open_Empire_Resource(TIE_FRONTEND_EDITION("brief.lfd", "brief640.lfd"));
	player_res = shellext_Open_Empire_Resource("player.lfd");

	xrect_Set_Rect(&frame, 0, 0, TIE_FRONTEND_EDITION(320, 640), TIE_FRONTEND_EDITION(200, 480));

	/* Load brief film. Tag the snapshot with the (lfd, film) tuple
	 * so the cutscene compositor can resolve a remaster bundle for
	 * this screen. Default INCREMENTAL redraw model is correct —
	 * dirty-rect refresh, persistent RT (only the briefing map
	 * polygon animates). The tag covers both SCENE_BRIEF_PRE
	 * (notice-dialog branch) and SCENE_BRIEF, since both run the
	 * same film. Auto-cleared at the next scene transition by
	 * shell_run_scene_dispatch. */
	brief_film = xfilm_Res_Callback_Film("brief", &frame, 0, 0, 0, brief_film_Callback);
#ifdef TIE_MODERN
	TieSnapshotBuilder_SetActiveFilm(TieProfile_UsesTie98Frontend() ? "BRIEF640" : "BRIEF", "brief");
#endif
	xfilm_Set_Film_Def_Palette(brief_film, scene_head->def_palette);

	/* Create XINPUT widgets */
	parent = xinput_Alloc_Input(NULL, &frame, 0, 0);

	/* Main menu button (id=0) */
	xrect_Set_Rect(&frame, TIE_FRONTEND_EDITION(84, 123), TIE_FRONTEND_EDITION(132, 329),
				   TIE_FRONTEND_EDITION(122, 184), TIE_FRONTEND_EDITION(172, 459));
	mainmenu_input = xinput_Alloc_Input(parent, &frame, 0, 0);
	xinpattr_Set_Input_Update_Function(mainmenu_input, brief_iupdate_Brief);
	xinpattr_Set_Input_User_Function(mainmenu_input, brief_iuser_Brief);
	mainmenu_input->mouseUsage = allInput;
	mainmenu_input->id = 0;

	/* Enter mission button (id=1) */
	xrect_Set_Rect(&frame, TIE_FRONTEND_EDITION(190, 385), TIE_FRONTEND_EDITION(118, 256),
				   TIE_FRONTEND_EDITION(248, 448), TIE_FRONTEND_EDITION(138, 350));
	mission_input = xinput_Alloc_Input(parent, &frame, 0, 0);
	xinpattr_Set_Input_Update_Function(mission_input, brief_iupdate_Brief);
	xinpattr_Set_Input_User_Function(mission_input, brief_iuser_Brief);
	mission_input->mouseUsage = allInput;
	mission_input->id = 1;

	/* Map area (id=2) */
	xrect_Set_Rect(&frame, TIE_FRONTEND_EDITION(28, 50), TIE_FRONTEND_EDITION(46, 80),
				   TIE_FRONTEND_EDITION(128, 230), TIE_FRONTEND_EDITION(124, 290));
	map_input = xinput_Alloc_Input(parent, &frame, 0, 0);
	xinpattr_Set_Input_Update_Function(map_input, brief_iupdate_Brief);
	xinpattr_Set_Input_User_Function(map_input, brief_iuser_Brief);
	map_input->mouseUsage = allInput;
	map_input->id = 2;

	/* Officer door (id=3) — skip if priest only */
	if (shipext_Get_Mission_Officer() != 2) {
		xrect_Set_Rect(&frame, TIE_FRONTEND_EDITION(140, 230), TIE_FRONTEND_EDITION(66, 157),
					   TIE_FRONTEND_EDITION(174, 274), TIE_FRONTEND_EDITION(114, 251));
		officer_input = xinput_Alloc_Input(parent, &frame, 0, 0);
		xinpattr_Set_Input_Update_Function(officer_input, brief_iupdate_Brief);
		xinpattr_Set_Input_User_Function(officer_input, brief_iuser_Brief);
		officer_input->mouseUsage = allInput;
		officer_input->id = 3;
	}

	/* Priest door (id=4) — skip if officer only */
	if (shipext_Get_Mission_Officer() != 1) {
		xrect_Set_Rect(&frame, TIE_FRONTEND_EDITION(242, 498), TIE_FRONTEND_EDITION(66, 125),
					   TIE_FRONTEND_EDITION(314, 604), TIE_FRONTEND_EDITION(102, 220));
		priest_input = xinput_Alloc_Input(parent, &frame, 0, 0);
		xinpattr_Set_Input_Update_Function(priest_input, brief_iupdate_Brief);
		xinpattr_Set_Input_User_Function(priest_input, brief_iuser_Brief);
		priest_input->mouseUsage = allInput;
		priest_input->id = 4;
	}

	xres_Close_Resource(player_res);
	xres_Close_Resource(brief_res);

	/* Initialize the briefing map with polygon projection */
	xrect_Set_Poly(&p, TIE_FRONTEND_EDITION(34, 56), TIE_FRONTEND_EDITION(50, 83),
				   TIE_FRONTEND_EDITION(127, 223), TIE_FRONTEND_EDITION(58, 93),
				   TIE_FRONTEND_EDITION(125, 225), TIE_FRONTEND_EDITION(106, 244),
				   TIE_FRONTEND_EDITION(36, 67), TIE_FRONTEND_EDITION(120, 277));
	player_Init_Brief_Display(map_input, &p);
#ifdef TIE_MODERN

	if (shellext_Get_Cur_Scene() == SCENE_BRIEF_PRE) {
		notice = brief_Build_Notice(NULL);
		xio_Set_Mouse_Position(TieProfile_UsesTie98Frontend() ? 330 : 190,
							   TieProfile_UsesTie98Frontend() ? 260 : 110);
	}
	TieBrief_RunView(notice, TieProfile_UsesTie98Frontend(), TieProfile_UsesTie98Frontend() ? 416 : 218,
					 TieProfile_UsesTie98Frontend() ? 302 : 126);
	return 0;
#else
	xview_Set_View_Update_Function(brief_end_View);
	xviewadd_Clear_View();
	xview_Disable_All_View_Erase();
	shellext_Handle_TIE_View();
	player_Free_Brief_Display();
	xview_Enable_All_View_Erase();
	xview_Clear_View_Update_Function();
	if (xcursor_Is_Cursor_Visible())
		xcursor_Hide_Cursor();
	return xerror_Get_Landru_Exit();
#endif
}
