#include "tie/map.h"
#ifdef TIE_MODERN
#include "tie_runtime/runtime/map_task.h"
#endif
#include "tie/goals.h"
#include "tie/mission.h"
#include "tie/player.h"
#include "tie/shade.h"
#include "tie/shellext.h"
#include "tie/shipext.h"
#include "tie/soundext.h"
#include "tie/talk.h"
#include "tie/textext.h"
#include "tie/tie.h"
#ifdef TIE_MODERN
#include "tie_runtime/snapshot/snapshot_internal.h"
#endif
#include "tie_runtime/storage/pilot_storage.h"
#include "tie_runtime/storage/score_tables.h"

#include "landru/actanim.h"
#include "landru/actdelt.h"
#include "landru/actor.h"
#include "landru/btnpush.h"
#include "landru/cursor.h"
#include "landru/dirty.h"
#include "landru/error.h"
#include "landru/fade.h"
#include "landru/file.h"
#include "landru/film.h"
#include "landru/font.h"
#include "landru/inpattr.h"
#include "landru/input.h"
#include "landru/io.h"
#include "landru/memhdl.h"
#include "landru/paint.h"
#include "landru/pal.h"
#include "landru/rect.h"
#include "landru/res.h"
#include "landru/view.h"
#include "landru/viewadd.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The original TU calls the library strcat() and strcpy() rather than the inline forms. */
#ifdef __WATCOMC__
#pragma function(strcat)
#pragma function(strcpy)
#endif

/* ======================================================================
 * Static data
 * ====================================================================== */

enum MapStr {
	MAP_LFD = 0,            /* "map.lfd" — main resource file */
	MAP_BRIEF_BG = 1,       /* "brfmap1" — briefing background */
	MAP_BRIEF_PAL = 2,      /* "brfpnl" — briefing palette */
	MAP_BUTTON_ICONS = 3,   /* "cmbticns" — button icon animation */
	MAP_COMBAT_BG = 4,      /* "cmbtmap1" — combat map background */
	MAP_COMBAT_PAL = 5,     /* "combatvr" — combat palette */
	MAP_PLAYER_LFD = 6,     /* "player.lfd" — player resource file */
	MAP_BRIEF_PANEL = 7,    /* "brfpnl" — sliding briefing panel */
	MAP_BRIEF_BG2 = 8,      /* "brfmap2" — briefing background layer 2 */
	MAP_PANEL_HANDLE = 9,   /* "pnlhldr" — panel handle animation */
	MAP_BRIEF_BUTTONS = 10, /* "brfbutns" — briefing button bar */
	MAP_COMBAT_TEXT = 11,   /* "cmbtmap3" — combat text overlay */
	MAP_COMBAT_TITLE = 12,  /* "title" — combat title overlay */
	MAP_TRAIN_TEXT = 13,    /* "trnmap1" — training text background */
	MAP_TRAIN_BG = 14,      /* "trnmap2" — training map background */
	MAP_TRAIN_OVERLAY = 15, /* "trnmap3" — training text overlay */
	MAP_TRAIN_TITLE = 16,   /* "trntitle" — training title overlay */
};

// GLOBAL: TIE95 0xCF96C
// GLOBAL: TIE98 0x4E4730
static const char map_str[17][14] = { "map.lfd",    "brfmap1", "brfpnl",  "cmbticns", "cmbtmap1", "combatvr",
									  "player.lfd", "brfpnl",  "brfmap2", "pnlhldr",  "brfbutns", "cmbtmap3",
									  "title",      "trnmap1", "trnmap2", "trnmap3",  "trntitle" };

// GLOBAL: TIE95 0xCFA5A
// GLOBAL: TIE98 0x4E4820
static const int16_t map_panel_y[5] = { 43, 29, 14, 10, 11 };
// GLOBAL: TIE95 0xCFA64
// GLOBAL: TIE98 0x4E4830
static const int16_t map_panel_hdl_y[5] = { 92, 80, 89, 84, 85 };
// GLOBAL: TIE95 0xCFA6E
// GLOBAL: TIE98 0x4E4840
static const int16_t map_panel_hdl_cel[5] = { 0, 0, 2, 2, 2 };

/* Button rects: [0-5]=training/combat, [6-9]=briefing (offset by index) */
// GLOBAL: TIE95 0xCFA7A
// GLOBAL: TIE98 0x4E4850
static const Rect map_rect[10] = {
	{ 172, 29, 194, 61 },   /* Stop */
	{ 172, 64, 194, 96 },   /* Play */
	{ 172, 99, 194, 131 },  /* Skip */
	{ 172, 260, 194, 292 }, /* Exit */
	{ 172, 144, 194, 176 }, /* ViewOfficer */
	{ 172, 225, 194, 257 }, /* ViewPriest/Enter */
	{ 175, 100, 196, 132 }, /* Brief Stop */
	{ 175, 135, 196, 167 }, /* Brief Play */
	{ 175, 169, 196, 202 }, /* Brief Skip */
	{ 175, 216, 196, 249 }, /* Brief Exit */
};

/* ======================================================================
 * Static BSS globals
 * ====================================================================== */

// GLOBAL: TIE95 0xF60E8
// GLOBAL: TIE98 0x584C98
static int16_t talk_win_id[6];
// GLOBAL: TIE95 0xF60FC
// GLOBAL: TIE98 0x584CB0
static Input* stop_input;
// GLOBAL: TIE95 0xF6110
// GLOBAL: TIE98 0x584C74
static Input* play_input;
// GLOBAL: TIE95 0xF60F4
// GLOBAL: TIE98 0x584CCC
static EBriefStruct* talk_brief;
// GLOBAL: TIE95 0xF60F8
// GLOBAL: TIE98 0x584C7C
static Input* map_input;
// GLOBAL: TIE95 0xF6100
// GLOBAL: TIE98 0x584CB8
static Input* talk_input;
// GLOBAL: TIE95 0xF6104
// GLOBAL: TIE98 0x584C60
static Actor* title_actor;
// GLOBAL: TIE95 0xF6108
// GLOBAL: TIE98 0x584CA4
static Actor* cmbticons;
// GLOBAL: TIE95 0xF6114
// GLOBAL: TIE98 0x584C6C
static EFArrayStruct* talk_fgroup;
// GLOBAL: TIE95 0xF611C
// GLOBAL: TIE98 0x584C80
static int16_t talk_win_status[5];
// GLOBAL: TIE95 0xF6126
// GLOBAL: TIE98 0x584CC8
static int16_t max_paragraph_size;
// GLOBAL: TIE95 0xF6128
// GLOBAL: TIE98 0x584C8C
static int16_t map_text;
// GLOBAL: TIE95 0xF612C
// GLOBAL: TIE98 0x584CBC
static int16_t map_text_count;
// GLOBAL: TIE95 0xF6142
// GLOBAL: TIE98 0x584CE0
static int16_t num_talk_paragraphs;
// GLOBAL: TIE95 0xF6130
// GLOBAL: TIE98 0x584CD8
static int16_t combat_pilot_medal_status;
// GLOBAL: TIE95 0xF6132
// GLOBAL: TIE98 0x584C70
static int16_t center_line;
// GLOBAL: TIE95 0xF6134
// GLOBAL: TIE98 0x584CA8
static int16_t cur_talk_paragraph;
// GLOBAL: TIE95 0xF6136
// GLOBAL: TIE98 0x584C64
static int16_t combat_pilot_medal_init;
// GLOBAL: TIE95 0xF6138
// GLOBAL: TIE98 0x584CDC
static int16_t talk_mode;
// GLOBAL: TIE95 0xF613A
// GLOBAL: TIE98 0x584CB4
static int16_t num_talk_questions;
// GLOBAL: TIE95 0xF613C
// GLOBAL: TIE98 0x584CAC
static int16_t next_mode;
// GLOBAL: TIE95 0xF613E
// GLOBAL: TIE98 0x584C78
static int16_t talk_person;
// GLOBAL: TIE95 0xF6140
// GLOBAL: TIE98 0x584C68
static int16_t cur_talk_question;
// GLOBAL: TIE95 0xCFA78
// GLOBAL: TIE98 0x584CE4
static uint8_t map_uses_battle_voice;
// GLOBAL: TIE95 0xF6144
// GLOBAL: TIE98 0x584CD0
static uint8_t map_is_post_mission;

/* extern per watdbg */
// GLOBAL: TIE95 0xF612A
// GLOBAL: TIE98 0x5FD27C
int16_t train_pilot_medal_status;

/* Forward declarations — only for functions called before their definition */
static int16_t map_Count_VR_Debrief_Pages(void);
static void map_Get_VR_Debrief_Line(char* string, int16_t line);

/* ======================================================================
 * View callback
 * ====================================================================== */

/* Tracks the last paragraph id whose voice we triggered. Compared
 * against talk_voice_question every frame to detect when the
 * briefing engine advances to a new paragraph (BCMD_SHOW_PARA1
 * stamps the new id) and we should fire the next .voc. */
// GLOBAL: TIE95 0xF612E
// GLOBAL: TIE98 0x584CC4
static int16_t last_voiced_paragraph;

static void map_end_View(int32_t refresh);
static int16_t map_iupdate_Map(Input* input, Rect* r, Rect* clip_r, int16_t key, uint8_t left, uint8_t right,
							   int16_t x, int16_t y);
static void map_iuser_Map(Input* input, int32_t time);
static void map_idraw_Map(Input* input, Rect* r, Rect* clip_r, int16_t refresh);
static int16_t map_user_Map_Panel(Actor* actor, int32_t time);
static int16_t map_draw_Map_Text(Actor* actor, Rect* r, Rect* clip_r, int16_t x, int16_t y, int16_t refresh);
static void map_idraw_Talk(Input* input, Rect* r, Rect* clip_r, int16_t refresh);
static void map_Set_VR_Talk_To_Text(int16_t person);
static void map_Set_VR_Talk_Paragraph(void);
static void map_Get_VR_Talk_Question(char* string, int16_t question);
static void map_Get_VR_Talk_Paragraph(char* string, int16_t line);
static void map_Check_VR_Talk_Questions(void);
static int16_t map_Count_VR_Debrief_Header(void);
static void map_Find_VR_Debrief_Header(char* string, int16_t line);
static void map_Get_VR_Debrief_Header(char* string, int16_t line);
static void map_Find_VR_Debrief_Goals(char* string, int16_t line);
static void map_Get_VR_Debrief_Goals(char* string, int16_t line);
static int16_t map_Count_VR_Debrief_Kills(void);
static void map_Find_VR_Debrief_Kills(char* string, int16_t page, int16_t line);
static void map_Get_VR_Debrief_Kill_Title(char* string);
static void map_Get_VR_Debrief_Kills(char* string, int16_t craft_idx);
static int16_t map_Count_VR_Debrief_Losses(void);
static void map_Find_VR_Debrief_Losses(char* string, int16_t page, int16_t line);
static void map_Get_VR_Debrief_Loss_Title(char* string);
static void map_Get_VR_Debrief_Losses(char* string, int16_t craft_idx);
static int16_t map_Count_VR_Debrief_Captures(void);
static void map_Find_VR_Debrief_Captures(char* string, int16_t page, int16_t line);
static void map_Get_VR_Debrief_Capture_Title(char* string);
static void map_Get_VR_Debrief_Captures(char* string, int16_t craft_idx);
static void map_Update_Debrief_Scores(void);
static void map_Set_Voice_Species_Mission(void);

/* ======================================================================
 * map_Map — main entry point
 * ====================================================================== */

// FUNCTION: TIE95 0x745E0
// FUNCTION: TIE98 0x44D870
int16_t map_Map(SceneHeadStruct* scene_head) {
	ResFile *file, *pfile;
	Palette* the_palette;
	Input *parent, *the_input;
	Actor* the_actor;
	Rect r;
	int16_t scene, index, i;

	scene = shellext_Get_Cur_Scene();
	map_uses_battle_voice = 0;
	map_is_post_mission = 0;
	map_text = -1;
	map_text_count = 0;

	/* Scene-specific init */
	if (scene == SCENE_TRAIN_MAP) {
		train_pilot_medal_status =
			(train_pilot_medal_status < 4 && pilot_record.train_max_level[shipext_Get_Train_Ship()] >= 4);
		talk_voice_mood = 'd';
		map_is_post_mission = 1;
		xio_Set_Mouse_Position(276, 180);
	} else if (scene == SCENE_COMBAT_MAP_A) {
		if (!shipext_Is_Combat_Ship_Tour()) {
			combat_pilot_medal_init = 0;
			for (i = 0; i < 8; i++) {
				if (pilot_record.combat_complete[shipext_Get_Combat_Ship()][i])
					combat_pilot_medal_init++;
			}
		}
		talk_voice_mood = 'b';
		xio_Set_Mouse_Position(240, 180);
	} else if (scene == SCENE_COMBAT_MAP_B) {
		if (!shipext_Is_Combat_Ship_Tour()) {
			index = 0;
			for (i = 0; i < 8; i++) {
				if (pilot_record.combat_complete[shipext_Get_Combat_Ship()][i])
					index++;
			}
			if (index <= combat_pilot_medal_init)
				combat_pilot_medal_status = 0;
			else if (index >= 2 && index <= 4)
				combat_pilot_medal_status = index - 1;
			else
				combat_pilot_medal_status = 0;
			combat_pilot_medal_init = index;
		} else {
			combat_pilot_medal_status = 0;
		}
		map_is_post_mission = 1;
		if (shipext_Is_Combat_Mission_Success()) {
			talk_voice_mood = 'd';
			xio_Set_Mouse_Position(112, 180);
		} else {
			talk_voice_mood = 'h';
			xio_Set_Mouse_Position(250, 180);
		}
	} else if (scene == SCENE_BRIEF_MAP) {
		xio_Set_Mouse_Position(240, 180);
		last_voiced_paragraph = 0;
		talk_voice_question = 0;
		talk_voice_mood = 0;
		talk_voice_officer = 'i';
		map_uses_battle_voice = 1;
		/* Tag the snapshot with (lfd, film) so the cutscene compositor
		 * can resolve a remaster bundle for this screen. The bundle key
		 * MAP/brief points at output/MAP/films/brief/manifest.yaml,
		 * which lists the chrome actors (brfmap1/2, brfpnl, pnlhldr,
		 * brfbutns) and chains to PLAYER + EMPIRE for icons + stars.
		 * Auto-cleared at the next scene transition by
		 * shell_run_scene_dispatch. */
#ifdef TIE_MODERN
		TieSnapshotBuilder_SetActiveFilm("MAP", "brief");
#endif
	}

	map_Update_Debrief_Scores();

	/* Load resources */
	file = shellext_Open_Empire_Resource(map_str[MAP_LFD]);
	pfile = shellext_Open_Empire_Resource(map_str[MAP_PLAYER_LFD]);
	xrect_Set_Rect(&r, 0, 0, 320, 200);

	if (scene == SCENE_BRIEF_MAP) {
		the_actor = xactdelt_Res_Delta_Actor(map_str[MAP_BRIEF_BG], &r, 0, 0, 50);
		/* Classic FB optimization: brfmap1 paints once at scene start
		 * and the diff bitmap restores its pixels on dirty-rect refresh,
		 * so it doesn't need to re-emit. The HD cutscene RT has no
		 * diff bitmap — leaving brfmap1 Non_Refreshable means moving
		 * actors (brfpanel slide) leave trails on the persistent RT
		 * because nothing repaints the underlying background. Re-
		 * enabling refresh costs one extra delta blit per classic-FB
		 * tick — negligible — and lets HD render correctly. */
		/* xactor_Non_Refreshable_Actor(the_actor); */
		the_actor = xactdelt_Res_Delta_Actor(map_str[MAP_BRIEF_BG2], &r, 0, 0, 50);
		the_actor = xactdelt_Res_Delta_Actor(map_str[MAP_BRIEF_PANEL], &r, 0, 0, 20);
		xactor_Set_Actor_User_Function(the_actor, (xactorCallback)map_user_Map_Panel);
		the_actor->id = 0;
		the_actor = xactanim_Res_Anim_Actor(map_str[MAP_PANEL_HANDLE], &r, 0, 0, 20);
		xactor_Set_Actor_User_Function(the_actor, (xactorCallback)map_user_Map_Panel);
		the_actor->id = 1;
		cmbticons = xactanim_Res_Anim_Actor(map_str[MAP_BRIEF_BUTTONS], &r, 0, 12, 0);
	} else if (scene == SCENE_TRAIN_MAP) {
		the_actor = xactdelt_Res_Delta_Actor(map_str[MAP_TRAIN_OVERLAY], &r, 0, 0, 0);
		xactor_Set_Actor_Draw_Function(the_actor, map_draw_Map_Text);
		the_actor->id = 1;
		the_actor = xactdelt_Res_Delta_Actor(map_str[MAP_TRAIN_TEXT], &r, 0, 0, 0);
		xactor_Non_Refreshable_Actor(the_actor);
		cmbticons = xactanim_Res_Anim_Actor(map_str[MAP_BUTTON_ICONS], &r, 0, 0, 0);
		title_actor = xactdelt_Res_Delta_Actor(map_str[MAP_TRAIN_TITLE], &r, 0, 0, 0);
		xactor_Set_Actor_Time(title_actor, 0, 0);
	} else {
		the_actor = xactdelt_Res_Delta_Actor(map_str[MAP_COMBAT_TEXT], &r, 0, 0, 0);
		xactor_Set_Actor_Draw_Function(the_actor, map_draw_Map_Text);
		the_actor->id = 1;
		the_actor = xactdelt_Res_Delta_Actor(map_str[MAP_COMBAT_BG], &r, 0, 0, 0);
		xactor_Non_Refreshable_Actor(the_actor);
		cmbticons = xactanim_Res_Anim_Actor(map_str[MAP_BUTTON_ICONS], &r, 0, 0, 0);
		title_actor = xactdelt_Res_Delta_Actor(map_str[MAP_COMBAT_TITLE], &r, 0, 0, 0);
		xactor_Set_Actor_Time(title_actor, 0, 0);
	}

	xactor_Set_Actor_Time(cmbticons, -1, -1);
	parent = xinput_Alloc_Input(NULL, &r, 0, 0);
	index = (scene == SCENE_BRIEF_MAP) ? 6 : 0;
	max_paragraph_size = 10;

	/* Create buttons (not for training scene) */
	if (scene != SCENE_TRAIN_MAP) {
		/* Stop */
		the_input = (Input*)xbtnpush_Alloc_Button(parent, (Rect*)&map_rect[index], 0, map_iuser_Map, NULL, 0);
		xinpattr_Set_Input_Update_Function(the_input, map_iupdate_Map);
		xinpattr_Set_Input_Draw_Function(the_input, map_idraw_Map);
		xinpattr_Refreshable_Input(the_input);
		the_input->mouseUsage = allInput;
		stop_input = the_input;

		/* Play */
		the_input =
			(Input*)xbtnpush_Alloc_Button(parent, (Rect*)&map_rect[index + 1], 0, map_iuser_Map, NULL, 1);
		xinpattr_Set_Input_Update_Function(the_input, map_iupdate_Map);
		xinpattr_Set_Input_Draw_Function(the_input, map_idraw_Map);
		xinpattr_Refreshable_Input(the_input);
		the_input->mouseUsage = allInput;
		play_input = the_input;

		/* Skip */
		the_input =
			(Input*)xbtnpush_Alloc_Button(parent, (Rect*)&map_rect[index + 2], 0, map_iuser_Map, NULL, 2);
		xinpattr_Set_Input_Update_Function(the_input, map_iupdate_Map);
		xinpattr_Set_Input_Draw_Function(the_input, map_idraw_Map);
		xinpattr_Refreshable_Input(the_input);
		the_input->mouseUsage = allInput;
	}

	/* Exit (always present) */
	the_input = (Input*)xbtnpush_Alloc_Button(parent, (Rect*)&map_rect[index + 3], 0, map_iuser_Map, NULL, 5);
	xinpattr_Set_Input_Update_Function(the_input, map_iupdate_Map);
	xinpattr_Set_Input_Draw_Function(the_input, map_idraw_Map);
	xinpattr_Refreshable_Input(the_input);
	the_input->mouseUsage = allInput;

	/* Combat: Enter Mission + ViewOfficer/ViewPriest buttons */
	if (scene == SCENE_COMBAT_MAP_A || scene == SCENE_COMBAT_MAP_B) {
		if (scene == SCENE_COMBAT_MAP_A || !shipext_Get_Mission_Officer()) {
			the_input =
				(Input*)xbtnpush_Alloc_Button(parent, (Rect*)&map_rect[index + 4], 0, map_iuser_Map, NULL, 3);
			xinpattr_Set_Input_Update_Function(the_input, map_iupdate_Map);
			xinpattr_Set_Input_Draw_Function(the_input, map_idraw_Map);
			xinpattr_Refreshable_Input(the_input);
			the_input->mouseUsage = allInput;
		}
		the_input =
			(Input*)xbtnpush_Alloc_Button(parent, (Rect*)&map_rect[index + 5], 0, map_iuser_Map, NULL, 4);
		xinpattr_Set_Input_Update_Function(the_input, map_iupdate_Map);
		xinpattr_Set_Input_Draw_Function(the_input, map_idraw_Map);
		xinpattr_Refreshable_Input(the_input);
		the_input->mouseUsage = allInput;
	}

	/* Map + talk display areas */
	if (scene == SCENE_BRIEF_MAP)
		xrect_Set_Rect(&r, 13, 18, 307, 167);
	else
		xrect_Set_Rect(&r, 13, 6, 307, 155);

	map_input = xinput_Alloc_Input(parent, &r, 0, 0);
	talk_input = xinput_Alloc_Input(parent, &r, 0, 0);
	xinpattr_Set_Input_Draw_Function(talk_input, map_idraw_Talk);

	/* Palette setup */
	if (scene == SCENE_COMBAT_MAP_A) {
		xpal_Set_Dest_Pal_Color(1, 255, 6, 6, 6);
		xpal_Dest_To_Screen_Palette(1, 1, 255);
	}
	xpal_Set_Dest_Palette(scene_head->def_palette);
	the_palette = xpal_Res_Palette("range");
	xpal_Set_Dest_Palette(the_palette);
	the_palette =
		xpal_Res_Palette((scene == SCENE_COMBAT_MAP_A) ? map_str[MAP_COMBAT_PAL] : map_str[MAP_BRIEF_PAL]);
	xpal_Set_Dest_Palette(the_palette);

	if (scene == SCENE_COMBAT_MAP_A)
		xfade_Start_Full_Fade(FADE_WIPE_SNAP_ON, FADE_COLOR_PAL_TO_PAL, 1, 0, 0);
	else
		xfade_Start_Full_Fade(FADE_WIPE_SNAP_ON, FADE_COLOR_TWO_PHASE, 1, 0, 1);

	xres_Close_Resource(pfile);
	xres_Close_Resource(file);

	/* Init talk mode */
	if (scene == SCENE_COMBAT_MAP_B || scene == SCENE_TRAIN_MAP) {
		xinpattr_Hide_Input(map_input);
		if (shipext_Get_Mission_Officer() == 2) {
			next_mode = 2;
			talk_voice_officer = 'p';
			talk_voice_question = 1;
			talk_mode = 2;
		} else {
			talk_mode = 1;
			talk_voice_question = 1;
			talk_voice_officer = 'o';
			if (shipext_Get_Mission_Officer() == 1)
				next_mode = 1;
			else
				next_mode = 2;
		}
	} else {
		xinpattr_Hide_Input(talk_input);
		talk_mode = 0;
		last_voiced_paragraph = 0;
		talk_voice_officer = 'i';
		next_mode = (shipext_Get_Mission_Officer() == 2) ? 2 : 1;
	}

	player_Init_Brief_Display(map_input, NULL);
	talk_brief = player_Fetch_Brief();
	talk_fgroup = player_Fetch_FGroup();
	map_Set_VR_Talk_To_Text(talk_mode == 2 ? 1 : 0);

	/* Push the modal view task */
	xview_Set_View_Update_Function(map_end_View);
	xviewadd_Clear_View();
	xview_Disable_All_View_Erase();
	map_Set_Voice_Species_Mission();
	talk_Alloc_Speech_Sound();
	talk_paragraph_timer = 0x7FFFFFFF;
#ifdef TIE_MODERN
	TieMap_RunView();
	return 0;
#else
	shellext_Handle_TIE_View();
	talk_Free_Speech_Sound();
	player_Free_Brief_Display();
	xview_Enable_All_View_Erase();
	xview_Clear_View_Update_Function();

	if (xcursor_Is_Cursor_Visible())
		xcursor_Hide_Cursor();
	return xerror_Get_Landru_Exit();
#endif
}

// FUNCTION: TIE95 0x74E48
// FUNCTION: TIE98 0x44E3A0
static void map_end_View(int32_t refresh) {
	/* Briefing-map voice path. Retail MAP_end_View does the same:
	 * when the page id stamped by PLAYER_Step_Page differs from the
	 * last-voiced id and the officer character is 'i' (info
	 * briefing), play the corresponding .voc. The talk-mode and
	 * combat-debrief paths run their voice trigger from
	 * map_iuser_Map / map_Set_VR_Talk_To_Text instead — only 'i' gets the
	 * end-view treatment. */
	if (last_voiced_paragraph != talk_voice_question && talk_voice_officer == 'i') {
		talk_Start_Speech_Stream();
		last_voiced_paragraph = talk_voice_question;
	}

	if (!refresh && !xcursor_Is_Cursor_Visible())
		xcursor_Show_Cursor();
}

/* ======================================================================
 * iupdate/iuser/idraw callbacks for MAP buttons
 * ====================================================================== */

// FUNCTION: TIE95 0x74EA8
// FUNCTION: TIE98 0x44E3F0
static int16_t map_iupdate_Map(Input* input, Rect* r, Rect* clip_r, int16_t key, uint8_t left, uint8_t right,
							   int16_t x, int16_t y) {
	uint8_t button;
	PushButton* btn = (PushButton*)input;

	(void)clip_r;
	if (key)
		return 0;

#ifdef TIE_MODERN
	/* Retail leaves button uninitialized when neither mouse button is set. */
	button = 0;
#endif
	if (left)
		button = left;
	if (right)
		button = right;

	switch (button) {
		case 1:
			btn->pressed = 1;
			break;
		case 2:
			btn->pressed = xrect_Point_In_Rect(r, x + r->left, y + r->top);
			break;
		case 3:
			if (btn->pressed) {
				xinpattr_Selected_Input(input);
				btn->pressed = 0;
			}
			break;
	}

	xinpattr_Refresh_Input(input);
	map_text_count = 12;

	switch (input->id) {
		case 0:
			if (talk_mode)
				map_text = txtMapLastQuestion;
			else if (player_Is_Map_Playing())
				map_text = txtMapBriefStop;
			else
				map_text = txtMapBriefRewind;
			break;
		case 1:
			if (talk_mode)
				map_text = txtMapNextQuestion;
			else if (player_Is_Map_Playing())
				map_text = txtMapBriefRewind;
			else
				map_text = txtMapBriefPlay;
			break;
		case 2:
			if (talk_mode)
				map_text = txtMapNextPage;
			else
				map_text = txtMapBriefSkip;
			break;
		case 3:
			/* The "View" button cycles through three labels matching the
			 * upcoming talk mode (next_mode). Showing 'Exit' here was wrong
			 * — that label belongs to the Exit button (case 4/5). */
			switch (next_mode) {
				case 0:
					map_text = txtMapViewBriefing;
					break;
				case 1:
					map_text = txtMapViewOfficer;
					break;
				case 2:
					map_text = txtMapViewPriest;
					break;
			}
			break;
		case 4:
			if (shellext_Get_Cur_Scene() == SCENE_COMBAT_MAP_A)
				map_text = txtMapEnterMission;
			else
				map_text = txtDebriefAgain;
			break;
		case 5:
			if (shellext_Get_Cur_Scene() == SCENE_COMBAT_MAP_A)
				map_text = txtMapExitBriefing;
			else
				map_text = txtMapExitDebriefing;
			break;
	}
	return 1;
}

// FUNCTION: TIE95 0x75110
// FUNCTION: TIE98 0x44E5B0
static void map_iuser_Map(Input* input, int32_t time) {
	/* Briefing mode: offset button position during panel animation */
	if (shellext_Get_Cur_Scene() == SCENE_BRIEF_MAP && time < 5) {
		if (time)
			xrect_Offset_Rect(&input->frame, 0, map_panel_y[time] - map_panel_y[time - 1]);
		else
			xrect_Offset_Rect(&input->frame, 0, map_panel_y[0] - map_panel_y[4]);
	}

	/* Auto-advance the VR talk paragraph in lockstep with the speech.
	 * Retail does this at the head of MAP_iuser_Map, gated by the
	 * talk-mode flag (HIWORD(dword_F6136)). */
	if (talk_mode && time > talk_paragraph_timer) {
		uint32_t next_timer = (uint32_t)talk_paragraph_timer + 264u;
		if (next_timer & 0x80000000u)
			talk_paragraph_timer = 0x7FFFFFFF;
		else
			talk_paragraph_timer = (int32_t)next_timer;
		if (++cur_talk_paragraph >= num_talk_paragraphs) {
			talk_paragraph_timer = 0x7FFFFFFF;
			cur_talk_paragraph = 0;
		}
	}

	if (!xinpattr_Get_Input_Selected(input))
		return;

	switch (input->id) {
		case 0: /* Stop / Prev Question */
			if (talk_mode) {
				if (num_talk_questions) {
					if (cur_talk_question)
						cur_talk_question--;
					else
						cur_talk_question = num_talk_questions - 1;
					map_Set_VR_Talk_Paragraph();
					if (options_gbl.speech_active)
						talk_paragraph_timer = time + 264;
					talk_voice_question = (int16_t)(cur_talk_question + 1);
					talk_Start_Speech_Stream();
				}
			} else {
				if (player_Is_Map_Playing())
					player_Toggle_Map_Play();
				else
					player_Rewind_Page();
				xinpattr_Refresh_Input(play_input);
			}
			break;

		case 1: /* Play / Next Question */
			if (talk_mode) {
				if (num_talk_questions) {
					if (cur_talk_question >= num_talk_questions - 1)
						cur_talk_question = 0;
					else
						cur_talk_question++;
					map_Set_VR_Talk_Paragraph();
					if (options_gbl.speech_active)
						talk_paragraph_timer = time + 264;
					talk_voice_question = (int16_t)(cur_talk_question + 1);
					talk_Start_Speech_Stream();
				}
			} else {
				if (player_Is_Map_Playing()) {
					player_Rewind_Page();
					xinpattr_Refresh_Input(play_input);
				} else {
					player_Toggle_Map_Play();
					xinpattr_Refresh_Input(stop_input);
				}
			}
			break;

		case 2: /* Skip / Next Page */
			if (talk_mode) {
				if (xio_Right_Button_Release()) {
					/* Right-click: previous page; disable auto-advance */
					talk_paragraph_timer = 0x7FFFFFFF;
					if (--cur_talk_paragraph < 0)
						cur_talk_paragraph = num_talk_paragraphs - 1;
				} else {
					/* Left-click: bump auto-advance threshold (264 ms) */
					uint32_t next_timer = (uint32_t)talk_paragraph_timer + 264u;
					if (next_timer & 0x80000000u)
						talk_paragraph_timer = 0x7FFFFFFF;
					else
						talk_paragraph_timer = (int32_t)next_timer;
					if (++cur_talk_paragraph == num_talk_paragraphs) {
						talk_paragraph_timer = 0x7FFFFFFF;
						cur_talk_paragraph = 0;
					}
				}
			} else {
				player_Seek_Page_Section();
			}
			break;

		case 3: /* View Officer/Priest / Map */
			talk_mode = next_mode;
			next_mode = (next_mode + 1) % 3;
			/* Adjust next_mode to skip unavailable officer/priest */
			if (!next_mode) {
				if (shellext_Get_Cur_Scene() == SCENE_COMBAT_MAP_B) {
					next_mode = (shipext_Get_Mission_Officer() == 2) ? 2 : 1;
				}
			} else if (next_mode == 1) {
				if (shipext_Get_Mission_Officer() == 2)
					next_mode = 2;
			} else if (next_mode == 2) {
				/* mission_officer == 1: skip priest. Go to officer (1) when
				 * we're in COMBAT_MAP_B; otherwise to map (0). Binary @ 0x75490. */
				if (shipext_Get_Mission_Officer() == 1)
					next_mode = (shellext_Get_Cur_Scene() == SCENE_COMBAT_MAP_B) ? 1 : 0;
			}

			if (talk_mode == 0) {
				talk_voice_officer = 'i';
				last_voiced_paragraph = 0;
				player_Rewind_Page();
				xinpattr_Show_Input(map_input);
				xinpattr_Hide_Input(talk_input);
				talk_paragraph_timer = 0x7FFFFFFF;
			} else if (talk_mode == 1) {
				xinpattr_Hide_Input(map_input);
				xinpattr_Show_Input(talk_input);
				talk_voice_officer = 'o';
				talk_voice_question = 1;
				if (map_is_post_mission) {
					talk_voice_question = 0;
					talk_voice_mood = (mission.primary_complete == 1) ? 'd' : 'h';
				}
				map_Set_VR_Talk_To_Text(0);
				if (options_gbl.speech_active)
					talk_paragraph_timer = time + 264;
			} else if (talk_mode == 2) {
				xinpattr_Hide_Input(map_input);
				xinpattr_Show_Input(talk_input);
				talk_voice_officer = 'p';
				talk_voice_question = 1;
				if (map_is_post_mission) {
					talk_voice_question = 0;
					talk_voice_mood = (mission.secondary_complete == 1) ? 'd' : 'h';
				}
				map_Set_VR_Talk_To_Text(1);
				if (options_gbl.speech_active)
					talk_paragraph_timer = time + 264;
			}
			xview_Refresh_View();
			break;

		case 4: /* Enter Mission / Exit Debrief */
			if (shellext_Get_Cur_Scene() >= SCENE_COMBAT_MAP_A &&
				shellext_Get_Cur_Scene() <= SCENE_COMBAT_MAP_B) {
				xerror_Set_Landru_Exit(SCENE_FLIGHT_COMBAT);
				shipext_Update_Pilot();
			} else if (shellext_Get_Cur_Scene() == SCENE_BRIEF_MAP) {
				xerror_Set_Landru_Exit(SCENE_BRIEF);
			}
			soundext_Stop_SFX(sfxText);
			break;

		case 5: /* Exit */
			if (shellext_Get_Cur_Scene() == SCENE_TRAIN_MAP)
				xerror_Set_Landru_Exit(SCENE_TRAIN_B);
			else if (shellext_Get_Cur_Scene() >= SCENE_COMBAT_MAP_A &&
					 shellext_Get_Cur_Scene() <= SCENE_COMBAT_MAP_B)
				xerror_Set_Landru_Exit(SCENE_COMBAT_B);
			else if (shellext_Get_Cur_Scene() == SCENE_BRIEF_MAP)
				xerror_Set_Landru_Exit(SCENE_BRIEF);
			soundext_Stop_SFX(sfxText);
			break;
	}
}

// FUNCTION: TIE95 0x75684
// FUNCTION: TIE98 0x44EB20
static void map_idraw_Map(Input* input, Rect* r, Rect* clip_r, int16_t refresh) {
	PushButton* btn;
	int16_t down;
	int16_t x;
	int16_t y;
	int16_t id, down_id;

	if (!refresh)
		return;

	btn = (PushButton*)input;
	down = 0;
	x = cmbticons->x;
	y = cmbticons->y;

	if (shellext_Get_Cur_Scene() == SCENE_BRIEF_MAP) {
		y += input->frame.top - 175;
		if (input->id == 0 && !player_Is_Map_Playing())
			down = 1;
		if (input->id == 1 && player_Is_Map_Playing())
			down = 1;
		id = (input->id == 5) ? 3 : input->id;
		down_id = id + 4;
	} else {
		if (!talk_mode) {
			if (input->id == 0 && !player_Is_Map_Playing())
				down = 1;
			if (input->id == 1 && player_Is_Map_Playing())
				down = 1;
		}
		if (talk_mode) {
			switch (input->id) {
				case 0:
					id = 12;
					down_id = 15;
					break;
				case 2:
					id = 13;
					down_id = 16;
					break;
				case 3:
					if (!next_mode) {
						id = 14;
						down_id = 17;
					} else {
						id = input->id;
						down_id = id + 6;
					}
					break;
				default:
					id = input->id;
					down_id = id + 6;
					break;
			}
		} else {
			id = input->id;
			down_id = id + 6;
		}
	}

	if (btn->pressed || down)
		xactor_Set_Actor_State(cmbticons, down_id, 0);
	else
		xactor_Set_Actor_State(cmbticons, id, 0);

	xactanim_Draw_Anim_Actor(cmbticons, r, clip_r, x, y, refresh);

	if (xinpattr_Is_Input_Dirty(input))
		xdirty_Dirty_Rect(clip_r);
}

/* ======================================================================
 * Panel actor callbacks
 * ====================================================================== */

// FUNCTION: TIE95 0x7581C
// FUNCTION: TIE98 0x44EC90
static int16_t map_user_Map_Panel(Actor* actor, int32_t time) {
	switch (actor->id) {
		case 0:
			actor->y = (time > 4) ? map_panel_y[4] : map_panel_y[time];
			break;
		case 1: {
			int16_t y;
			int16_t cel;

			if (time > 4) {
				cel = map_panel_hdl_cel[4];
				y = map_panel_hdl_y[4];
			} else {
				cel = map_panel_hdl_cel[time];
				y = map_panel_hdl_y[time];
			}
			actor->y = y;
			xactor_Set_Actor_State(actor, cel, 0);
			break;
		}
	}
	return 1;
}

// FUNCTION: TIE95 0x7588C
// FUNCTION: TIE98 0x44ED10
static int16_t map_draw_Map_Text(Actor* actor, Rect* r, Rect* clip_r, int16_t x, int16_t y, int16_t refresh) {
	xactdelt_Draw_Delta_Actor(actor, r, clip_r, x, y, refresh);

	if (map_text != -1) {
		Rect tr;
		char name[48];

		xactdelt_Draw_Delta_Actor(title_actor, r, clip_r, x, y, refresh);

		xrect_Set_Rect(&tr, 60, 158, 260, 170);
		textext_Copy_Text(name, map_text);
		xfont_Enable_FontID_Shadow(0);
		xfont_Print_Centered_Text(name, &tr, 0, 28);
		xfont_Disable_FontID_Shadow(0);
	}

	if (map_text_count <= 0)
		map_text = -1;
	else
		map_text_count--;

	return 1;
}

/* ======================================================================
 * map_idraw_Talk — talk text overlay in MAP mode
 * ====================================================================== */

// FUNCTION: TIE95 0x75960
// FUNCTION: TIE98 0x44EDF0
static void map_idraw_Talk(Input* input, Rect* r, Rect* clip_r, int16_t refresh) {
	char buf[64], str1[32], fmt[32];
	Rect tr;

	xrect_Copy_Rect(&tr, r);
	player_Stars_To_Back(r->top + 1);

	if (shellext_Get_Cur_Scene() != SCENE_TRAIN_MAP) {
		/* Combat/briefing talk text display */
		int16_t first_line;
		int16_t saved_bold;
		int16_t i;

		xfont_Enable_FontID_Shadow(0);
		tr.top += 2;
		tr.bottom = tr.top + 10;

		/* Officer/priest title + question indicator */
		textext_Copy_Text(buf, talk_person + txtMapBriefOfficer);
		textext_Copy_Text(fmt, txtMapQuestion);
		if (shellext_Get_Cur_Scene() == SCENE_COMBAT_MAP_B) {
			if (cur_talk_question == num_talk_questions - 1)
				sprintf(str1, fmt, 1, num_talk_questions);
			else
				sprintf(str1, fmt, cur_talk_question + 2, num_talk_questions);
		} else {
			sprintf(str1, fmt, cur_talk_question + 1, num_talk_questions);
		}
		strcat(buf, str1);
		xfont_Print_Centered_Text(buf, &tr, 0, 14);

		xpaint_Horiz_Clipped_Line(tr.left + 8, tr.bottom + 1, tr.right - tr.left - 16, 24);
		tr.top += 14;
		tr.bottom = tr.top + 10;

		/* Current question text */
		map_Get_VR_Talk_Question(buf, cur_talk_question);
		xfont_Print_Centered_Text(buf, &tr, 0, 15);
		xrect_Offset_Rect(&tr, 0, 10);

		/* Paragraph text */
		first_line = cur_talk_paragraph * max_paragraph_size;
		tr.top += 2;
		xrect_Inset_Rect(&tr, 46, 0);
		tr.bottom = tr.top + 78;
		xpaint_Horiz_Clipped_Line(r->left + 8, tr.top, r->right - r->left - 16, 24);
		tr.top += 4;
		tr.bottom = tr.top + 10;

		saved_bold = xfont_Get_FontID_Bold_Color(0);
		xfont_Set_FontID_Bold_Color(0, 231);

		for (i = first_line; i < max_paragraph_size + first_line; i++) {
			center_line = 0;
			map_Get_VR_Talk_Paragraph(buf, i);
			if (center_line)
				xfont_Print_Centered_Text(buf, &tr, 0, 228);
			else
				xfont_Print_Clipped_Text(buf, tr.left + 8, tr.top, 0, 228);
			xrect_Offset_Rect(&tr, 0, 10);
		}

		xfont_Set_FontID_Bold_Color(0, saved_bold);

		/* Page indicator */
		strcpy(str1, textext_Get_Text(txtMapPage));
		strcpy(fmt, textext_Get_Text(txtMapOf));
		sprintf(buf, "%s %d %s %d", str1, cur_talk_paragraph + 1, fmt, num_talk_paragraphs);
		xfont_Print_Clipped_Text(buf, r->right - 80, r->bottom - 10, 0, 24);
	} else {
		/* Training stats display */
		int16_t saved_bold;
		int16_t j;

		xfont_Enable_FontID_Shadow(0);
		saved_bold = xfont_Get_FontID_Bold_Color(0);
		xfont_Set_FontID_Bold_Color(0, 231);

		tr.top += 40;
		tr.bottom = tr.top + 10;

		shipext_Get_Ship_Name(buf, shipext_Get_Train_Ship(), 0, 0);
		textext_Copy_Text(fmt, txtMapTrainLevel);
		sprintf(str1, fmt, mission.train_level);
		strcat(buf, str1);
		xfont_Print_Centered_Text(buf, &tr, 0, 228);

		xrect_Offset_Rect(&tr, 0, 14);
		xpaint_Horiz_Clipped_Line(tr.left + 48, tr.top - 3, tr.right - tr.left - 96, 231);

		textext_Copy_Text(fmt, txtMapTrainScore);
		sprintf(buf, fmt, mission.mission_score);
		xfont_Print_Centered_Text(buf, &tr, 0, 228);
		xrect_Offset_Rect(&tr, 0, 10);

		if (mission.mission_new_rank) {
			textext_Copy_Text(str1, txtTalkRank);
			textext_Copy_Text(fmt, mission.mission_new_rank + 1);
			for (j = 0; str1[j]; j++) {
				if ((int8_t)str1[j] == '1')
					str1[j] = 1;
				if ((int8_t)str1[j] == '2')
					str1[j] = 2;
			}
			sprintf(buf, str1, fmt);
			xfont_Print_Centered_Text(buf, &tr, 0, 228);
			xrect_Offset_Rect(&tr, 0, 10);
		}

		if (train_pilot_medal_status) {
			textext_Copy_Text(buf, txtTalkTrainPatch);
			for (j = 0; buf[j]; j++) {
				if ((int8_t)buf[j] == '1')
					buf[j] = 1;
				if ((int8_t)buf[j] == '2')
					buf[j] = 2;
			}
			xfont_Print_Centered_Text(buf, &tr, 0, 228);
			xrect_Offset_Rect(&tr, 0, 10);
		}

		textext_Copy_Text(fmt, txtMapTrainPassed);
		sprintf(buf, fmt, (uint16_t)mission.train_gates_passed);
		xfont_Print_Centered_Text(buf, &tr, 0, 228);
		xrect_Offset_Rect(&tr, 0, 10);

		textext_Copy_Text(fmt, txtMapTrainRemain);
		sprintf(buf, fmt, (uint16_t)mission.train_gates_remaining);
		xfont_Print_Centered_Text(buf, &tr, 0, 228);
		xrect_Offset_Rect(&tr, 0, 10);

		textext_Copy_Text(fmt, txtMapTrainTargets);
		sprintf(buf, fmt, (uint16_t)mission.train_targets);
		xfont_Print_Centered_Text(buf, &tr, 0, 228);

		if (mission.training_badge_earned) {
			xrect_Offset_Rect(&tr, 0, 10);
			textext_Copy_Text(buf, txtMapTrainBadge);
			xfont_Print_Centered_Text(buf, &tr, 0, 228);
		}

		xfont_Set_FontID_Bold_Color(0, saved_bold);
	}

	xfont_Disable_FontID_Shadow(0);

	if (xinpattr_Is_Input_Dirty(input))
		xdirty_Dirty_Rect(clip_r);
}

// FUNCTION: TIE95 0x76074
// FUNCTION: TIE98 0x44F530
static void map_Set_VR_Talk_To_Text(int16_t person) {
	int16_t i;

	num_talk_questions = 0;
	cur_talk_question = 0;
	num_talk_paragraphs = 0;
	cur_talk_paragraph = 0;
	talk_person = person;

	if (shellext_Get_Cur_Scene() == SCENE_COMBAT_MAP_B)
		talk_person += 2;

	map_Check_VR_Talk_Questions();

	for (i = 0; i < 5; i++) {
		int16_t show = 0;
		switch (talk_win_status[i]) {
			case 1:
				show = 1;
				break;
			case 2:
				show = (mission.primary_complete == 1);
				break;
			case 3:
				show = (mission.secondary_complete == 1);
				break;
			case 4:
				show = (mission.primary_complete != 1);
				break;
			case 5:
				show = (mission.secondary_complete != 1);
				break;
		}
		if (show)
			talk_win_id[num_talk_questions++] = i;
	}

	if (shellext_Get_Cur_Scene() == SCENE_COMBAT_MAP_B) {
		if (num_talk_questions == 5)
			talk_win_id[4] = 5;
		else
			talk_win_id[num_talk_questions++] = 5;
		cur_talk_question = num_talk_questions - 1;
		num_talk_paragraphs = map_Count_VR_Debrief_Pages();
		cur_talk_paragraph = 0;
	}

	map_Set_VR_Talk_Paragraph();

	/* Retail MAP_Set_VR_Talk_To_Text fires the current paragraph's
	 * voice file at the end (gated on officer != 'i' since the
	 * info-briefing path is driven by map_end_View instead). Lets the
	 * combat-sim scenes hear the first phrase on entry. */
	if (talk_voice_officer != 'i')
		talk_Start_Speech_Stream();
}
/* TIE95 has no separate body: its callers use map_Count_VR_Debrief_Header's
 * identical code at 0x769DC. */
// FUNCTION: TIE98 0x450280
static int16_t map_Count_VR_Debrief_Goals(void) { return 1; }

// FUNCTION: TIE95 0x761C0
// FUNCTION: TIE98 0x44F660
static void map_Set_VR_Talk_Paragraph(void) {
	int8_t* data;
	int16_t pos;
	int16_t line_count;

	if (cur_talk_question < 0 || cur_talk_question >= num_talk_questions)
		return;

	if (talk_win_id[cur_talk_question] != 5) {
		data = (int8_t*)xmemhdl_Lock_Handle(
			talk_brief->talk_data[5 * talk_person + talk_win_id[cur_talk_question]]);
		line_count = 0;

		pos = 0;
		while (data[pos] && data[pos] != '\n')
			pos++;
		pos++;

		while (data[pos]) {
			while (data[pos] && data[pos] != '\n')
				pos++;
			if (data[pos])
				pos++;
			line_count++;
		}

		xmemhdl_Unlock_Handle(talk_brief->talk_data[5 * talk_person + talk_win_id[cur_talk_question]]);
		num_talk_paragraphs = (max_paragraph_size + line_count - 1) / max_paragraph_size;
		cur_talk_paragraph = 0;
	} else {
		num_talk_paragraphs = map_Count_VR_Debrief_Pages();
		cur_talk_paragraph = 0;
	}
}

/* ======================================================================
 * VR talk text functions
 * ====================================================================== */

// FUNCTION: TIE95 0x762F4
// FUNCTION: TIE98 0x44F770
static void map_Get_VR_Talk_Question(char* string, int16_t question) {
	int8_t* data;
	int16_t out_len;
	int16_t i;

	*string = '\0';
	if (question < 0 || question >= num_talk_questions)
		return;

	if (talk_win_id[question] != 5) {
		if (!talk_brief->talk_data[5 * talk_person + talk_win_id[question]])
			return;

		data = (int8_t*)xmemhdl_Lock_Handle(talk_brief->talk_data[5 * talk_person + talk_win_id[question]]);
		out_len = 0;
		i = 0;
		while (data[i] && data[i] != '\n') {
			if (data[i] == 4 || data[i] == 5)
				i++;
			else
				string[out_len++] = data[i];
			i++;
		}
		string[out_len] = '\0';
		xmemhdl_Unlock_Handle(talk_brief->talk_data[5 * talk_person + talk_win_id[question]]);
	} else {
		textext_Copy_Text(string, txtTalkDebrief);
	}
}

// FUNCTION: TIE95 0x76400
// FUNCTION: TIE98 0x44F860
static void map_Get_VR_Talk_Paragraph(char* string, int16_t line) {
	int16_t bold = 0;
	int16_t qid;
	char* data;
	int16_t pos;
	int16_t line_idx;

	*string = '\0';

	if (cur_talk_question < 0 || cur_talk_question >= num_talk_questions)
		return;

	qid = talk_win_id[cur_talk_question];
	if (qid == 5) {
		map_Get_VR_Debrief_Line(string, line);
		return;
	}

	data = (char*)xmemhdl_Lock_Handle(talk_brief->talk_data[5 * talk_person + qid]);
	if (!data)
		return;

	/* Skip first line (question title) */
	pos = 0;
	while (data[pos] && data[pos] != '\n')
		pos++;
	pos++;

	line_idx = 0;
	while (data[pos] && line_idx <= line) {
		if (line_idx == line) {
			int16_t out_len = 0;
			if (bold) {
				out_len = 1;
				string[0] = 2;
			}
			while (data[pos] && data[pos] != '\n') {
				if (data[pos] == 3)
					pos += 2; /* skip mood code (not applied in MAP) */
				else
					string[out_len++] = data[pos++];
			}
			string[out_len] = '\0';
		} else {
			while (data[pos] && data[pos] != '\n') {
				if (data[pos] == 2)
					bold = 1;
				if (data[pos] == 1)
					bold = 0;
				if (data[pos] == 3)
					pos += 2;
				else
					pos++;
			}
		}
		if (data[pos])
			pos++;
		line_idx++;
	}
	xmemhdl_Unlock_Handle(talk_brief->talk_data[5 * talk_person + qid]);
}

/* ======================================================================
 * map_Check_VR_Talk_Questions — scan talk data visibility conditions
 * ====================================================================== */

// FUNCTION: TIE95 0x765B8
// FUNCTION: TIE98 0x44F9F0
static void map_Check_VR_Talk_Questions(void) {
	int16_t i;

	for (i = 0; i < 5; i++) {
		int16_t status = 0;
		if (talk_brief->talk_data[5 * talk_person + (unsigned)i]) {
			int8_t* data = (int8_t*)xmemhdl_Lock_Handle(talk_brief->talk_data[5 * talk_person + (unsigned)i]);
			if (data[0]) {
				int16_t j;

				status = 1;
				for (j = 0;; j++) {
					if (!data[j] || data[j] == '\n')
						break;
					if (data[j] == 4 || data[j] == 5) {
						data += j;
						if (data[0] == 4)
							status = data[1] + 1;
						else
							status = data[1] + 3;
						break;
					}
				}
			}
			xmemhdl_Unlock_Handle(talk_brief->talk_data[5 * talk_person + (unsigned)i]);
		}
		talk_win_status[i] = status;
	}
}

// FUNCTION: TIE95 0x766A4
// FUNCTION: TIE98 0x44FAB0
static int16_t map_Count_VR_Debrief_Pages(void) {
	return map_Count_VR_Debrief_Header() + map_Count_VR_Debrief_Goals() + map_Count_VR_Debrief_Kills() +
		   map_Count_VR_Debrief_Losses() + map_Count_VR_Debrief_Captures();
}

/* --- Debrief line dispatcher --- */

// FUNCTION: TIE95 0x766E0
// FUNCTION: TIE98 0x44FAE0
static void map_Get_VR_Debrief_Line(char* string, int16_t line) {
	int16_t abs_line = line;
	int16_t section_page = line / max_paragraph_size;
	int16_t s;

	*string = '\0';
	center_line = 0;

	for (s = 0; s < 5 && section_page >= 0; s++) {
		int16_t pages;
		switch (s) {
			case 0:
				pages = map_Count_VR_Debrief_Header();
				if (section_page < pages)
					map_Find_VR_Debrief_Header(string, abs_line);
				break;
			case 1:
				pages = map_Count_VR_Debrief_Goals();
				if (section_page < pages)
					map_Find_VR_Debrief_Goals(string, abs_line);
				break;
			case 2:
				pages = map_Count_VR_Debrief_Kills();
				if (section_page < pages)
					map_Find_VR_Debrief_Kills(string, section_page, abs_line);
				break;
			case 3:
				pages = map_Count_VR_Debrief_Losses();
				if (section_page < pages)
					map_Find_VR_Debrief_Losses(string, section_page, abs_line);
				break;
			case 4:
				pages = map_Count_VR_Debrief_Captures();
				if (section_page < pages)
					map_Find_VR_Debrief_Captures(string, section_page, abs_line);
				break;
		}
		section_page -= pages;
		abs_line -= pages * max_paragraph_size;
	}
}

/* --- Standard header (shared by header + goals Find functions) --- */

// FUNCTION: TIE95 0x767F8
// FUNCTION: TIE98 0x44FC40
static void map_Get_VR_Standard_Debrief_Header(char* string, int16_t line) {
	char buf[80], fmt[40];
	uint16_t j;

	switch (line) {
		case 0: {
			int16_t ship = shipext_Get_Mission_Ship();
			shipext_Get_Ship_Name(buf, ship, 0, 0);
			strcpy(string, buf);
			textext_Cat_Text(string, mission.difficulty + txtTalkEasy);
			center_line = 1;
			break;
		}
		case 1: {
			uint8_t combat_ship = shipext_Get_Combat_Ship();
			if (combat_ship < 12) {
				textext_Copy_Text(fmt, txtMapHistorical);
				for (j = 0; fmt[j]; j++) {
					if (fmt[j] == '1')
						fmt[j] = 1;
					if (fmt[j] == '2')
						fmt[j] = 2;
				}
				snprintf(buf, sizeof(buf), fmt, shipext_Get_Combat_Mission() + 1);
			} else {
				textext_Copy_Text(fmt, txtTalkBattle);
				for (j = 0; fmt[j]; j++) {
					if (fmt[j] == '1')
						fmt[j] = 1;
					if (fmt[j] == '2')
						fmt[j] = 2;
				}
				snprintf(buf, sizeof(buf), fmt, combat_ship - 11, shipext_Get_Combat_Mission() + 1);
			}
			strcpy(string, buf);
			textext_Copy_Text(fmt, txtTalkScore);
			for (j = 0; fmt[j]; j++) {
				if (fmt[j] == '1')
					fmt[j] = 1;
				if (fmt[j] == '2')
					fmt[j] = 2;
			}
			snprintf(buf, sizeof(buf), fmt, mission.mission_score);
			strcat(string, buf);
			center_line = 1;
			break;
		}
		case 2:
			textext_Copy_Text(string, txtTalkDash);
			center_line = 1;
			break;
	}
}

/* ======================================================================
 * VR debrief — count/find/get for all 5 sections
 * ====================================================================== */

// FUNCTION: TIE95 0x769DC
// FUNCTION: TIE98 0x44FBE0
static int16_t map_Count_VR_Debrief_Header(void) { return 1; }

// FUNCTION: TIE95 0x769E4
// FUNCTION: TIE98 0x44FBF0
static void map_Find_VR_Debrief_Header(char* string, int16_t line) {
	int16_t skip = line;
	int16_t i;

	for (i = 0; i < max_paragraph_size; i++) {
		if (i >= 3)
			map_Get_VR_Debrief_Header(string, i);
		else
			map_Get_VR_Standard_Debrief_Header(string, i);
		if (*string) {
			if (!skip)
				return;
			*string = '\0';
			skip--;
		}
	}
}

/* --- Header section --- */

// FUNCTION: TIE95 0x76A50
// FUNCTION: TIE98 0x44FE80
static void map_Get_VR_Debrief_Header(char* string, int16_t line) {
	char buf[80], fmt[40], rank_name[40];
	uint16_t val;
	uint16_t j;

	switch (line) {
		case 3:
			if (shipext_Is_Combat_Mission_Success())
				textext_Copy_Text(buf, txtTalkSuccess);
			else
				textext_Copy_Text(buf, txtTalkFailure);
			for (j = 0; buf[j]; j++) {
				if (buf[j] == '1')
					buf[j] = 1;
				if (buf[j] == '2')
					buf[j] = 2;
			}
			strcpy(string, buf);
			return;
		case 4:
			if (!mission.mission_new_rank)
				return;
			textext_Copy_Text(fmt, txtTalkRank);
			textext_Copy_Text(rank_name, mission.mission_new_rank + 1);
			for (j = 0; fmt[j]; j++) {
				if (fmt[j] == '1')
					fmt[j] = 1;
				if (fmt[j] == '2')
					fmt[j] = 2;
			}
			snprintf(buf, sizeof(buf), fmt, rank_name);
			strcpy(string, buf);
			return;
		case 5:
			if (shellext_Get_Cur_Scene() == SCENE_COMBAT_MAP_B) {
				if (!combat_pilot_medal_status)
					return;
				if (combat_pilot_medal_status <= 1)
					textext_Copy_Text(buf, txtTalkMedallion);
				else
					textext_Copy_Text(buf, txtTalkNewMedallion);
			} else {
				if (!mission.mission_medal)
					return;
				textext_Copy_Text(buf, txtTalkMedal);
			}
			for (j = 0; buf[j]; j++) {
				if (buf[j] == '1')
					buf[j] = 1;
				if (buf[j] == '2')
					buf[j] = 2;
			}
			strcpy(string, buf);
			return;
		case 6:
			textext_Copy_Text(string, txtTalkDash);
			center_line = 1;
			return;
		case 7:
			val = pstate.player_laser_hit ? 100 * pstate.player_laser_hit / pstate.player_laser_fired : 0;
			textext_Copy_Text(fmt, txtCompInfoLaser);
			snprintf(buf, sizeof(buf), fmt, pstate.player_laser_hit, pstate.player_laser_fired, val);
			strcpy(string, buf);
			return;
		case 8:
			val =
				pstate.player_missile_hit ? 100 * pstate.player_missile_hit / pstate.player_missile_fired : 0;
			textext_Copy_Text(fmt, txtCompInfoIon);
			snprintf(buf, sizeof(buf), fmt, pstate.player_missile_hit, pstate.player_missile_fired, val);
			strcpy(string, buf);
			return;
		case 9:
			if (pstate.player_warhead_hit > pstate.player_warhead_fired)
				pstate.player_warhead_fired = pstate.player_warhead_hit;
			val =
				pstate.player_warhead_hit ? 100 * pstate.player_warhead_hit / pstate.player_warhead_fired : 0;
			textext_Copy_Text(fmt, txtCompInfoRocket);
			snprintf(buf, sizeof(buf), fmt, pstate.player_warhead_hit, pstate.player_warhead_fired, val);
			strcpy(string, buf);
			return;
	}
}

// FUNCTION: TIE95 0x76DC0
// FUNCTION: TIE98 0x450290
static void map_Find_VR_Debrief_Goals(char* string, int16_t line) {
	int16_t skip = line;
	int16_t i;

	for (i = 0; i < max_paragraph_size; i++) {
		if (i < 3)
			map_Get_VR_Standard_Debrief_Header(string, i);
		else
			map_Get_VR_Debrief_Goals(string, i);
		if (*string) {
			if (!skip)
				return;
			*string = '\0';
			skip--;
		}
	}
}

/* --- Goals section --- */

// FUNCTION: TIE95 0x76E10
// FUNCTION: TIE98 0x4502E0
static void map_Get_VR_Debrief_Goals(char* string, int16_t line) {
	char buf[80], fmt[40], count_str[40];
	int16_t done, fail;

	switch (line) {
		case 3:
			done = goalsCompletedCount[0];
			fail = goalsCount[0] - done;
			if (done || fail) {
				if (mission.primary_complete == 1) {
					textext_Copy_Text(string, txtTalkAllPri);
				} else {
					if (!done)
						textext_Copy_Text(count_str, txtTalkNo);
					else {
						textext_Copy_Text(fmt, txtTalkOf);
#ifdef TIE_MODERN
						snprintf(count_str, sizeof(count_str), fmt, done, done + fail);
#else
						sprintf(count_str, fmt, done, done + fail);
#endif
					}
					textext_Copy_Text(fmt, txtTalkSomePri);
#ifdef TIE_MODERN
					snprintf(buf, sizeof(buf), fmt, count_str);
#else
					sprintf(buf, fmt, count_str);
#endif
					strcpy(string, buf);
				}
			} else
				*string = '\0';
			break;
		case 4:
			done = goalsCompletedCount[1];
			fail = goalsCount[1] - done;
			if (done || fail) {
				if (mission.secondary_complete == 1) {
					textext_Copy_Text(string, txtTalkAllSec);
				} else {
					if (!done)
						textext_Copy_Text(count_str, txtTalkNo);
					else {
						textext_Copy_Text(fmt, txtTalkOf);
#ifdef TIE_MODERN
						snprintf(count_str, sizeof(count_str), fmt, done, done + fail);
#else
						sprintf(count_str, fmt, done, done + fail);
#endif
					}
					textext_Copy_Text(fmt, txtTalkSomeSec);
#ifdef TIE_MODERN
					snprintf(buf, sizeof(buf), fmt, count_str);
#else
					sprintf(buf, fmt, count_str);
#endif
					strcpy(string, buf);
				}
			} else
				*string = '\0';
			break;
		case 5:
			done = goalsCompletedCount[2];
			fail = goalsCount[2] - done;
			if (done || fail) {
				if (mission.bonus_complete == 1) {
					textext_Copy_Text(string, txtTalkAllBonus);
				} else {
					if (!done)
						textext_Copy_Text(count_str, txtTalkNo);
					else {
						textext_Copy_Text(fmt, txtTalkOf);
#ifdef TIE_MODERN
						snprintf(count_str, sizeof(count_str), fmt, done, done + fail);
#else
						sprintf(count_str, fmt, done, done + fail);
#endif
					}
					textext_Copy_Text(fmt, txtTalkSomeBonus);
#ifdef TIE_MODERN
					snprintf(buf, sizeof(buf), fmt, count_str);
#else
					sprintf(buf, fmt, count_str);
#endif
					strcpy(string, buf);
				}
			} else
				*string = '\0';
			break;
	}
	center_line = 1;
}

// FUNCTION: TIE95 0x770BC
// FUNCTION: TIE98 0x450590
static int16_t map_Count_VR_Debrief_Kills(void) {
	int16_t count = 0;
	int16_t i;

	for (i = 0; i < (int16_t)NUM_SPEC; i++) {
		int16_t j;
		int16_t found = 0;

		for (j = 0; j < 6; j++) {
			if (player_Is_Side_Enemy(j) && mission.kills_losses[j][i])
				found = 1;
		}
		if (found)
			count++;
	}
	if (pstate.player_total_kills)
		count++;
	return (count + max_paragraph_size - 3) / (max_paragraph_size - 2);
}

// FUNCTION: TIE95 0x77148
// FUNCTION: TIE98 0x450620
static void map_Find_VR_Debrief_Kills(char* string, int16_t page, int16_t line) {
	int16_t in_page = line % max_paragraph_size;
	if (in_page == 0) {
		map_Get_VR_Debrief_Kill_Title(string);
	} else if (in_page == 1) {
		textext_Copy_Text(string, txtTalkDash);
		center_line = 1;
	} else {
		int16_t skip = line - 2 * (page + 1);
		int16_t i;

		for (i = 0; i <= (int16_t)NUM_SPEC; i++) {
			map_Get_VR_Debrief_Kills(string, i);
			if (*string) {
				if (!skip)
					return;
				skip--;
				*string = '\0';
			}
		}
	}
}

/* --- Kills section --- */

// FUNCTION: TIE95 0x771BC
// FUNCTION: TIE98 0x4506B0
static void map_Get_VR_Debrief_Kill_Title(char* string) {
	uint16_t total = 0, player_total = 0;
	char fmt[40], buf[80];
	uint16_t i;
	uint16_t j;

	for (i = 0; i < NUM_SPEC; i++) {
		for (j = 0; j < 6; j++) {
			if (player_Is_Side_Enemy(j))
				total += mission.kills_losses[j][i];
		}
		player_total += pstate.player_kills_per_species[i];
	}
	textext_Copy_Text(fmt, txtTalkDestroyed);
	for (j = 0; fmt[j]; j++) {
		if (fmt[j] == '1')
			fmt[j] = 1;
		if (fmt[j] == '2')
			fmt[j] = 2;
	}
	snprintf(buf, sizeof(buf), fmt, total, player_total);
	strcpy(string, buf);
	center_line = 1;
}

// FUNCTION: TIE95 0x772AC
// FUNCTION: TIE98 0x4507C0
static void map_Get_VR_Debrief_Kills(char* string, int16_t craft_idx) {
	uint16_t count = 0;
	char name[40], buf[80];
	if (craft_idx < (int16_t)NUM_SPEC) {
		int16_t j;

		for (j = 0; j < 6; j++) {
			if (player_Is_Side_Enemy(j))
				count += mission.kills_losses[j][craft_idx];
		}
	} else {
		count = pstate.player_total_kills;
	}
	if (count) {
		if (craft_idx < (int16_t)NUM_SPEC) {
			textext_Get_Ship_Text(name, craft_idx);
			sprintf(buf, "  %s: %d(%d)", name, count, pstate.player_kills_per_species[craft_idx]);
		} else {
			textext_Get_Ship_Text(name, 84);
			sprintf(buf, "  %s: %d", name, count);
		}
		strcpy(string, buf);
	}
}

// FUNCTION: TIE95 0x77378
// FUNCTION: TIE98 0x4508D0
static int16_t map_Count_VR_Debrief_Losses(void) {
	int16_t count = 0;
	int16_t i;

	for (i = 0; i < (int16_t)NUM_SPEC; i++) {
		int16_t j;

		for (j = 0; j < 6; j++) {
			if (!player_Is_Side_Enemy(j) && mission.kills_losses[j][i]) {
				count++;
				break;
			}
		}
	}
	return (count + max_paragraph_size - 3) / (max_paragraph_size - 2);
}

// FUNCTION: TIE95 0x773F8
// FUNCTION: TIE98 0x450950
static void map_Find_VR_Debrief_Losses(char* string, int16_t page, int16_t line) {
	int in_page = line % max_paragraph_size;
	int16_t i;

	if (in_page < 2) {
		if (in_page != 0) {
			textext_Copy_Text(string, txtTalkDash);
			center_line = 1;
		} else {
			map_Get_VR_Debrief_Loss_Title(string);
		}
		return;
	}
	i = 0;
	line -= 2 * (page + 1);
	while (i < NUM_SPEC) {
		map_Get_VR_Debrief_Losses(string, i);
		if (*string) {
			if (!line)
				return;
			line--;
			*string = '\0';
		}
		i++;
	}
}

/* --- Losses section --- */

// FUNCTION: TIE95 0x77470
// FUNCTION: TIE98 0x4509E0
static void map_Get_VR_Debrief_Loss_Title(char* string) {
	uint16_t total = 0;
	char fmt[40], buf[80];
	uint16_t i;
	uint16_t j;

	for (i = 0; i < NUM_SPEC; i++) {
		for (j = 0; j < 6; j++) {
			if (!player_Is_Side_Enemy(j))
				total += mission.kills_losses[j][i];
		}
	}
	textext_Copy_Text(fmt, txtTalkLost);
	for (j = 0; fmt[j]; j++) {
		if (fmt[j] == '1')
			fmt[j] = 1;
		if (fmt[j] == '2')
			fmt[j] = 2;
	}
	snprintf(buf, sizeof(buf), fmt, total);
	strcpy(string, buf);
	center_line = 1;
}

// FUNCTION: TIE95 0x77538
// FUNCTION: TIE98 0x450AC0
static void map_Get_VR_Debrief_Losses(char* string, int16_t craft_idx) {
	uint16_t count = 0;
	char name[40], buf[80];
	if (craft_idx < (int16_t)NUM_SPEC) {
		int16_t j;

		for (j = 0; j < 6; j++) {
			if (!player_Is_Side_Enemy(j))
				count += mission.kills_losses[j][craft_idx];
		}
	}
	if (count) {
		textext_Get_Ship_Text(name, craft_idx);
		snprintf(buf, sizeof(buf), "  %s: %d", name, count);
		strcpy(string, buf);
	}
}

// FUNCTION: TIE95 0x775B8
// FUNCTION: TIE98 0x450B60
static int16_t map_Count_VR_Debrief_Captures(void) {
	int16_t count = 0;
	int16_t i;

	for (i = 0; i < (int16_t)NUM_SPEC; i++) {
		if (mission.captures_by_type[i])
			count++;
	}
	return (count + max_paragraph_size - 3) / (max_paragraph_size - 2);
}

// FUNCTION: TIE95 0x775F4
// FUNCTION: TIE98 0x450B90
static void map_Find_VR_Debrief_Captures(char* string, int16_t page, int16_t line) {
	int in_page = line % max_paragraph_size;
	int16_t i;

	if (in_page < 2) {
		if (in_page != 0) {
			textext_Copy_Text(string, txtTalkDash);
			center_line = 1;
		} else {
			map_Get_VR_Debrief_Capture_Title(string);
		}
		return;
	}
	i = 0;
	line -= 2 * (page + 1);
	while (i < NUM_SPEC) {
		map_Get_VR_Debrief_Captures(string, i);
		if (*string) {
			if (!line)
				return;
			*string = '\0';
			line--;
		}
		i++;
	}
}

/* --- Captures section --- */

// FUNCTION: TIE95 0x7766C
// FUNCTION: TIE98 0x450C20
static void map_Get_VR_Debrief_Capture_Title(char* string) {
	uint16_t total = 0;
	char fmt[40], buf[80];
	uint16_t i;
	uint16_t j;

	for (i = 0; i < NUM_SPEC; i++)
		total += mission.captures_by_type[i];
	textext_Copy_Text(fmt, txtTalkCaptured);
	for (j = 0; fmt[j]; j++) {
		if (fmt[j] == '1')
			fmt[j] = 1;
		if (fmt[j] == '2')
			fmt[j] = 2;
	}
	snprintf(buf, sizeof(buf), fmt, total);
	strcpy(string, buf);
	center_line = 1;
}

// FUNCTION: TIE95 0x77708
// FUNCTION: TIE98 0x450CD0
static void map_Get_VR_Debrief_Captures(char* string, int16_t craft_idx) {
	uint16_t count = mission.captures_by_type[craft_idx];
	if (count) {
		char name[40], buf[80];
		textext_Get_Ship_Text(name, craft_idx);
		snprintf(buf, sizeof(buf), "  %s: %d", name, count);
		strcpy(string, buf);
	}
}

/* ======================================================================
 * Score update functions
 * ====================================================================== */

/* Retail inlines the training and combat score updates here. */
// FUNCTION: TIE95 0x77754
// FUNCTION: TIE98 0x450D50
static void map_Update_Debrief_Scores(void) {
	int16_t scene = shellext_Get_Cur_Scene();

	if (scene == SCENE_TRAIN_MAP) {
		/* Training score file, kept in the shared TIE98-capable representation. */
		TrainingScoreEntry train_scores[TRAIN_SCORE_ENTRY_COUNT];
		char pilot_name[TIE_PILOT_NAME_CAPACITY];
		int16_t change, index;
		int16_t i;

		if (!TieScoreTables_LoadTraining("train.hgh", train_scores))
			memset(train_scores, 0, sizeof(train_scores));

		change = 0;
		index = 0;
#ifdef TIE_MODERN
		shipext_Get_Pilot_Name(pilot_name, sizeof(pilot_name));
#else
		shipext_Get_Pilot_Name(pilot_name);
#endif

		for (i = 0; i < TRAIN_SCORE_ENTRY_COUNT && !change; i++) {
			if (train_scores[i].score < mission.mission_score) {
				change = 1;
				index = i;
			}
		}

		if (change) {
			for (i = TRAIN_SCORE_ENTRY_COUNT - 1; i > index; i--)
				train_scores[i] = train_scores[i - 1];
			snprintf(train_scores[index].name, sizeof(train_scores[index].name), "%s", pilot_name);
			train_scores[index].score = mission.mission_score;
			train_scores[index].level = mission.train_level;
			TieScoreTables_SaveTraining("train.hgh", train_scores);
		}
	} else if (scene == SCENE_COMBAT_MAP_B) {
		char mission_name[16], pilot_name[TIE_PILOT_NAME_CAPACITY];
		uint8_t ship_idx = shipext_Get_Combat_Ship();
		char file_name[16];
		int16_t missions;
		GameScoreHead* scores;
		int16_t num_scores;
		int16_t kills;
		int16_t i;
		int16_t index;

		if (ship_idx < 12) {
			strcpy(file_name, "shipxx.hgh");
			file_name[4] = (ship_idx + 1) / 10 + '0';
			file_name[5] = (ship_idx + 1) % 10 + '0';
			missions = 8;
		} else {
			strcpy(file_name, "battlexx.hgh");
			file_name[6] = (ship_idx - 11) / 10 + '0';
			file_name[7] = (ship_idx - 11) % 10 + '0';
			missions = 20;
		}

		scores = (GameScoreHead*)calloc(missions, sizeof(GameScoreHead));
		if (!scores)
			return;
		num_scores = 0;
		TieScoreTables_LoadGame(file_name, scores, missions, &num_scores);

		/* Count player kills */
		kills = 0;
		for (i = 0; i < (int16_t)NUM_SPEC; i++)
			kills += pstate.player_kills_per_species[i];

		strcpy(mission_name, shipext_Get_Mission_Name());
#ifdef TIE_MODERN
		shipext_Get_Pilot_Name(pilot_name, sizeof(pilot_name));
#else
		shipext_Get_Pilot_Name(pilot_name);
#endif

		/* Find or create mission slot */
		index = 0;
		while (index < missions && scores[index].name[0] && strcmp(scores[index].name, mission_name))
			index++;

		if (index < missions && !scores[index].name[0]) {
			snprintf(scores[index].name, sizeof(scores[index].name), "%s", mission_name);
			num_scores++;
		}

		if (index < missions) {
			int16_t change = 0, score_index = 0;

			for (i = 0; i < GAME_SCORE_ENTRY_COUNT && !change; i++) {
				if (scores[index].scores[i].score < mission.mission_score) {
					change = 1;
					score_index = i;
				}
			}

			if (change) {
				for (i = GAME_SCORE_ENTRY_COUNT - 1; i > score_index; i--)
					scores[index].scores[i] = scores[index].scores[i - 1];
				snprintf(scores[index].scores[score_index].name,
						 sizeof(scores[index].scores[score_index].name), "%s", pilot_name);
				scores[index].scores[score_index].score = mission.mission_score;
				scores[index].scores[score_index].status = kills;
				TieScoreTables_SaveGame(file_name, scores, num_scores);
			}
		}

		free(scores);
	}
}

// FUNCTION: TIE95 0x77C4C
// FUNCTION: TIE98 0x4513E0
static void map_Set_Voice_Species_Mission(void) {
	int16_t* species = &talk_voice_species;

	last_voiced_paragraph = 0;
	if (!map_uses_battle_voice) {
		if (pilot_record.cur_combat_ship < NUM_SHIPS) {
			*species = pilot_record.cur_combat_ship + 1;
			*species = -*species;
		} else {
			*species = pilot_record.cur_combat_ship - (NUM_SHIPS - 1);
		}
		talk_voice_mission = pilot_record.combat_course_cursor[pilot_record.cur_combat_ship] + 1;
	} else {
		*species = pilot_record.cur_battle + 1;
		talk_voice_mission = pilot_record.battle_cursor[pilot_record.cur_battle] + 1;
	}
}
