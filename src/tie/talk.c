// FLAGS: TIE95 -d2
#include "tie/talk.h"
#include "tie_runtime/audio/imuse_api.h"
#ifdef TIE_MODERN
#include "tie_runtime/runtime/talk_task.h"
#endif

#include "tie/goals.h"
#include "tie/mission.h"
#include "tie/player.h"
#include "tie/shade.h"
#include "tie/shellext.h"
#include "tie/shipext.h"
#include "tie/textext.h"
#include "tie/tie.h"
#include "tie_runtime/audio/config.h"
#include "tie_runtime/audio/frontend_voice.h"
#include "tie_runtime/audio/voc_compat.h"
#include "tie_runtime/diagnostics/diagnostics.h"
#include "tie_runtime/display/classic_display.h"
#include "tie_runtime/display/classic_framebuffer.h"
#include "tie_runtime/flight_assets/model_types.h"
#include "tie_runtime/input/input.h"
#include "tie_runtime/runtime/exports.h"
#include "tie_runtime/runtime/profile.h"
#ifdef TIE_MODERN
#include "tie_runtime/snapshot/snapshot.h"
#include "tie_runtime/snapshot/snapshot_internal.h"
#endif
#include "tie_runtime/storage/storage.h"

#include "landru/actanim.h"
#include "landru/actdelt.h"
#include "landru/actor.h"
#include "landru/canvas.h"
#include "landru/cursor.h"
#include "landru/dirty.h"
#include "landru/error.h"
#include "landru/file.h"
#include "landru/film.h"
#include "landru/font.h"
#include "landru/fourcc.h"
#include "landru/inpattr.h"
#include "landru/input.h"
#include "landru/io.h"
#include "landru/memhdl.h"
#include "landru/paint.h"
#include "landru/pal.h"
#include "landru/rect.h"
#include "landru/res.h"
#include "landru/sound.h"
#include "landru/stream.h"
#include "landru/view.h"
#include "landru/viewadd.h"

#include <imuse/lolevel.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ======================================================================
 * Static data — resource name table (16 entries × 14 chars)
 * ====================================================================== */

// GLOBAL: TIE95 0xCE474
// GLOBAL: TIE98 0x4F1978
static const char talk_str[16][14] = { "talk.lfd", "brf_off", "brf_ss", "dbrf_off", "dbrf_ss", "eyes",
									   "ssface",   "offbak",  "ssbak",  "doffbak",  "dssbak",  "offtxt",
									   "sstxt",    "dofftxt", "dsstxt", "mouth" };

/* Officer face animation state tables (indexed by mood 0-4) */
// GLOBAL: TIE95 0xCE554
// GLOBAL: TIE98 0x4F1A58
static const int16_t officer_mood_eye[5] = { 0, 5, 9, 6, 9 };
// GLOBAL: TIE95 0xCE55E
// GLOBAL: TIE98 0x4F1A68
static const int16_t officer_mood_blink[5] = { 4, 4, 4, 7, 7 };
// GLOBAL: TIE95 0xCE568
// GLOBAL: TIE98 0x4F1A78
static const int16_t officer_mood_mouth[5] = { 0, 2, 1, 3, 0 };
// GLOBAL: TIE95 0xCE572
// GLOBAL: TIE98 0x4F1A88
static const int16_t priest_mood_eye[5] = { 0, 5, 5, 6, 5 };
// GLOBAL: TIE95 0xCE57C
// GLOBAL: TIE98 0x4F1A98
static const int16_t priest_mood_blink[5] = { 4, 5, 5, 7, 5 };

/* ======================================================================
 * Static BSS globals
 * ====================================================================== */

// GLOBAL: TIE95 0xF570C
// GLOBAL: TIE98 0x589CE0
static int16_t talk_win_id[6];
// GLOBAL: TIE95 0xF5718
// GLOBAL: TIE98 0x589D0C
static EBriefStruct* talk_brief;
// GLOBAL: TIE95 0xF571C
// GLOBAL: TIE98 0x589CF8
static Actor* eye_actor;
// GLOBAL: TIE95 0xF5720
// GLOBAL: TIE98 0x589CEC
static Input* parent;
// GLOBAL: TIE95 0xF5724
// GLOBAL: TIE98 0x589D00
static Actor* mouth_actor;
// GLOBAL: TIE95 0xF5728
// GLOBAL: TIE98 0x589D04
static Input* talk_input; /* renamed from 'talk' to avoid keyword conflict */
// GLOBAL: TIE95 0xF572C
// GLOBAL: TIE98 0x589CDC
static Film* talk_film;
// GLOBAL: TIE95 0xF5730
// GLOBAL: TIE98 0x589D18
static Input* answer;
// GLOBAL: TIE95 0xF5734
// GLOBAL: TIE98 0x589CC4
static EFArrayStruct* talk_fgroup;
// GLOBAL: TIE95 0xF5738
// GLOBAL: TIE98 0x589CD0
static int16_t talk_win_status[5];
// GLOBAL: TIE95 0xF5744
// GLOBAL: TIE98 0x589D08
static int16_t max_paragraph_size;
// GLOBAL: TIE95 0xF5746
// GLOBAL: TIE98 0x589CC0
static int16_t cur_talk_question;
// GLOBAL: TIE95 0xF574C
// GLOBAL: TIE98 0x589D14
static int16_t num_talk_paragraphs;
// GLOBAL: TIE95 0xF574E
// GLOBAL: TIE98 0x589CC8
static int16_t center_line;
// GLOBAL: TIE95 0xF5752
// GLOBAL: TIE98 0x589CF4
static int16_t cur_talk_paragraph;
// GLOBAL: TIE95 0xF5754
// GLOBAL: TIE98 0x589D10
static int16_t talk_mode;
// GLOBAL: TIE95 0xF5756
// GLOBAL: TIE98 0x589CFC
static int16_t num_talk_questions;
// GLOBAL: TIE95 0xF5748
// GLOBAL: TIE98 0x589CCC
static int16_t active_talk_question;
// GLOBAL: TIE95 0xF5758
// GLOBAL: TIE98 0x589CF0
static int16_t officer_mood;

/* Auto-advance timer for paragraph display. Retail bumps this by 264 per
 * paragraph step; auto-advance triggers when iuser time arg exceeds it.
 * Stays at INT32_MAX (inert) when speech is disabled or when the current
 * question is the final debrief paragraph. Shared with map.c. */
// GLOBAL: TIE95 0xCE586
// GLOBAL: TIE98 0x4F1AA4
int32_t talk_paragraph_timer = 0x7FFFFFFF;

/* ----------------------------------------------------------------------
 * Voice-over / streaming speech.
 *
 * Retail dedicates one streaming Sound for talk briefings; the same
 * sound is reused by the in-flight VR talk in map.c. The 2 MB buffer
 * was a streaming staging area filled 2 KB/frame from the CD; with our
 * synchronous file I/O we just load the whole .voc file into the
 * Sound's data buffer at trigger time.
 *
 * talk_speech_sound is owned by talk.c / map.c (whoever called
 * talk_Alloc_Speech_Sound). talk_voice_* hold the filename inputs
 * (species index, mission number, officer character, mood character,
 * paragraph index).
 * -------------------------------------------------------------------- */
// GLOBAL: TIE95 0xF5704
Sound* talk_speech_sound = NULL;
/* talk_voice_species >0: numeric "Nm..." filename; <0: char-encoded "[fibagdm]m...". */
// GLOBAL: TIE95 0xF5742
// GLOBAL: TIE98 0x5A2768
int16_t talk_voice_species = 0;
// GLOBAL: TIE95 0xF5750
// GLOBAL: TIE98 0x5A2760
int16_t talk_voice_mission = 0;
// GLOBAL: TIE95 0xF574A
// GLOBAL: TIE98 0x5A2756
int16_t talk_voice_question = 0;
// GLOBAL: TIE95 0xF575A
// GLOBAL: TIE98 0x5A2754
uint8_t talk_voice_officer = 0; /* 'o', 'p', 'i' */
// GLOBAL: TIE95 0xF575B
// GLOBAL: TIE98 0x5A276A
int8_t talk_voice_mood = 0; /* 'b', 'd', 'h', 'o', etc. */

enum {
	TALK_SPEECH_BUF_SIZE = 2048000,   /* matches retail allocation */
	TALK_SPEECH_PRIME_SIZE = 0x20000, /* retail initial stream read */
};

// GLOBAL: TIE95 0xF5708
// GLOBAL: TIE98 0x5A2764
static int32_t talk_speech_pos = 0;
// GLOBAL: TIE95 0xF575C
// GLOBAL: TIE98 0x5A276B
static uint8_t talk_speech_streaming = 0;

static void talk_end_View(int32_t refresh);
static int16_t talk_iupdate_Talk(Input* input, Rect* r, Rect* clip_r, int16_t key, uint8_t left,
								 uint8_t right, int16_t x, int16_t y);
static int16_t talk_iuser_Talk(Input* input, int32_t time);
static void talk_idraw_Talk(Input* input, Rect* r, Rect* clip_r, int16_t refresh);
static int16_t talk_iupdate_Answer(Input* input, Rect* r, Rect* clip_r, int16_t key, uint8_t left,
								   uint8_t right, int16_t x, int16_t y);
static int16_t talk_iuser_Answer(Input* input, int32_t time);
static void talk_idraw_Answer(Input* input, Rect* r, Rect* clip_r, int16_t refresh);
static int16_t talk_user_Talk_Eyes(Actor* actor, int32_t time);
static void talk_Check_Talk_Questions(void);
static int16_t talk_Count_Debrief_Pages(void);
static void talk_Get_Debrief_Line(char* string, int16_t line);
static int16_t talk_Count_Debrief_Header(void);
static void talk_Find_Debrief_Header(char* string, int16_t line);
static void talk_Get_Debrief_Header(char* string, int16_t line_idx);
static int16_t talk_Count_Debrief_Goals(void);
static void talk_Find_Debrief_Goals(char* string, int16_t line);
static void talk_Get_Debrief_Goals(char* string, int16_t line_idx);
static int16_t talk_Count_Debrief_Kills(void);
static void talk_Find_Debrief_Kills(char* string, int16_t page, int16_t line);
static void talk_Get_Debrief_Kill_Title(char* string);
static void talk_Get_Debrief_Kills(char* string, int16_t craft_idx);
static int16_t talk_Count_Debrief_Losses(void);
static void talk_Find_Debrief_Losses(char* string, int16_t page, int16_t line);
static void talk_Get_Debrief_Loss_Title(char* string);
static void talk_Get_Debrief_Losses(char* string, int16_t craft_idx);
static int16_t talk_Count_Debrief_Captures(void);
static void talk_Find_Debrief_Captures(char* string, int16_t page, int16_t line);
static void talk_Get_Debrief_Capture_Title(char* string);
static void talk_Get_Debrief_Captures(char* string, int16_t craft_idx);
static void talk_Speech_User_Func(Sound* snd, int32_t time);

/* ======================================================================
 * talk_Talk — main entry point
 * ====================================================================== */

// FUNCTION: TIE95 0x67F89
// FUNCTION: TIE98 0x489530
int talk_Talk(SceneHeadStruct* scene_head) {
	ResFile* resource;
	Rect frame;
	int talk_type;
	Actor* delt;

	resource = shellext_Open_Empire_Resource(talk_str[0]);
	xrect_Set_Rect(&frame, 0, 0, 320, 200);

	/* Retail TALK_Talk seeds the voice-over filename chars here:
	 * 'o','b' for brief officer, 'p','b' for brief priest, 'o','d' for
	 * debrief officer, 'p','d' for debrief priest. */
	switch (shellext_Get_Cur_Scene()) {
		case SCENE_TALK_BRIEF_OFFICER:
			max_paragraph_size = 10;
			talk_type = 1;
			talk_voice_mood = 'b';
			talk_voice_officer = 'o';
			break;
		case SCENE_TALK_DEBRIEF_OFFICER:
			max_paragraph_size = 10;
			talk_type = 3;
			talk_voice_mood = 'd';
			talk_voice_officer = 'o';
			break;
		case SCENE_TALK_BRIEF_PRIEST:
			max_paragraph_size = 10;
			talk_type = 2;
			talk_voice_mood = 'b';
			talk_voice_officer = 'p';
			break;
		case SCENE_TALK_DEBRIEF_PRIEST:
			max_paragraph_size = 10;
			talk_type = 4;
			talk_voice_mood = 'd';
			talk_voice_officer = 'p';
			break;
	}

	talk_mode = talk_type - 1;

	/* Load talk film. Tag the snapshot with the (lfd, film) tuple so
	 * the cutscene compositor can resolve a remaster bundle for this
	 * screen. One tag call covers all four scenes — talk_str[talk_type]
	 * picks the right film name (brf_off / brf_ss / dbrf_off / dbrf_ss).
	 * Default INCREMENTAL redraw model is correct (face-anim + text
	 * scroll under dirty-rect refresh, persistent RT). The tag is
	 * auto-cleared at the next scene transition by
	 * shell_run_scene_dispatch. */
	talk_film = xfilm_Res_Film(talk_str[talk_type], &frame, 0, 0, 0);
#ifdef TIE_MODERN
	TieSnapshotBuilder_SetActiveFilm("TALK", talk_str[talk_type]);
#endif
	xfilm_Set_Film_Def_Palette(talk_film, scene_head->def_palette);

	/* Find and disable the text overlay delta actor */
	delt = xactor_Find_Actor(FOURCC_DELT, talk_str[talk_type + 6]);
	xactor_Non_Refreshable_Actor(delt);

	/* Set up face animation actors */
	switch (shellext_Get_Cur_Scene()) {
		case SCENE_TALK_BRIEF_OFFICER:
		case SCENE_TALK_DEBRIEF_OFFICER:
			eye_actor = xactor_Find_Actor(FOURCC_ANIM, talk_str[5]);
			mouth_actor = xactor_Find_Actor(FOURCC_ANIM, talk_str[15]);
			xactor_Set_Actor_User_Function(eye_actor, (xactorCallback)(void (*)(void))talk_user_Talk_Eyes);
			eye_actor->id = 0;
			break;
		case SCENE_TALK_BRIEF_PRIEST:
		case SCENE_TALK_DEBRIEF_PRIEST:
			eye_actor = xactor_Find_Actor(FOURCC_ANIM, talk_str[6]);
			mouth_actor = NULL;
			xactor_Set_Actor_User_Function(eye_actor, (xactorCallback)(void (*)(void))talk_user_Talk_Eyes);
			eye_actor->id = 1;
			break;
	}

	/* Build the input widget tree */
	parent = xinput_Alloc_Input(NULL, &frame, 0, 0);

	xrect_Set_Rect(&frame, 122, 116 - 10 * (max_paragraph_size + 1), 318, 116);
	answer = xinput_Alloc_Input(parent, &frame, 0, 0);
	xinpattr_Set_Input_Update_Function(answer, talk_iupdate_Answer);
	xinpattr_Set_Input_User_Function(answer, (InputUserFunc)(void (*)(void))talk_iuser_Answer);
	xinpattr_Set_Input_Draw_Function(answer, talk_idraw_Answer);
	xinpattr_Refreshable_Input(answer);
	answer->mouseUsage = allInput;
	answer->id = 0;

	xrect_Set_Rect(&frame, 122, 135, 318, 195);
	talk_input = xinput_Alloc_Input(parent, &frame, 0, 0);
	xinpattr_Set_Input_Update_Function(talk_input, talk_iupdate_Talk);
	xinpattr_Set_Input_User_Function(talk_input, (InputUserFunc)(void (*)(void))talk_iuser_Talk);
	xinpattr_Set_Input_Draw_Function(talk_input, talk_idraw_Talk);
	xinpattr_Refreshable_Input(talk_input);
	talk_input->mouseUsage = allInput;
	talk_input->id = 0;

	/* Initialize talk state */
	talk_brief = player_Init_Brief_For_Talk();
	talk_fgroup = player_Fetch_FGroup();

	talk_Set_Talk_To_Text();
	officer_mood = 0;

	/* Position mouse */
	switch (shellext_Get_Cur_Scene()) {
		case SCENE_TALK_BRIEF_OFFICER:
		case SCENE_TALK_BRIEF_PRIEST:
			xio_Set_Mouse_Position(260, 192 - 10 * num_talk_questions);
			break;
		case SCENE_TALK_DEBRIEF_OFFICER:
		case SCENE_TALK_DEBRIEF_PRIEST:
			xio_Set_Mouse_Position(260, 182);
			break;
	}

	/* Push the modal view task */
	xview_Set_View_Update_Function(talk_end_View);
	xviewadd_Clear_View();
	xview_Disable_All_View_Erase();

	/* Resolve species/mission and arm the streaming speech sound. */
	talk_Set_Voice_Species_Mission();
	talk_Alloc_Speech_Sound();
#ifdef TIE_MODERN
	TieTalk_RunView(resource);
	return 0;
#else
	shellext_Handle_TIE_View();
	talk_Free_Speech_Sound();
	xview_Enable_All_View_Erase();
	xview_Clear_View_Update_Function();

	if (xcursor_Is_Cursor_Visible())
		xcursor_Hide_Cursor();

	player_Free_Display_Map();
	xres_Close_Resource(resource);
	return xerror_Get_Landru_Exit();
#endif
}

/* ======================================================================
 * talk_end_View — view update callback
 * ====================================================================== */

// FUNCTION: TIE95 0x683BD
// FUNCTION: TIE98 0x489A10
static void talk_end_View(int32_t refresh) {
	if (refresh)
		return;
	if (xcursor_Is_Cursor_Visible())
		return;
	xcursor_Show_Cursor();
}

/* ======================================================================
 * iupdate/iuser/idraw callbacks
 * ====================================================================== */

// FUNCTION: TIE95 0x683F5
// FUNCTION: TIE98 0x489A30
static int16_t talk_iupdate_Talk(Input* input, Rect* r, Rect* clip_r, int16_t key, uint8_t left,
								 uint8_t right, int16_t x, int16_t y) {
	uint8_t button;
	int16_t hover;

	(void)r;
	(void)clip_r;
	(void)x;

	if (key)
		return 0;

	button = left;
	if (!button)
		button = right;

	switch (button) {
		case 0:
		case 1:
		case 2:
			if (y >= 0 && y < 10 * (num_talk_questions + 1))
				hover = y / 10;
			else
				hover = -1;
			if (hover != active_talk_question) {
				active_talk_question = hover;
				xinpattr_Refresh_Input(input);
				xinpattr_Refresh_Input(answer);
				if (button)
					input->var1 = 1;
			}
			break;
		case 3:
			if (active_talk_question != -1) {
				if (active_talk_question == cur_talk_question) {
					xinpattr_Selected_Input(answer);
					answer->var2 = (button == right);
				} else {
					xinpattr_Selected_Input(input);
				}
				xinpattr_Refresh_Input(input);
				xinpattr_Refresh_Input(answer);
			}
			input->var1 = 0;
			break;
	}
	input->var2 = 1;
	return 1;
}

// FUNCTION: TIE95 0x6855A
// FUNCTION: TIE98 0x489B50
static int16_t talk_iuser_Talk(Input* input, int32_t time) {
	if (!time) {
		shade_Build_Shaded_Palette();
		xinpattr_Show_Input(talk_input);
		xinpattr_Refresh_Input(talk_input);
	}

	if (xinpattr_Get_Input_Selected(input)) {
		if (active_talk_question == num_talk_questions) {
			/* Exit selected */
			int16_t scene = shellext_Get_Cur_Scene();
			if (scene == SCENE_TALK_BRIEF_OFFICER || scene == SCENE_TALK_BRIEF_PRIEST)
				xerror_Set_Landru_Exit(SCENE_BRIEF);
			else if (scene == SCENE_TALK_DEBRIEF_OFFICER || scene == SCENE_TALK_DEBRIEF_PRIEST)
				xerror_Set_Landru_Exit(SCENE_DEBRIEF);
		} else {
			cur_talk_question = active_talk_question;
			/* Voice-over: kick off the .voc for this question and arm
			 * auto-advance, except for the closing debrief question
			 * (mood 'd' + last index), which the player reads manually. */
			talk_voice_question = (int16_t)(active_talk_question + 1);
			talk_Start_Speech_Stream();
			if (options_gbl.speech_active) {
				if (talk_voice_mood == 'd' && cur_talk_question == num_talk_questions - 1)
					talk_paragraph_timer = 0x7FFFFFFF;
				else
					talk_paragraph_timer = time + 264;
			} else {
				talk_paragraph_timer = 0x7FFFFFFF;
			}
			talk_Set_Talk_Paragraph();
			xinpattr_Refresh_Input(input);
			xinpattr_Refresh_Input(answer);
		}
	}

	if (input->var2) {
		input->var2 = 0;
	} else if (active_talk_question != -1) {
		active_talk_question = -1;
		xinpattr_Refresh_Input(input);
		xinpattr_Refresh_Input(answer);
	}
	return 1;
}

// FUNCTION: TIE95 0x686D5
// FUNCTION: TIE98 0x489CB0
static void talk_idraw_Talk(Input* input, Rect* r, Rect* clip_r, int16_t refresh) {
	Rect tr;
	int16_t y_offset;
	int16_t q;

	char question_buf[80];

	if (!refresh)
		return;

	shade_Draw_Talk_Shade_Rect(r);

	xrect_Copy_Rect(&tr, r);
	tr.bottom = tr.top + 10 * num_talk_questions + 4;
	xfont_Enable_FontID_Shadow(0);

	y_offset = 0;
	for (q = 0; q <= num_talk_questions; q++) {
		int16_t color;

		if (q == active_talk_question)
			color = input->var1 ? 14 : 28;
		else
			color = (q == cur_talk_question) ? 9 : 24;

		talk_Get_Talk_Question(question_buf, q);
		xfont_Print_Clipped_Text(question_buf, tr.left + 3, y_offset + tr.top + 2, 0, color);
		y_offset += 10;
	}
	xfont_Disable_FontID_Shadow(0);

	if (xinpattr_Is_Input_Dirty(input))
		xdirty_Dirty_Rect(clip_r);
}

// FUNCTION: TIE95 0x687FB
// FUNCTION: TIE98 0x489DB0
static int16_t talk_iupdate_Answer(Input* input, Rect* r, Rect* clip_r, int16_t key, uint8_t left,
								   uint8_t right, int16_t x, int16_t y) {
	uint8_t button;
	int16_t frame_pad;

	(void)r;
	(void)clip_r;
	(void)x;
	(void)frame_pad;
	(void)y;

	if (key)
		return 0;

	button = left;
	if (!button)
		button = right;
	if (cur_talk_question != -1) {
		switch (button) {
			case 1:
				input->var1 = 1;
				xinpattr_Refresh_Input(input);
				xinpattr_Refresh_Input(talk_input);
				break;
			case 3:
				input->var1 = 0;
				input->var2 = (button == right);
				xinpattr_Selected_Input(input);
				xinpattr_Refresh_Input(input);
				xinpattr_Refresh_Input(talk_input);
				break;
		}
	}
	return 1;
}

// FUNCTION: TIE95 0x688E2
// FUNCTION: TIE98 0x489E50
static int16_t talk_iuser_Answer(Input* input, int32_t time) {
	/* Auto-advance: when armed (talk_paragraph_timer < INT32_MAX) the
	 * paragraph advances each time the current time exceeds the
	 * threshold; the threshold bumps by 264 ms per page. */
	if (time > talk_paragraph_timer) {
		talk_paragraph_timer += 264;
		if (++cur_talk_paragraph >= num_talk_paragraphs) {
			talk_paragraph_timer = 0x7FFFFFFF;
			cur_talk_question = -1;
			cur_talk_paragraph = -1;
		}
	}

	if (xinpattr_Get_Input_Selected(input)) {
		if (input->var2) {
			/* Right-click: previous page; disable auto-advance */
			if (cur_talk_paragraph) {
				talk_paragraph_timer = 0x7FFFFFFF;
				cur_talk_paragraph--;
			}
		} else {
			/* Left-click: next page; push threshold forward 264 ms,
			 * clamping when the retail 32-bit addition sets the sign bit. */
			uint32_t next_timer = (uint32_t)talk_paragraph_timer + 264u;
			if (next_timer & 0x80000000u)
				talk_paragraph_timer = 0x7FFFFFFF;
			else
				talk_paragraph_timer = (int32_t)next_timer;
			if (++cur_talk_paragraph >= num_talk_paragraphs) {
				talk_paragraph_timer = 0x7FFFFFFF;
				cur_talk_question = -1;
				cur_talk_paragraph = -1;
			}
		}
	}
	return 1;
}

// FUNCTION: TIE95 0x689DF
// FUNCTION: TIE98 0x489F30
static void talk_idraw_Answer(Input* input, Rect* r, Rect* clip_r, int16_t refresh) {
	Rect dst;
	int16_t first_line;
	int16_t text_color;
	int16_t y_off;
	int16_t cur_line;

	char line_buf[80];
	char page_buf[40];

	if (!refresh)
		return;
	if (cur_talk_paragraph != -1) {
		xrect_Copy_Rect(&dst, r);
		first_line = max_paragraph_size * cur_talk_paragraph;
		text_color = input->var1 ? 1 : 9;

		xfont_Enable_FontID_Shadow(0);

		y_off = 0;
		for (cur_line = first_line; cur_line < first_line + max_paragraph_size; cur_line++) {
			int16_t line_y;

			center_line = 0;
			talk_Get_Talk_Paragraph(line_buf, cur_line);

			line_y = y_off + dst.top;
			if (center_line) {
				Rect line_rect;
				xrect_Set_Rect(&line_rect, dst.left, line_y, dst.right, line_y + 10);
				xfont_Print_Centered_Text(line_buf, &line_rect, 0, text_color);
			} else {
				xfont_Print_Clipped_Text(line_buf, dst.left, line_y, 0, text_color);
			}
			y_off += 10;
		}

		/* Page indicator */
		textext_Copy_Text(line_buf, txtTalkOf);
		snprintf(page_buf, sizeof(page_buf), line_buf, cur_talk_paragraph + 1, num_talk_paragraphs);
		textext_Copy_Text(line_buf, txtMapPage);
		strcat(line_buf, " ");
		strcat(line_buf, page_buf);
		xfont_Print_Clipped_Text(line_buf, dst.right - 84, y_off + dst.top, 0, text_color);
		xfont_Disable_FontID_Shadow(0);
	}

	if (xinpattr_Is_Input_Dirty(input))
		xdirty_Dirty_Rect(clip_r);
}

/* ======================================================================
 * talk_user_Talk_Eyes — actor callback for face animation
 * ====================================================================== */

// FUNCTION: TIE95 0x68BC8
// FUNCTION: TIE98 0x48A140
static int16_t talk_user_Talk_Eyes(Actor* actor, int32_t time) {
	int16_t eye;
	int16_t blink;
	int16_t mouth_st;

	switch (actor->id) {
		case 0:
			/* Officer face: separate eye + mouth actors */
			if (xactor_Is_Actor_Visible(actor)) {
				eye = officer_mood_eye[talk_Get_Officer_Mood()];
				blink = officer_mood_blink[talk_Get_Officer_Mood()];
				mouth_st = officer_mood_mouth[talk_Get_Officer_Mood()];

				if (!time || actor->var1 <= 0) {
					if (actor->var1 == -2)
						actor->var1 = rand() & 0x5F;
					else
						actor->var1--;
					xactor_Set_Actor_State(actor, blink, 0);
				} else {
					actor->var1--;
					xactor_Set_Actor_State(actor, eye, 0);
				}
				xactor_Set_Actor_State(mouth_actor, mouth_st, 0);
			}
			break;
		case 1:
			/* Priest face: single actor with eye states */
			if (xactor_Is_Actor_Visible(actor)) {
				eye = priest_mood_eye[talk_Get_Officer_Mood()];
				blink = priest_mood_blink[talk_Get_Officer_Mood()];

				if (!time || actor->var1 <= 0) {
					if (actor->var1 == -3)
						actor->var1 = rand() & 0x5F;
					else
						actor->var1--;
					xactor_Set_Actor_State(actor, blink, 0);
				} else {
					actor->var1--;
					xactor_Set_Actor_State(actor, eye, 0);
				}
			}
			break;
	}
	return 1;
}

/* ======================================================================
 * Mood get/set
 * ====================================================================== */

// FUNCTION: TIE95 0x68D68
// FUNCTION: TIE98 0x48A8E0
void talk_Set_Officer_Mood(int16_t mood) { officer_mood = mood; }

// FUNCTION: TIE95 0x68D95
// FUNCTION: TIE98 0x48A2A0
int16_t talk_Get_Officer_Mood(void) { return officer_mood; }

/* ======================================================================
 * talk_Set_Talk_To_Text — reinitialize talk for text mode
 * ====================================================================== */

// FUNCTION: TIE95 0x68DC2
// FUNCTION: TIE98 0x48A2B0
void talk_Set_Talk_To_Text(void) {
	char question_buf[80];
	Rect r;
	int16_t frame_pad;
	int16_t max_w;
	int16_t total_h;
	int16_t w;
	int16_t i;
	int16_t saved_font;
	(void)frame_pad;

	num_talk_questions = 0;
	active_talk_question = -1;
	cur_talk_question = -1;
	num_talk_paragraphs = 0;
	cur_talk_paragraph = -1;

	/* Scan talk data slots for visibility status */
	talk_Check_Talk_Questions();

	/* Build filtered question list */
	for (i = 0; i < 5; ++i) {
		switch (talk_win_status[i]) {
			case 1:
				talk_win_id[num_talk_questions++] = i;
				break;
			case 2:
				if (mission.primary_complete == 1)
					talk_win_id[num_talk_questions++] = i;
				break;
			case 3:
				if (mission.secondary_complete == 1)
					talk_win_id[num_talk_questions++] = i;
				break;
			case 4:
				if (mission.primary_complete != 1)
					talk_win_id[num_talk_questions++] = i;
				break;
			case 5:
				if (mission.secondary_complete != 1)
					talk_win_id[num_talk_questions++] = i;
				break;
		}
	}

	/* For debrief scenes, add debrief question */
	if (shellext_Get_Cur_Scene() == SCENE_TALK_DEBRIEF_OFFICER ||
		shellext_Get_Cur_Scene() == SCENE_TALK_DEBRIEF_PRIEST) {
		if (num_talk_questions == 5)
			talk_win_id[num_talk_questions - 1] = 5; /* overwrite last slot */
		else
			talk_win_id[num_talk_questions++] = 5;
		cur_talk_question = num_talk_questions - 1;
		active_talk_question = cur_talk_question;
		num_talk_paragraphs = talk_Count_Debrief_Pages();
		cur_talk_paragraph = 0;
	}

	/* Add exit entry and size the talk widget */
	talk_win_id[num_talk_questions] = 6;

	max_w = total_h = 0;
	for (i = 0; i <= num_talk_questions; ++i) {
		talk_Get_Talk_Question(question_buf, i);
		saved_font = xfont_Get_Font();
		xfont_Set_Font(0);
		w = xfont_Get_String_Width(question_buf);
		xfont_Set_Font(saved_font);
		if (max_w < w)
			max_w = w;
		total_h += 10;
	}
	max_w += 6;
	total_h += 3;

	xrect_Set_Rect(&r, 318 - max_w, 198 - total_h, 318, 198);
	xinpattr_Set_Input_Frame(talk_input, &r);

	if (options_gbl.speech_active) {
		if (talk_voice_mood != 'd' || cur_talk_question != num_talk_questions - 1)
			talk_paragraph_timer = 264;
		else
			talk_paragraph_timer = 0x7FFFFFFF;
	} else
		talk_paragraph_timer = 0x7FFFFFFF;
}

/* ======================================================================
 * talk_Set_Talk_Paragraph — recalculate paragraph count
 * ====================================================================== */

// FUNCTION: TIE95 0x690CE
// FUNCTION: TIE98 0x48A4C0
void talk_Set_Talk_Paragraph(void) {
	int pos;
	int line_count;
	int8_t* data;

	if (cur_talk_question >= 0 && cur_talk_question < num_talk_questions) {
		if (talk_win_id[cur_talk_question] != 5) {
			data = (int8_t*)xmemhdl_Lock_Handle(
				talk_brief->talk_data[5 * talk_mode + talk_win_id[cur_talk_question]]);
			line_count = 0;

			/* Skip question line */
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
			xmemhdl_Unlock_Handle(talk_brief->talk_data[5 * talk_mode + talk_win_id[cur_talk_question]]);

			num_talk_paragraphs = (max_paragraph_size + line_count - 1) / max_paragraph_size;
			if (num_talk_paragraphs)
				cur_talk_paragraph = 0;
			else
				cur_talk_paragraph = -1;
		} else {
			num_talk_paragraphs = talk_Count_Debrief_Pages();
			cur_talk_paragraph = 0;
		}
	} else {
		num_talk_paragraphs = 0;
		cur_talk_paragraph = -1;
	}
}

/* ======================================================================
 * talk_Get_Talk_Question — extract question display text
 * ====================================================================== */

// FUNCTION: TIE95 0x69282
// FUNCTION: TIE98 0x48A5F0
void talk_Get_Talk_Question(char* out, int16_t id) {
	signed char* data;
	int16_t frame_pad;
	int16_t i;
	int16_t buf_len;
	(void)frame_pad;

	if (id >= 0 && id < num_talk_questions) {
		if (talk_win_id[id] != 5) {
			if (talk_brief->talk_data[5 * talk_mode + talk_win_id[id]]) {
				data =
					(signed char*)xmemhdl_Lock_Handle(talk_brief->talk_data[5 * talk_mode + talk_win_id[id]]);
				i = 0;
				buf_len = 0;
				while (data[i] && data[i] != '\n') {
					if (data[i] == 4 || data[i] == 5)
						++i; /* skip control code + param */
					else
						out[buf_len++] = data[i];
					++i;
				}
				out[buf_len] = '\0';
				xmemhdl_Unlock_Handle(talk_brief->talk_data[5 * talk_mode + talk_win_id[id]]);
			} else {
				*out = '\0';
			}
		} else {
			strcpy(out, textext_Get_Text(txtTalkDebrief));
		}
	} else if (talk_win_id[id] == 6) {
		strcpy(out, textext_Get_Text(txtTalkExit));
	}
}

/* ======================================================================
 * talk_Get_Talk_Paragraph — extract paragraph line text
 * ====================================================================== */

// FUNCTION: TIE95 0x6943C
// FUNCTION: TIE98 0x48A720
void talk_Get_Talk_Paragraph(char* out, int16_t line) {
	int pos;
	int16_t italic;
	int16_t out_len;
	int16_t line_idx;
	signed char* data;

	italic = 0;
	*out = '\0';
	talk_Set_Officer_Mood(0);

	if (cur_talk_question < 0 || cur_talk_question >= num_talk_questions)
		return;

	if (talk_win_id[cur_talk_question] != 5) {
		/* Normal talk data: parse line-by-line */
		data = (signed char*)xmemhdl_Lock_Handle(
			talk_brief->talk_data[5 * talk_mode + talk_win_id[cur_talk_question]]);
		line_idx = 0;

		/* Skip the question line (first line) */
		pos = 0;
		while (data[pos] && data[pos] != '\n')
			pos++;
		pos++; /* skip newline */

		while (data[pos] && line_idx <= line) {
			if (line_idx == line) {
				out_len = 0;
				if (italic)
					out[out_len++] = 2; /* italic on */
				while (data[pos] && data[pos] != '\n') {
					if (data[pos] == 3) {
						talk_Set_Officer_Mood(data[pos + 1] - 1);
						pos += 2;
					} else {
						out[out_len++] = data[pos++];
					}
				}
				out[out_len] = '\0';
			} else {
				while (data[pos] && data[pos] != '\n') {
					if (data[pos] == 2)
						italic = 1;
					if (data[pos] == 1)
						italic = 0;
					if (data[pos] == 3) {
						talk_Set_Officer_Mood(data[pos + 1] - 1);
						pos += 2;
					} else {
						pos++;
					}
				}
			}
			if (data[pos])
				pos++;
			line_idx++;
		}
		xmemhdl_Unlock_Handle(talk_brief->talk_data[5 * talk_mode + talk_win_id[cur_talk_question]]);
	} else {
		/* Debrief mode: dispatch to section renderers */
		talk_Get_Debrief_Line(out, line);
	}
}

/* ======================================================================
 * talk_Check_Talk_Questions — scan talk data for visibility conditions
 * ====================================================================== */

// FUNCTION: TIE95 0x696A8
// FUNCTION: TIE98 0x48A8F0
static void talk_Check_Talk_Questions(void) {
	int16_t slot_idx;
	int16_t status;
	int8_t* data;
	int16_t pos;

	for (slot_idx = 0; slot_idx < 5; ++slot_idx) {
		status = 0;
		if (talk_brief->talk_data[5 * talk_mode + slot_idx]) {
			data = (int8_t*)xmemhdl_Lock_Handle(talk_brief->talk_data[5 * talk_mode + slot_idx]);
			pos = 0;
			if (data[0]) {
				status = 1;
				while (data[pos] && data[pos] != '\n') {
					if (data[pos] == 4 || data[pos] == 5) {
						if (data[pos] == 4)
							status = data[pos + 1] + 1;
						else
							status = data[pos + 1] + 3;
						++pos;
					}
					++pos;
				}
			}
			xmemhdl_Unlock_Handle(talk_brief->talk_data[5 * talk_mode + slot_idx]);
		}
		talk_win_status[slot_idx] = status;
	}
}

// FUNCTION: TIE95 0x69816
// FUNCTION: TIE98 0x48A9C0
static int16_t talk_Count_Debrief_Pages(void) {
	int16_t total = talk_Count_Debrief_Header();
	total += talk_Count_Debrief_Goals();
	total += talk_Count_Debrief_Kills();
	total += talk_Count_Debrief_Losses();
	total += talk_Count_Debrief_Captures();
	return total;
}

/* ======================================================================
 * talk_Get_Debrief_Line — dispatch a single debrief line to the right section
 * ====================================================================== */

// FUNCTION: TIE95 0x69868
// FUNCTION: TIE98 0x48A9F0
static void talk_Get_Debrief_Line(char* string, int16_t line) {
	int16_t frame_pad0;
	int16_t section_page;
	int16_t section_pages;
	int16_t section;
	int16_t frame_pad1, frame_pad2, frame_pad3, frame_pad4;

	(void)frame_pad0;
	(void)frame_pad1;
	(void)frame_pad2;
	(void)frame_pad3;
	(void)frame_pad4;
	*string = '\0';
	center_line = 0;
	section_page = line / max_paragraph_size;

	for (section = 0; section < 5 && section_page >= 0; ++section) {
		switch (section) {
			case 0:
				section_pages = talk_Count_Debrief_Header();
				if (section_page < section_pages)
					talk_Find_Debrief_Header(string, line);
				break;
			case 1:
				section_pages = talk_Count_Debrief_Goals();
				if (section_page < section_pages)
					talk_Find_Debrief_Goals(string, line);
				break;
			case 2:
				section_pages = talk_Count_Debrief_Kills();
				if (section_page < section_pages) {
					talk_Set_Officer_Mood(1);
					talk_Find_Debrief_Kills(string, section_page, line);
				}
				break;
			case 3:
				section_pages = talk_Count_Debrief_Losses();
				if (section_page < section_pages) {
					talk_Set_Officer_Mood(3);
					talk_Find_Debrief_Losses(string, section_page, line);
				}
				break;
			case 4:
				section_pages = talk_Count_Debrief_Captures();
				if (section_page < section_pages) {
					talk_Set_Officer_Mood(1);
					talk_Find_Debrief_Captures(string, section_page, line);
				}
				break;
		}
		section_page -= section_pages;
		line -= max_paragraph_size * section_pages;
	}
}

/* ======================================================================
 * Count_Debrief_* — page count helpers
 * ====================================================================== */

// FUNCTION: TIE95 0x699EB
// FUNCTION: TIE98 0x48AB10
static int16_t talk_Count_Debrief_Header(void) { return 1; }

// FUNCTION: TIE95 0x69A16
// FUNCTION: TIE98 0x48AB20
static void talk_Find_Debrief_Header(char* string, int16_t line) {
	int16_t i;

	i = 0;
	while (i < max_paragraph_size) {
		talk_Get_Debrief_Header(string, i);
		if (*string) {
			if (line) {
				*string = '\0';
				--line;
			} else
				break;
		}
		++i;
	}
}

/* ======================================================================
 * Find/talk_Get_Debrief_Header — mission header info
 * ====================================================================== */

// FUNCTION: TIE95 0x69A7C
// FUNCTION: TIE98 0x48AB60
static void talk_Get_Debrief_Header(char* string, int16_t line_idx) {
	signed char buf[80], fmt[40], rank_name[40];
	int16_t cur_battle = pilot_record.cur_battle;
	uint16_t pct;
	int16_t i;

	switch (line_idx) {
		case 0:
			shipext_Get_Battle_Ship_Name((char*)buf);
			strcpy(string, (char*)buf);
			textext_Cat_Text(string, mission.difficulty + txtTalkEasy);
			center_line = 1;
			break;
		case 1:
			textext_Copy_Text((char*)fmt, txtTalkBattle);
			for (i = 0; fmt[i]; ++i) {
				if (fmt[i] == '1')
					fmt[i] = 1;
				if (fmt[i] == '2')
					fmt[i] = 2;
			}
			if (shipext_Is_Mission_Success())
				sprintf((char*)buf, (char*)fmt, cur_battle + 1, pilot_record.battle_cursor[cur_battle]);
			else
				sprintf((char*)buf, (char*)fmt, cur_battle + 1, pilot_record.battle_cursor[cur_battle] + 1);
			strcpy(string, (char*)buf);
			textext_Copy_Text((char*)fmt, txtTalkScore);
			for (i = 0; fmt[i]; ++i) {
				if (fmt[i] == '1')
					fmt[i] = 1;
				if (fmt[i] == '2')
					fmt[i] = 2;
			}
			sprintf((char*)buf, (char*)fmt, mission.mission_score);
			strcat(string, (char*)buf);
			center_line = 1;
			break;
		case 2:
			textext_Copy_Text(string, txtTalkDash);
			center_line = 1;
			break;
		case 3:
			if (shipext_Is_Mission_Success())
				textext_Copy_Text((char*)buf, txtTalkSuccess);
			else
				textext_Copy_Text((char*)buf, txtTalkFailure);
			for (i = 0; buf[i]; ++i) {
				if (buf[i] == '1')
					buf[i] = 1;
				if (buf[i] == '2')
					buf[i] = 2;
			}
			strcpy(string, (char*)buf);
			break;
		case 4:
			if (mission.mission_new_rank) {
				textext_Copy_Text((char*)fmt, txtTalkRank);
				textext_Copy_Text((char*)rank_name, mission.mission_new_rank + 1);
				for (i = 0; fmt[i]; ++i) {
					if (fmt[i] == '1')
						fmt[i] = 1;
					if (fmt[i] == '2')
						fmt[i] = 2;
				}
				sprintf((char*)buf, (char*)fmt, rank_name);
				strcpy(string, (char*)buf);
				break;
			}
			/* fall through */
		case 5:
			if (shipext_Get_TOD_Medal()) {
				textext_Copy_Text((char*)buf, txtTalkMedal);
				for (i = 0; buf[i]; ++i) {
					if (buf[i] == '1')
						buf[i] = 1;
					if (buf[i] == '2')
						buf[i] = 2;
				}
				strcpy(string, (char*)buf);
				break;
			}
			/* fall through */
		case 6:
			textext_Copy_Text(string, txtTalkDash);
			center_line = 1;
			break;
		case 7:
			if (pstate.player_laser_hit)
				pct = 100 * pstate.player_laser_hit / pstate.player_laser_fired;
			else
				pct = 0;
			textext_Copy_Text((char*)fmt, txtCompInfoLaser);
			sprintf((char*)buf, (char*)fmt, pstate.player_laser_hit, pstate.player_laser_fired, pct);
			strcpy(string, (char*)buf);
			break;
		case 8:
			if (pstate.player_missile_hit)
				pct = 100 * pstate.player_missile_hit / pstate.player_missile_fired;
			else
				pct = 0;
			textext_Copy_Text((char*)fmt, txtCompInfoIon);
			sprintf((char*)buf, (char*)fmt, pstate.player_missile_hit, pstate.player_missile_fired, pct);
			strcpy(string, (char*)buf);
			break;
		case 9:
			if (pstate.player_warhead_hit > pstate.player_warhead_fired)
				pstate.player_warhead_fired = pstate.player_warhead_hit;
			if (pstate.player_warhead_hit)
				pct = 100 * pstate.player_warhead_hit / pstate.player_warhead_fired;
			else
				pct = 0;
			textext_Copy_Text((char*)fmt, txtCompInfoRocket);
			sprintf((char*)buf, (char*)fmt, pstate.player_warhead_hit, pstate.player_warhead_fired, pct);
			strcpy(string, (char*)buf);
			break;
	}
}

// FUNCTION: TIE95 0x6A024
// FUNCTION: TIE98 0x48B040
static int16_t talk_Count_Debrief_Goals(void) { return 1; }

// FUNCTION: TIE95 0x6A04F
// FUNCTION: TIE98 0x48B050
static void talk_Find_Debrief_Goals(char* string, int16_t line) {
	int16_t i;

	i = 0;
	while (i < max_paragraph_size) {
		talk_Get_Debrief_Goals(string, i);
		if (*string) {
			if (line) {
				*string = '\0';
				--line;
			} else
				break;
		}
		++i;
	}
}

/* ======================================================================
 * Find/talk_Get_Debrief_Goals — mission goal completion
 * ====================================================================== */

// FUNCTION: TIE95 0x6A0B5
// FUNCTION: TIE98 0x48B090
static void talk_Get_Debrief_Goals(char* string, int16_t line_idx) {
	char buf[80], count_str[40], fmt[40];
	int16_t cur_battle = pilot_record.cur_battle;
	int16_t fail;
	int16_t done;
	int16_t i;

	switch (line_idx) {
		case 0:
			shipext_Get_Battle_Ship_Name(buf);
			strcpy(string, buf);
			textext_Cat_Text(string, mission.difficulty + txtTalkEasy);
			break;
		case 1:
			textext_Copy_Text(fmt, txtTalkBattle);
			for (i = 0; fmt[i]; i++) {
				if (fmt[i] == '1')
					fmt[i] = 1;
				if (fmt[i] == '2')
					fmt[i] = 2;
			}
			if (shipext_Is_Mission_Success())
				sprintf(buf, fmt, cur_battle + 1, pilot_record.battle_cursor[cur_battle]);
			else
				sprintf(buf, fmt, cur_battle + 1, pilot_record.battle_cursor[cur_battle] + 1);
			strcpy(string, buf);
			textext_Copy_Text(fmt, txtTalkScore);
			for (i = 0; fmt[i]; i++) {
				if (fmt[i] == '1')
					fmt[i] = 1;
				if (fmt[i] == '2')
					fmt[i] = 2;
			}
			sprintf(buf, fmt, mission.mission_score);
			strcat(string, buf);
			break;
		case 2:
			textext_Copy_Text(string, txtTalkDash);
			break;
		case 3:
			done = fail = 0;
			done = goalsCompletedCount[0];
			fail = goalsCount[0] - goalsCompletedCount[0];
			if (done || fail) {
				if (mission.primary_complete == 1) {
					textext_Copy_Text(string, txtTalkAllPri);
				} else {
					if (!done) {
						textext_Copy_Text(count_str, txtTalkNo);
					} else {
						textext_Copy_Text(fmt, txtTalkOf);
						sprintf(count_str, fmt, done, done + fail);
					}
					textext_Copy_Text(fmt, txtTalkSomePri);
					sprintf(buf, fmt, count_str);
					strcpy(string, buf);
				}
			} else {
				*string = '\0';
			}
			break;
		case 4:
			done = fail = 0;
			done = goalsCompletedCount[1];
			fail = goalsCount[1] - goalsCompletedCount[1];
			if (done || fail) {
				if (mission.secondary_complete == 1) {
					textext_Copy_Text(string, txtTalkAllSec);
				} else {
					if (!done) {
						textext_Copy_Text(count_str, txtTalkNo);
					} else {
						textext_Copy_Text(fmt, txtTalkOf);
						sprintf(count_str, fmt, done, done + fail);
					}
					textext_Copy_Text(fmt, txtTalkSomeSec);
					sprintf(buf, fmt, count_str);
					strcpy(string, buf);
				}
			} else {
				*string = '\0';
			}
			break;
		case 5:
			done = fail = 0;
			done = goalsCompletedCount[2];
			fail = goalsCount[2] - goalsCompletedCount[2];
			if (done || fail) {
				if (mission.bonus_complete == 1) {
					textext_Copy_Text(string, txtTalkAllBonus);
				} else {
					if (!done) {
						textext_Copy_Text(count_str, txtTalkNo);
					} else {
						textext_Copy_Text(fmt, txtTalkOf);
						sprintf(count_str, fmt, done, done + fail);
					}
					textext_Copy_Text(fmt, txtTalkSomeBonus);
					sprintf(buf, fmt, count_str);
					strcpy(string, buf);
				}
			} else {
				*string = '\0';
			}
			break;
	}
	center_line = 1;
}

// FUNCTION: TIE95 0x6A542
// FUNCTION: TIE98 0x48B500
static int16_t talk_Count_Debrief_Kills(void) {
	int16_t count = 0;
	int16_t craft;

	for (craft = 0; craft < 69; craft++) {
		int16_t has_kill = 0;
		int16_t side;

		for (side = 0; side < 6; side++) {
			if (player_Is_Side_Enemy(side) && mission.kills_losses[side][craft])
				has_kill = 1;
		}
		if (has_kill)
			count++;
	}
	if (pstate.player_total_kills)
		count++;
	return (max_paragraph_size + count - 3) / (max_paragraph_size - 2);
}

/* ======================================================================
 * Debrief section Find_ renderers (paginated iteration)
 * ====================================================================== */

// FUNCTION: TIE95 0x6A61B
// FUNCTION: TIE98 0x48B590
static void talk_Find_Debrief_Kills(char* string, int16_t page, int16_t line) {
	int16_t craft;

	if (line % max_paragraph_size < 2) {
		if (line % max_paragraph_size) {
			textext_Copy_Text(string, txtTalkDash);
			center_line = 1;
		} else {
			talk_Get_Debrief_Kill_Title(string);
		}
	} else {
		line -= 2 * (page + 1);
		craft = 0;
		while (craft <= 69) {
			talk_Get_Debrief_Kills(string, craft);
			if (*string) {
				if (line) {
					*string = '\0';
					line -= 1;
				} else {
					break;
				}
			}
			++craft;
		}
	}
}

/* ======================================================================
 * Debrief title renderers — Get_Debrief_Kill/Loss/Capture_Title
 * ====================================================================== */

// FUNCTION: TIE95 0x6A6DB
// FUNCTION: TIE98 0x48B620
static void talk_Get_Debrief_Kill_Title(char* string) {
	signed char fmt[40], buf[80];
	uint16_t total_kills, player_total;
	uint16_t craft, side, frame_pad;
	(void)frame_pad;

	player_total = total_kills = 0;
	for (craft = 0; craft < 69; ++craft) {
		for (side = 0; side < 6; ++side) {
			if (player_Is_Side_Enemy(side))
				total_kills += mission.kills_losses[side][craft];
		}
		player_total += pstate.player_kills_per_species[craft];
	}
	textext_Copy_Text((char*)fmt, txtTalkDestroyed);
	for (craft = 0; fmt[craft]; ++craft) {
		if (fmt[craft] == '1')
			fmt[craft] = 1;
		if (fmt[craft] == '2')
			fmt[craft] = 2;
	}
#ifdef TIE_MODERN
	snprintf((char*)buf, sizeof(buf), (char*)fmt, total_kills, player_total);
#else
	sprintf(buf, fmt, total_kills, player_total);
#endif
	strcpy(string, (char*)buf);
	center_line = 1;
}

/* ======================================================================
 * Debrief entry renderers — talk_Get_Debrief_Kills/Losses/Captures
 * ====================================================================== */

// FUNCTION: TIE95 0x6A81A
// FUNCTION: TIE98 0x48B730
static void talk_Get_Debrief_Kills(char* string, int16_t craft_idx) {
	uint16_t count = 0;
	uint16_t side;
	char name[40];
	char buf[80];

	if (craft_idx < 69) {
		for (side = 0; side < 6; side++) {
			if (player_Is_Side_Enemy(side))
				count += mission.kills_losses[side][craft_idx];
		}
	} else {
		count += pstate.player_total_kills;
	}
	if (count) {
		if (craft_idx < 69) {
			textext_Get_Ship_Text(name, craft_idx);
			sprintf(buf, "  %s: %d(%d)", name, count, pstate.player_kills_per_species[craft_idx]);
		} else {
			textext_Get_Ship_Text(name, 84);
			sprintf(buf, "  %s: %d", name, count);
		}
		strcpy(string, buf);
	}
}

// FUNCTION: TIE95 0x6A92D
// FUNCTION: TIE98 0x48B840
static int16_t talk_Count_Debrief_Losses(void) {
	int16_t count = 0;
	int16_t craft;

	for (craft = 0; craft < 69; craft++) {
		int16_t has_loss = 0;
		int16_t side;

		for (side = 0; side < 6; side++) {
			if (!player_Is_Side_Enemy(side) && mission.kills_losses[side][craft])
				has_loss = 1;
		}
		if (has_loss)
			count++;
	}
	return (max_paragraph_size + count - 3) / (max_paragraph_size - 2);
}

// FUNCTION: TIE95 0x6A9F9
// FUNCTION: TIE98 0x48B8C0
static void talk_Find_Debrief_Losses(char* string, int16_t page, int16_t line) {
	int16_t craft;

	if (line % max_paragraph_size < 2) {
		if (line % max_paragraph_size) {
			textext_Copy_Text(string, txtTalkDash);
			center_line = 1;
		} else {
			talk_Get_Debrief_Loss_Title(string);
		}
	} else {
		line -= 2 * (page + 1);
		craft = 0;
		while (craft < 69) {
			talk_Get_Debrief_Losses(string, craft);
			if (*string) {
				if (line) {
					*string = '\0';
					line -= 1;
				} else {
					break;
				}
			}
			++craft;
		}
	}
}

// FUNCTION: TIE95 0x6AAB9
// FUNCTION: TIE98 0x48B950
static void talk_Get_Debrief_Loss_Title(char* string) {
	uint16_t total = 0;
	char fmt[40], buf[80];

	uint16_t craft;

	for (craft = 0; craft < 69; craft++) {
		uint16_t side;

		for (side = 0; side < 6; side++) {
			if (!player_Is_Side_Enemy(side))
				total += mission.kills_losses[side][craft];
		}
	}
	textext_Copy_Text(fmt, txtTalkLost);
	for (craft = 0; fmt[craft]; craft++) {
		if (fmt[craft] == '1')
			fmt[craft] = 1;
		if (fmt[craft] == '2')
			fmt[craft] = 2;
	}
	snprintf(buf, sizeof(buf), fmt, total);
	strcpy(string, buf);
	center_line = 1;
}

// FUNCTION: TIE95 0x6ABD9
// FUNCTION: TIE98 0x48BA30
static void talk_Get_Debrief_Losses(char* string, int16_t craft_idx) {
	uint16_t count = 0;
	char name[40];
	char buf[80];

	if (craft_idx < 69) {
		int16_t side;

		for (side = 0; side < 6; side++) {
			if (!player_Is_Side_Enemy(side))
				count += mission.kills_losses[side][craft_idx];
		}
	}
	if (count) {
		textext_Get_Ship_Text(name, craft_idx);
		snprintf(buf, sizeof(buf), "  %s: %d", name, count);
		strcpy(string, buf);
	}
}

// FUNCTION: TIE95 0x6AC8E
// FUNCTION: TIE98 0x48BAD0
static int16_t talk_Count_Debrief_Captures(void) {
	int16_t count = 0;
	int16_t craft;

	for (craft = 0; craft < 69; craft++) {
		if (mission.captures_by_type[craft])
			count++;
	}
	return (max_paragraph_size + count - 3) / (max_paragraph_size - 2);
}

// FUNCTION: TIE95 0x6AD10
// FUNCTION: TIE98 0x48BB00
static void talk_Find_Debrief_Captures(char* string, int16_t page, int16_t line) {
	int16_t craft;

	if (line % max_paragraph_size < 2) {
		if (line % max_paragraph_size) {
			textext_Copy_Text(string, txtTalkDash);
			center_line = 1;
		} else {
			talk_Get_Debrief_Capture_Title(string);
		}
	} else {
		line -= 2 * (page + 1);
		craft = 0;
		while (craft < 69) {
			talk_Get_Debrief_Captures(string, craft);
			if (*string) {
				if (line) {
					*string = '\0';
					line -= 1;
				} else {
					break;
				}
			}
			++craft;
		}
	}
}

// FUNCTION: TIE95 0x6ADD0
// FUNCTION: TIE98 0x48BB90
static void talk_Get_Debrief_Capture_Title(char* string) {
	uint16_t total = 0;
	char fmt[40], buf[80];

	uint16_t craft;

	for (craft = 0; craft < 69; craft++)
		total += mission.captures_by_type[craft];

	textext_Copy_Text(fmt, txtTalkCaptured);
	for (craft = 0; fmt[craft]; craft++) {
		if (fmt[craft] == '1')
			fmt[craft] = 1;
		if (fmt[craft] == '2')
			fmt[craft] = 2;
	}
	snprintf(buf, sizeof(buf), fmt, total);
	strcpy(string, buf);
	center_line = 1;
}

// FUNCTION: TIE95 0x6AEBC
// FUNCTION: TIE98 0x48BC40
static void talk_Get_Debrief_Captures(char* string, int16_t craft_idx) {
	uint16_t count = mission.captures_by_type[craft_idx];
	if (count) {
		char name[40];
		char buf[80];
		textext_Get_Ship_Text(name, craft_idx);
		snprintf(buf, sizeof(buf), "  %s: %d", name, count);
		strcpy(string, buf);
	}
}

/* Initialize species/mission for a talk briefing/debrief. Mirrors
 * retail TALK_Set_Voice_Species_Mission (sub_6AF31): species and
 * mission come from the pilot record's tour-battle position; the
 * mood char is forced to 'h' (hostile/failed) when the relevant
 * objective is incomplete. */
// FUNCTION: TIE95 0x6AF31
// FUNCTION: TIE98 0x48BCC0
void talk_Set_Voice_Species_Mission(void) {
	uint8_t cur = pilot_record.cur_battle;
	talk_voice_species = (int16_t)(cur + 1);
	talk_voice_mission = (int16_t)(pilot_record.battle_cursor[cur] + 1);

	if (talk_voice_mood == 'b')
		return; /* Briefing — no failure-mood patch */

	if (talk_voice_officer == 'o') {
		if (shipext_Is_Mission_Success())
			talk_voice_mission = (int16_t)pilot_record.battle_cursor[cur];
		else
			talk_voice_mood = 'h';
	} else {
		if (shipext_Is_Mission_Success())
			talk_voice_mission = (int16_t)pilot_record.battle_cursor[cur];
		if (mission.secondary_complete != 1)
			talk_voice_mood = 'h';
	}
}

/* Stop any running speech, build the next .voc filename, chain it on
 * the stream engine, prime the staging buffer, and start playback.
 * The modern build reads the whole file synchronously instead, and
 * falls back silently if speech is disabled or the .voc file is
 * missing — the screen still works without voice. */
// FUNCTION: TIE95 0x6AFF9
void talk_Start_Speech_Stream(void) {
	char path[64];
	char sp[5];

#ifdef TIE_MODERN
	if (!options_gbl.speech_active)
		return;
#endif
	memset(sp, 0, sizeof(sp));
	/* Species prefix: numeric for tour battles, a letter for the
	 * negative (character-encoded) training/historical species. */
	if (talk_voice_species < 0) {
		talk_voice_species = -talk_voice_species;
		switch (talk_voice_species) {
			case 1:
				sp[0] = 'f';
				break;
			case 2:
				sp[0] = 'i';
				break;
			case 3:
				sp[0] = 'b';
				break;
			case 4:
				sp[0] = 'a';
				break;
			case 5:
				sp[0] = 'g';
				break;
			case 6:
				sp[0] = 'd';
				break;
			case 7:
				sp[0] = 'm';
				break;
			default:
				printf("error with filename");
				sp[0] = 'f';
				break;
		}
		talk_voice_species = -talk_voice_species;
	} else {
		sprintf(sp, "%d", talk_voice_species);
	}

	if (talk_voice_officer == 'i') {
		/* Briefing-officer voiceover: no mood character. Missions whose
		 * name ends in 'w' use a one-shorter voice list. */
		const char* mission_name = shipext_Get_Mission_Name();
		if (mission_name[strlen(mission_name) - 1] == 'w')
			sprintf(path, "\\voice\\%sm%d\\%sm%d%c%d.voc", sp, talk_voice_mission, sp, talk_voice_mission,
					talk_voice_officer, talk_voice_question - 1);
		else
			sprintf(path, "\\voice\\%sm%d\\%sm%d%c%d.voc", sp, talk_voice_mission, sp, talk_voice_mission,
					talk_voice_officer, talk_voice_question);
	} else if (strcmp(sp, "1") == 0 && talk_voice_mission == 1 && talk_voice_mood == 'h' &&
			   mission.primary_complete != 1 && mission.secondary_complete == 1) {
		/* Mission-1 secondary-only retry: fixed files for the first
		 * two questions. */
		if (talk_voice_question == 1)
			strcpy(path, "\\voice\\1m1\\1m1od2.voc");
		else if (talk_voice_question == 2)
			strcpy(path, "\\voice\\1m1\\1m1oh1.voc");
#ifdef TIE_MODERN
		/* Retail leaves the path unset for later questions. */
		else
			return;
#endif
	} else {
		sprintf(path, "\\voice\\%sm%d\\%sm%d%c%c%d.voc", sp, talk_voice_mission, sp, talk_voice_mission,
				talk_voice_officer, talk_voice_mood, talk_voice_question);
	}

	if (talk_speech_sound && talk_speech_sound->data) {
		xsound_Stop_Sound(talk_speech_sound);
		talk_speech_streaming = 0;
		talk_speech_pos = 0;
#ifdef TIE_MODERN
		{
			/* MODERN ADAPTATION: load the whole file synchronously instead
			 * of chaining it through the CD streamer. */
			TieFrontendVoiceSource voice_source;
			TieFile* fp;
			uint8_t* data;
			size_t bytes_read;
			uint32_t source_rate_hz;
			TieVocCompatResult voc_compat;

			fp = TieFrontendVoice_Open(path, &voice_source);
			if (!fp) {
				TieDiagnostics_Log(TIE_LOG_INFO, "[talk-voice] missing %s\n", path);
				return;
			}
			TieDiagnostics_Log(TIE_LOG_INFO, "[talk-voice] play %s source=%s\n", path,
							   TieFrontendVoice_SourceName(voice_source));

			data = xmemhdl_Lock_Handle(talk_speech_sound->data);
			if (!data) {
				TieStorage_Close(fp);
				return;
			}
			memset(data, 0, TALK_SPEECH_BUF_SIZE);
			bytes_read = TieStorage_Read(data, 1, TALK_SPEECH_BUF_SIZE, fp);
			TieStorage_Close(fp);
			source_rate_hz = 0;
			/* TIE98 ships VOC 1.20/type-9 PCM, while the recovered TIE95
			 * iMUSE dispatcher consumes VOC 1.10/type-1 blocks. */
			voc_compat = TieVocCompat_PrepareImuse(data, &bytes_read, &source_rate_hz);
			xmemhdl_Unlock_Handle(talk_speech_sound->data);
			if (voc_compat == TIE_VOC_COMPAT_INVALID) {
				TieDiagnostics_Log(TIE_LOG_WARN, "[talk-voice] unsupported VOC format in %s\n", path);
				talk_speech_sound->size = 0;
				return;
			}

			talk_speech_pos = (int32_t)bytes_read;
			talk_speech_sound->size = (int32_t)bytes_read;
			talk_speech_streaming = (bytes_read >= TALK_SPEECH_BUF_SIZE);
			if (bytes_read == 0)
				return;

			xsound_Start_Speech(talk_speech_sound);
			if (voc_compat == TIE_VOC_COMPAT_CONVERTED)
				(void)lolevel_ImSetParam((intptr_t)talk_speech_sound, IMUSE_PARAM_SOUND_FREQUENCY,
										 (int)source_rate_hz);
			lolevel_ImSetParam((intptr_t)talk_speech_sound, 0x500, 100);
		}
#else
		xstream_Unchain_Current_Stream_File(0);
		if (xstream_Chain_Stream_File(0, path) && xstream_Use_Stream_File(0, path)) {
			uint8_t* data;
			int32_t bytes_read;

			data = xmemhdl_Lock_Handle(talk_speech_sound->data);
			memset(data, 0, TALK_SPEECH_BUF_SIZE);
			xmemhdl_Unlock_Handle(talk_speech_sound->data);
			bytes_read =
				xstream_Read_From_Stream_Buffer(0, talk_speech_sound->data, 0, TALK_SPEECH_PRIME_SIZE, 1);
			if (bytes_read == TALK_SPEECH_PRIME_SIZE)
				talk_speech_streaming = 1;
			else
				talk_speech_streaming = 0;
			talk_speech_pos = bytes_read;
			talk_speech_sound->size = bytes_read;
			xsound_Start_Speech(talk_speech_sound);
			lolevel_ImSetParam((intptr_t)talk_speech_sound, 0x500, 100);
		}
#endif
	}
}

/* Allocate the talk-speech Sound + 2 MB streaming buffer. Mirrors
 * retail TALK_Alloc_Speech_Sound (sub_6B363). */
// FUNCTION: TIE95 0x6B363
void talk_Alloc_Speech_Sound(void) {
	talk_speech_sound = xsound_Alloc_Sound(LANDRU_NULL_HANDLE, 0, 0);
	talk_speech_sound->type = digitalSound;
	/* Retail writes 0x564F4943 (= FOURCC_VOIC as a little-endian DWORD)
	 * so anything that looks at the sound list by res_type matches. */
	talk_speech_sound->res_type = FOURCC_VOIC;
	talk_speech_pos = 0;
	talk_speech_streaming = 0;
	talk_speech_sound->data = xmemhdl_Alloc_Clear_Handle(TALK_SPEECH_BUF_SIZE, LANDRU_MEMORY_DEFAULT);

	/* Match retail flag clears so the sound is freed normally on
	 * scene shutdown rather than being held alive. */
	xsound_Discard_Sound_Data(talk_speech_sound);
	xsound_Clear_Sound_Keep(talk_speech_sound);
	xsound_Clear_Sound_Keepable(talk_speech_sound);
	xsound_Clear_Sound_User_Keep(talk_speech_sound);
	xsound_Set_Sound_User_Function(talk_speech_sound, talk_Speech_User_Func);
}

/* Tear down the streaming state. Retail leaves the Sound and its
 * buffer to be reclaimed by the global sound free pass at scene
 * shutdown; we do the same — just clear our reference so the next
 * scene can re-allocate. */
// FUNCTION: TIE95 0x6B400
void talk_Free_Speech_Sound(void) {
	talk_speech_sound = NULL;
	talk_speech_pos = 0;
	talk_speech_streaming = 0;
}

/* ======================================================================
 * Voice-over streaming
 * ====================================================================== */

/* Per-frame user callback on the streaming Sound: append the next 2 KB
 * chunk from the CD streamer. Our synchronous loader normally reads the
 * whole file up front, leaving talk_speech_streaming clear. */
// FUNCTION: TIE95 0x6B43F
static void talk_Speech_User_Func(Sound* snd, int32_t time) {
	int32_t bytes_read;

	(void)snd;
	(void)time;
	if (talk_speech_streaming) {
		if (talk_speech_sound) {
			if (talk_speech_sound->data) {
				bytes_read =
					xstream_Read_From_Stream_Buffer(0, talk_speech_sound->data, talk_speech_pos, 0x800, 0);
				if (bytes_read != -1) {
					if (bytes_read != 0x800)
						talk_speech_streaming = 0;
					talk_speech_pos += bytes_read;
					talk_speech_sound->size += bytes_read;
				}
			}
		}
	}
}
