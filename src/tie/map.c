#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tie/goals.h"
#include "tie/map.h"
#include "tie/mission.h"
#include "tie/player.h"
#include "tie/shade.h"
#include "tie/shellext.h"
#include "tie/shipext.h"
#include "tie/soundext.h"
#include "tie/talk.h"
#include "tie/textext.h"
#include "tie/tie.h"
#include "tie_runtime/snapshot/snapshot.h"
#include "tie_runtime/snapshot/snapshot_internal.h"
#include "tie_runtime/storage/pilot_storage.h"
#include "tie_runtime/storage/score_tables.h"
#include <landru/task.h>

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
#include "landru/paint.h"
#include "landru/pal.h"
#include "landru/rect.h"
#include "landru/res.h"
#include "landru/view.h"
#include "landru/viewadd.h"

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

static const char map_str[17][14] = { "map.lfd",    "brfmap1", "brfpnl",  "cmbticns", "cmbtmap1", "combatvr",
									  "player.lfd", "brfpnl",  "brfmap2", "pnlhldr",  "brfbutns", "cmbtmap3",
									  "title",      "trnmap1", "trnmap2", "trnmap3",  "trntitle" };

static const int16_t map_panel_y[5] = { 43, 29, 14, 10, 11 };
static const int16_t map_panel_hdl_y[5] = { 92, 80, 89, 84, 85 };
// GLOBAL: TIE 0xCFA6E
static const int16_t map_panel_hdl_cel[5] = { 0, 0, 2, 2, 2 };

/* Button rects: [0-5]=training/combat, [6-9]=briefing (offset by index) */
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

/* Training score file, kept in the shared TIE98-capable representation. */
static TrainingScoreEntry debrief_train_scores[TRAIN_SCORE_ENTRY_COUNT];

/* ======================================================================
 * Static BSS globals
 * ====================================================================== */

static int16_t talk_win_id[6];
static Input* stop_input;
static Input* play_input;
static EBriefStruct* talk_brief;
static Input* map_input;
static Input* talk_input;
// GLOBAL: TIE 0xF5968
static Actor* title_actor;
static Actor* cmbticons;
static Film* brief_film;
static EFArrayStruct* talk_fgroup;
static Palette* cmbtpal;
static int16_t talk_win_status[5];
// GLOBAL: TIE 0xF6126
static int16_t max_paragraph_size;
static int16_t map_text;
static int16_t map_text_count;
static int16_t num_talk_paragraphs;
static int16_t combat_pilot_medal_status;
static int16_t center_line;
// GLOBAL: TIE 0xF6134
static int16_t cur_talk_paragraph;
static int16_t combat_pilot_medal_init;
static int16_t talk_mode;
static int16_t num_talk_questions;
static int16_t next_mode;
static int16_t talk_person;
static int16_t cur_talk_question;
// GLOBAL: TIE 0xCFA78
static uint8_t map_uses_battle_voice;
// GLOBAL: TIE 0xF6144
static uint8_t map_is_post_mission;

/* extern per watdbg */
int16_t train_pilot_medal_status;

/* Forward declarations — only for functions called before their definition */
static int16_t Count_VR_Debrief_Pages(void);
static void Get_VR_Debrief_Line(char* string, int16_t line);

/* ======================================================================
 * Helper: replace ASCII '1'/'2' with italic control codes 0x01/0x02
 * ====================================================================== */

static void replace_italic_codes(char* str) {
	for (int16_t i = 0; str[i]; i++) {
		if (str[i] == '1')
			str[i] = 1;
		if (str[i] == '2')
			str[i] = 2;
	}
}

/* ======================================================================
 * View callback
 * ====================================================================== */

/* Tracks the last paragraph id whose voice we triggered. Compared
 * against talk_voice_question every frame to detect when the
 * briefing engine advances to a new paragraph (BCMD_SHOW_PARA1
 * stamps the new id) and we should fire the next .voc. */
static int16_t last_voiced_paragraph;

static void end_View(int32_t refresh) {
	/* Briefing-map voice path. Retail MAP_end_View does the same:
	 * when the page id stamped by PLAYER_Step_Page differs from the
	 * last-voiced id and the officer character is 'i' (info
	 * briefing), play the corresponding .voc. The talk-mode and
	 * combat-debrief paths run their voice trigger from
	 * iuser_Map / Set_VR_Talk_To_Text instead — only 'i' gets the
	 * end-view treatment. */
	if (last_voiced_paragraph != talk_voice_question && talk_voice_officer == 'i') {
		talk_Start_Speech_Stream();
		last_voiced_paragraph = talk_voice_question;
	}

	if (!refresh && !lcursor_Is_Cursor_Visible())
		lcursor_Show_Cursor();
}

/* ======================================================================
 * Panel actor callbacks
 * ====================================================================== */

static void user_Map_Panel(Actor* actor, int32_t time) {
	if (actor->id == 0) {
		actor->y = (time <= 4) ? map_panel_y[time] : map_panel_y[4];
	} else if (actor->id == 1) {
		int16_t y_val = (time <= 4) ? map_panel_hdl_y[time] : map_panel_hdl_y[4];
		int16_t cel_val = (time <= 4) ? map_panel_hdl_cel[time] : map_panel_hdl_cel[4];
		actor->y = y_val;
		lactor_Set_Actor_State(actor, cel_val, 0);
	}
}

static int draw_Map_Text(Actor* actor, Rect* r, Rect* clip_r, int16_t x, int16_t y, int16_t refresh) {
	lactdelt_Draw_Delta_Actor(actor, r, clip_r, x, y, refresh);

	if (map_text != -1) {
		lactdelt_Draw_Delta_Actor(title_actor, r, clip_r, x, y, refresh);
		Rect tr;
		char name[48];
		lrect_Set_Rect(&tr, 60, 158, 260, 170);
		textext_Copy_Text(name, map_text);
		lfont_Enable_FontID_Shadow(0);
		lfont_Print_Centered_Text(name, &tr, 28, 0);
		lfont_Disable_FontID_Shadow(0);
	}

	if (map_text_count <= 0)
		map_text = -1;
	else
		map_text_count--;

	return 1;
}

/* ======================================================================
 * Check_VR_Talk_Questions — scan talk data visibility conditions
 * ====================================================================== */

static void Check_VR_Talk_Questions(void) {
	for (int16_t i = 0; i < 5; i++) {
		int16_t status = 0;
		char* data = (char*)talk_brief->talk_data[5 * talk_person + i];
		if (data) {
			if (data[0]) {
				status = 1;
				for (int16_t j = 0; data[j] && data[j] != '\n'; j++) {
					if (data[j] == 4 || data[j] == 5) {
						int8_t param = data[j + 1];
						status = (data[j] == 4) ? param + 1 : param + 3;
						break;
					}
				}
			}
		}
		talk_win_status[i] = status;
	}
}

/* ======================================================================
 * VR talk text functions
 * ====================================================================== */

static void Get_VR_Talk_Question(char* string, int16_t question) {
	*string = '\0';
	if (question < 0 || question >= num_talk_questions)
		return;

	int16_t qid = talk_win_id[question];
	if (qid == 5) {
		textext_Copy_Text(string, txtTalkDebrief);
		return;
	}

	char* data = (char*)talk_brief->talk_data[5 * talk_person + qid];
	if (!data)
		return;

	int16_t out_len = 0;
	for (int16_t i = 0; data[i] && data[i] != '\n'; i++) {
		if (data[i] == 4 || data[i] == 5)
			i++;
		else
			string[out_len++] = data[i];
	}
	string[out_len] = '\0';
}

static void Get_VR_Talk_Paragraph(char* string, int16_t line) {
	int16_t bold = 0;
	*string = '\0';

	if (cur_talk_question < 0 || cur_talk_question >= num_talk_questions)
		return;

	int16_t qid = talk_win_id[cur_talk_question];
	if (qid == 5) {
		Get_VR_Debrief_Line(string, line);
		return;
	}

	char* data = (char*)talk_brief->talk_data[5 * talk_person + qid];
	if (!data)
		return;

	/* Skip first line (question title) */
	int16_t pos = 0;
	while (data[pos] && data[pos] != '\n')
		pos++;
	pos++;

	int16_t line_idx = 0;
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
}

static void Set_VR_Talk_Paragraph(void) {
	if (cur_talk_question < 0 || cur_talk_question >= num_talk_questions)
		return;

	int16_t qid = talk_win_id[cur_talk_question];
	if (qid == 5) {
		num_talk_paragraphs = Count_VR_Debrief_Pages();
		cur_talk_paragraph = 0;
		return;
	}

	char* data = (char*)talk_brief->talk_data[5 * talk_person + qid];
	if (!data)
		return;

	int16_t pos = 0;
	while (data[pos] && data[pos] != '\n')
		pos++;
	pos++;

	int16_t line_count = 0;
	while (data[pos]) {
		while (data[pos] && data[pos] != '\n')
			pos++;
		if (data[pos])
			pos++;
		line_count++;
	}

	num_talk_paragraphs = (max_paragraph_size + line_count - 1) / max_paragraph_size;
	cur_talk_paragraph = 0;
}

static void Set_VR_Talk_To_Text(int16_t person) {
	num_talk_questions = 0;
	cur_talk_question = 0;
	num_talk_paragraphs = 0;
	cur_talk_paragraph = 0;
	talk_person = person;

	if (shellext_Get_Cur_Scene() == SCENE_COMBAT_MAP_B)
		talk_person += 2;

	Check_VR_Talk_Questions();

	for (int16_t i = 0; i < 5; i++) {
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
		num_talk_paragraphs = Count_VR_Debrief_Pages();
		cur_talk_paragraph = 0;
	}

	Set_VR_Talk_Paragraph();

	/* Retail MAP_Set_VR_Talk_To_Text fires the current paragraph's
	 * voice file at the end (gated on officer != 'i' since the
	 * info-briefing path is driven by end_View instead). Lets the
	 * combat-sim scenes hear the first phrase on entry. */
	if (talk_voice_officer != 'i')
		talk_Start_Speech_Stream();
}

/* ======================================================================
 * VR debrief — count/find/get for all 5 sections
 * ====================================================================== */

static int16_t Count_VR_Debrief_Header(void) { return 1; }
static int16_t Count_VR_Debrief_Goals(void) { return 1; }

static int16_t Count_VR_Debrief_Kills(void) {
	int16_t count = 0;
	for (int16_t i = 0; i < (int16_t)NUM_SPEC; i++) {
		for (int16_t j = 0; j < 6; j++) {
			if (player_Is_Side_Enemy(j) && mission.kills_losses[j][i]) {
				count++;
				break;
			}
		}
	}
	if (pstate.player_total_kills)
		count++;
	return (count + max_paragraph_size - 3) / (max_paragraph_size - 2);
}

static int16_t Count_VR_Debrief_Losses(void) {
	int16_t count = 0;
	for (int16_t i = 0; i < (int16_t)NUM_SPEC; i++) {
		for (int16_t j = 0; j < 6; j++) {
			if (!player_Is_Side_Enemy(j) && mission.kills_losses[j][i]) {
				count++;
				break;
			}
		}
	}
	return (count + max_paragraph_size - 3) / (max_paragraph_size - 2);
}

static int16_t Count_VR_Debrief_Captures(void) {
	int16_t count = 0;
	for (int16_t i = 0; i < (int16_t)NUM_SPEC; i++) {
		if (mission.captures_by_type[i])
			count++;
	}
	return (count + max_paragraph_size - 3) / (max_paragraph_size - 2);
}

static int16_t Count_VR_Debrief_Pages(void) {
	return Count_VR_Debrief_Header() + Count_VR_Debrief_Goals() + Count_VR_Debrief_Kills() +
		   Count_VR_Debrief_Losses() + Count_VR_Debrief_Captures();
}

/* --- Standard header (shared by header + goals Find functions) --- */

static void Get_VR_Standard_Debrief_Header(char* string, int16_t line) {
	char buf[80], fmt[40];

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
				replace_italic_codes(fmt);
				snprintf(buf, sizeof(buf), fmt, shipext_Get_Combat_Mission() + 1);
			} else {
				textext_Copy_Text(fmt, txtTalkBattle);
				replace_italic_codes(fmt);
				snprintf(buf, sizeof(buf), fmt, combat_ship - 11, shipext_Get_Combat_Mission() + 1);
			}
			strcpy(string, buf);
			textext_Copy_Text(fmt, txtTalkScore);
			replace_italic_codes(fmt);
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

/* --- Header section --- */

static void Get_VR_Debrief_Header(char* string, int16_t line) {
	char buf[80], fmt[40], rank_name[40];
	uint16_t val;

	switch (line) {
		case 3:
			if (shipext_Is_Combat_Mission_Success())
				textext_Copy_Text(buf, txtTalkSuccess);
			else
				textext_Copy_Text(buf, txtTalkFailure);
			replace_italic_codes(buf);
			strcpy(string, buf);
			return;
		case 4:
			if (!mission.mission_new_rank)
				return;
			textext_Copy_Text(fmt, txtTalkRank);
			textext_Copy_Text(rank_name, mission.mission_new_rank + 1);
			replace_italic_codes(fmt);
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
			replace_italic_codes(buf);
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

static void Find_VR_Debrief_Header(char* string, int16_t line) {
	int16_t skip = line;
	for (int16_t i = 0; i < max_paragraph_size; i++) {
		if (i >= 3)
			Get_VR_Debrief_Header(string, i);
		else
			Get_VR_Standard_Debrief_Header(string, i);
		if (*string) {
			if (!skip)
				return;
			*string = '\0';
			skip--;
		}
	}
}

/* --- Goals section --- */

static void Get_VR_Debrief_Goals(char* string, int16_t line) {
	char buf[80], fmt[40], count_str[40];
	int16_t done, fail;

	if (line == 3) {
		done = (uint16_t)goalsCompletedCount[0];
		fail = (uint16_t)((uint16_t)goalsCount[0] - (uint16_t)goalsCompletedCount[0]);
		if (!done && !fail) {
			*string = '\0';
			return;
		}
		if (mission.primary_complete == 1) {
			textext_Copy_Text(string, txtTalkAllPri);
		} else {
			if (done) {
				textext_Copy_Text(fmt, txtTalkOf);
				snprintf(count_str, sizeof(count_str), fmt, done, done + fail);
			} else
				textext_Copy_Text(count_str, txtTalkNo);
			textext_Copy_Text(fmt, txtTalkSomePri);
			snprintf(buf, sizeof(buf), fmt, count_str);
			strcpy(string, buf);
		}
	} else if (line == 4) {
		done = (uint16_t)goalsCompletedCount[1];
		fail = (uint16_t)((uint16_t)goalsCount[1] - (uint16_t)goalsCompletedCount[1]);
		if (!done && !fail) {
			*string = '\0';
			return;
		}
		if (mission.secondary_complete == 1) {
			textext_Copy_Text(string, txtTalkAllSec);
		} else {
			if (done) {
				textext_Copy_Text(fmt, txtTalkOf);
				snprintf(count_str, sizeof(count_str), fmt, done, done + fail);
			} else
				textext_Copy_Text(count_str, txtTalkNo);
			textext_Copy_Text(fmt, txtTalkSomeSec);
			snprintf(buf, sizeof(buf), fmt, count_str);
			strcpy(string, buf);
		}
	} else if (line == 5) {
		done = (uint16_t)goalsCompletedCount[2];
		fail = (uint16_t)((uint16_t)goalsCount[2] - (uint16_t)goalsCompletedCount[2]);
		if (!done && !fail) {
			*string = '\0';
			return;
		}
		if (mission.bonus_complete == 1) {
			textext_Copy_Text(string, txtTalkAllBonus);
		} else {
			if (done) {
				textext_Copy_Text(fmt, txtTalkOf);
				snprintf(count_str, sizeof(count_str), fmt, done, done + fail);
			} else
				textext_Copy_Text(count_str, txtTalkNo);
			textext_Copy_Text(fmt, txtTalkSomeBonus);
			snprintf(buf, sizeof(buf), fmt, count_str);
			strcpy(string, buf);
		}
	}
	center_line = 1;
}

static void Find_VR_Debrief_Goals(char* string, int16_t line) {
	int16_t skip = line;
	for (int16_t i = 0; i < max_paragraph_size; i++) {
		if (i >= 3)
			Get_VR_Debrief_Goals(string, i);
		else
			Get_VR_Standard_Debrief_Header(string, i);
		if (*string) {
			if (!skip)
				return;
			*string = '\0';
			skip--;
		}
	}
}

/* --- Kills section --- */

static void Get_VR_Debrief_Kill_Title(char* string) {
	uint16_t total = 0, player_total = 0;
	char fmt[40], buf[80];
	for (uint16_t i = 0; i < NUM_SPEC; i++) {
		for (uint16_t j = 0; j < 6; j++) {
			if (player_Is_Side_Enemy(j))
				total += mission.kills_losses[j][i];
		}
		player_total += pstate.player_kills_per_species[i];
	}
	textext_Copy_Text(fmt, txtTalkDestroyed);
	replace_italic_codes(fmt);
	snprintf(buf, sizeof(buf), fmt, total, player_total);
	strcpy(string, buf);
	center_line = 1;
}

static void Get_VR_Debrief_Kills(char* string, int16_t craft_idx) {
	uint16_t count = 0;
	char name[40], buf[80];
	if (craft_idx >= (int16_t)NUM_SPEC) {
		count = pstate.player_total_kills;
	} else {
		for (int16_t j = 0; j < 6; j++) {
			if (player_Is_Side_Enemy(j))
				count += mission.kills_losses[j][craft_idx];
		}
	}
	if (count) {
		if (craft_idx >= (int16_t)NUM_SPEC) {
			textext_Get_Ship_Text(name, 84);
			snprintf(buf, sizeof(buf), "  %s: %d", name, count);
		} else {
			textext_Get_Ship_Text(name, craft_idx);
			snprintf(buf, sizeof(buf), "  %s: %d(%d)", name, count,
					 pstate.player_kills_per_species[craft_idx]);
		}
		strcpy(string, buf);
	}
}

static void Find_VR_Debrief_Kills(char* string, int16_t page, int16_t line) {
	int16_t in_page = line % max_paragraph_size;
	if (in_page == 0) {
		Get_VR_Debrief_Kill_Title(string);
	} else if (in_page == 1) {
		textext_Copy_Text(string, txtTalkDash);
		center_line = 1;
	} else {
		int16_t skip = line - 2 * (page + 1);
		for (int16_t i = 0; i <= (int16_t)NUM_SPEC; i++) {
			Get_VR_Debrief_Kills(string, i);
			if (*string) {
				if (!skip)
					return;
				skip--;
				*string = '\0';
			}
		}
	}
}

/* --- Losses section --- */

static void Get_VR_Debrief_Loss_Title(char* string) {
	uint16_t total = 0;
	char fmt[40], buf[80];
	for (uint16_t i = 0; i < NUM_SPEC; i++) {
		for (uint16_t j = 0; j < 6; j++) {
			if (!player_Is_Side_Enemy(j))
				total += mission.kills_losses[j][i];
		}
	}
	textext_Copy_Text(fmt, txtTalkLost);
	replace_italic_codes(fmt);
	snprintf(buf, sizeof(buf), fmt, total);
	strcpy(string, buf);
	center_line = 1;
}

static void Get_VR_Debrief_Losses(char* string, int16_t craft_idx) {
	uint16_t count = 0;
	char name[40], buf[80];
	if (craft_idx < (int16_t)NUM_SPEC) {
		for (int16_t j = 0; j < 6; j++) {
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

static void Find_VR_Debrief_Losses(char* string, int16_t page, int16_t line) {
	int16_t in_page = line % max_paragraph_size;
	if (in_page == 0) {
		Get_VR_Debrief_Loss_Title(string);
	} else if (in_page == 1) {
		textext_Copy_Text(string, txtTalkDash);
		center_line = 1;
	} else {
		int16_t skip = line - 2 * (page + 1);
		for (int16_t i = 0; i < (int16_t)NUM_SPEC; i++) {
			Get_VR_Debrief_Losses(string, i);
			if (*string) {
				if (!skip)
					return;
				skip--;
				*string = '\0';
			}
		}
	}
}

/* --- Captures section --- */

static void Get_VR_Debrief_Capture_Title(char* string) {
	uint16_t total = 0;
	char fmt[40], buf[80];
	for (uint16_t i = 0; i < NUM_SPEC; i++)
		total += mission.captures_by_type[i];
	textext_Copy_Text(fmt, txtTalkCaptured);
	replace_italic_codes(fmt);
	snprintf(buf, sizeof(buf), fmt, total);
	strcpy(string, buf);
	center_line = 1;
}

static void Get_VR_Debrief_Captures(char* string, int16_t craft_idx) {
	uint16_t count = mission.captures_by_type[craft_idx];
	if (count) {
		char name[40], buf[80];
		textext_Get_Ship_Text(name, craft_idx);
		snprintf(buf, sizeof(buf), "  %s: %d", name, count);
		strcpy(string, buf);
	}
}

static void Find_VR_Debrief_Captures(char* string, int16_t page, int16_t line) {
	int16_t in_page = line % max_paragraph_size;
	if (in_page == 0) {
		Get_VR_Debrief_Capture_Title(string);
	} else if (in_page == 1) {
		textext_Copy_Text(string, txtTalkDash);
		center_line = 1;
	} else {
		int16_t skip = line - 2 * (page + 1);
		for (int16_t i = 0; i < (int16_t)NUM_SPEC; i++) {
			Get_VR_Debrief_Captures(string, i);
			if (*string) {
				if (!skip)
					return;
				skip--;
				*string = '\0';
			}
		}
	}
}

/* --- Debrief line dispatcher --- */

static void Get_VR_Debrief_Line(char* string, int16_t line) {
	int16_t abs_line = line;
	int16_t section_page = line / max_paragraph_size;
	*string = '\0';
	center_line = 0;

	for (int16_t s = 0; s < 5 && section_page >= 0; s++) {
		int16_t pages;
		switch (s) {
			case 0:
				pages = Count_VR_Debrief_Header();
				if (section_page < pages)
					Find_VR_Debrief_Header(string, abs_line);
				break;
			case 1:
				pages = Count_VR_Debrief_Goals();
				if (section_page < pages)
					Find_VR_Debrief_Goals(string, abs_line);
				break;
			case 2:
				pages = Count_VR_Debrief_Kills();
				if (section_page < pages)
					Find_VR_Debrief_Kills(string, section_page, abs_line);
				break;
			case 3:
				pages = Count_VR_Debrief_Losses();
				if (section_page < pages)
					Find_VR_Debrief_Losses(string, section_page, abs_line);
				break;
			case 4:
				pages = Count_VR_Debrief_Captures();
				if (section_page < pages)
					Find_VR_Debrief_Captures(string, section_page, abs_line);
				break;
		}
		section_page -= pages;
		abs_line -= pages * max_paragraph_size;
	}
}

/* ======================================================================
 * Score update functions
 * ====================================================================== */

static void Update_Debrief_Train_Scores(void) {
	if (!TieScoreTables_LoadTraining("train.hgh", debrief_train_scores))
		memset(debrief_train_scores, 0, sizeof(debrief_train_scores));

	int16_t change = 0, index = 0;
	char pilot_name[TIE_PILOT_NAME_CAPACITY];
	shipext_Get_Pilot_Name(pilot_name, sizeof(pilot_name));

	for (int16_t i = 0; i < TRAIN_SCORE_ENTRY_COUNT && !change; i++) {
		if (debrief_train_scores[i].score < mission.mission_score) {
			change = 1;
			index = i;
		}
	}

	if (change) {
		for (int16_t i = TRAIN_SCORE_ENTRY_COUNT - 1; i > index; i--)
			debrief_train_scores[i] = debrief_train_scores[i - 1];
		snprintf(debrief_train_scores[index].name, sizeof(debrief_train_scores[index].name), "%s",
				 pilot_name);
		debrief_train_scores[index].score = mission.mission_score;
		debrief_train_scores[index].level = mission.train_level;
		TieScoreTables_SaveTraining("train.hgh", debrief_train_scores);
	}
}

static void Update_Debrief_Combat_Scores(void) {
	uint8_t ship_idx = shipext_Get_Combat_Ship();
	char file_name[16];
	int16_t missions;

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

	GameScoreHead* scores = (GameScoreHead*)calloc(missions, sizeof(GameScoreHead));
	if (!scores)
		return;
	int16_t num_scores = 0;
	TieScoreTables_LoadGame(file_name, scores, missions, &num_scores);

	/* Count player kills */
	int16_t kills = 0;
	for (int16_t i = 0; i < (int16_t)NUM_SPEC; i++)
		kills += pstate.player_kills_per_species[i];

	char mission_name[16], pilot_name[TIE_PILOT_NAME_CAPACITY];
	strcpy(mission_name, shipext_Get_Mission_Name());
	shipext_Get_Pilot_Name(pilot_name, sizeof(pilot_name));

	/* Find or create mission slot */
	int16_t index = 0;
	while (index < missions && scores[index].name[0] && strcmp(scores[index].name, mission_name))
		index++;

	if (index < missions && !scores[index].name[0]) {
		snprintf(scores[index].name, sizeof(scores[index].name), "%s", mission_name);
		num_scores++;
	}

	if (index < missions) {
		int16_t change = 0, score_index = 0;
		for (int16_t i = 0; i < GAME_SCORE_ENTRY_COUNT && !change; i++) {
			if (scores[index].scores[i].score < mission.mission_score) {
				change = 1;
				score_index = i;
			}
		}

		if (change) {
			for (int16_t i = GAME_SCORE_ENTRY_COUNT - 1; i > score_index; i--)
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

static void Update_Debrief_Scores(void) {
	int16_t scene = shellext_Get_Cur_Scene();
	if (scene == SCENE_TRAIN_MAP)
		Update_Debrief_Train_Scores();
	else if (scene == SCENE_COMBAT_MAP_B)
		Update_Debrief_Combat_Scores();
}

/* ======================================================================
 * iupdate/iuser/idraw callbacks for MAP buttons
 * ====================================================================== */

static int16_t iupdate_Map(Input* input, Rect* r, Rect* clip_r, int16_t key, uint8_t left, uint8_t right,
						   int16_t x, int16_t y) {
	(void)clip_r;
	if (key)
		return 0;

	uint8_t button = 0;
	if (left)
		button = left;
	if (right)
		button = right;

	PushButton* btn = (PushButton*)input;
	if (button < 2) {
		if (button == 1)
			btn->pressed = 1;
	} else if (button == 2) {
		btn->pressed = lrect_Point_In_Rect(r, r->left + x, r->top + y);
	} else if (button == 3 && btn->pressed) {
		linpattr_Selected_Input(input);
		btn->pressed = 0;
	}

	linpattr_Refresh_Input(input);
	map_text_count = 12;

	switch (input->id) {
		case 0:
			if (talk_mode) {
				map_text = txtMapLastQuestion;
			} else {
				map_text = player_Is_Map_Playing() ? txtMapBriefStop : txtMapBriefRewind;
			}
			break;
		case 1:
			if (talk_mode) {
				map_text = txtMapNextQuestion;
			} else {
				map_text = player_Is_Map_Playing() ? txtMapBriefRewind : txtMapBriefPlay;
			}
			break;
		case 2:
			map_text = talk_mode ? txtMapNextPage : txtMapBriefSkip;
			break;
		case 3:
			/* The "View" button cycles through three labels matching the
			 * upcoming talk mode (next_mode). Showing 'Exit' here was wrong
			 * — that label belongs to the Exit button (case 4/5). */
			if (!next_mode)
				map_text = txtMapViewBriefing;
			else if (next_mode == 1)
				map_text = txtMapViewOfficer;
			else if (next_mode == 2)
				map_text = txtMapViewPriest;
			break;
		case 4:
			map_text =
				(shellext_Get_Cur_Scene() == SCENE_COMBAT_MAP_A) ? txtMapEnterMission : txtDebriefAgain;
			break;
		case 5:
			map_text =
				(shellext_Get_Cur_Scene() == SCENE_COMBAT_MAP_A) ? txtMapExitBriefing : txtMapExitDebriefing;
			break;
	}
	return 1;
}

// FUNCTION: TIE 0x77C4C
static void Set_Voice_Species_Mission(void) {
	uint8_t mission_cursor;

	last_voiced_paragraph = 0;
	if (map_uses_battle_voice) {
		uint8_t battle = pilot_record.cur_battle;
		talk_voice_species = (int16_t)(battle + 1);
		mission_cursor = pilot_record.battle_cursor[battle];
	} else {
		uint8_t ship = pilot_record.cur_combat_ship;
		if (ship >= NUM_SHIPS)
			talk_voice_species = (int16_t)(ship - (NUM_SHIPS - 1));
		else
			talk_voice_species = (int16_t)(-(ship + 1));
		mission_cursor = pilot_record.combat_course_cursor[ship];
	}
	talk_voice_mission = (int16_t)(mission_cursor + 1);
}

static void iuser_Map(Input* input, int32_t time) {
	/* Briefing mode: offset button position during panel animation */
	if (shellext_Get_Cur_Scene() == SCENE_BRIEF_MAP && time < 5) {
		if (time)
			lrect_Offset_Rect(&input->frame, 0, map_panel_y[time] - map_panel_y[time - 1]);
		else
			lrect_Offset_Rect(&input->frame, 0, map_panel_y[0] - map_panel_y[4]);
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

	if (!linpattr_Get_Input_Selected(input))
		return;

	switch (input->id) {
		case 0: /* Stop / Prev Question */
			if (talk_mode) {
				if (num_talk_questions) {
					if (cur_talk_question)
						cur_talk_question--;
					else
						cur_talk_question = num_talk_questions - 1;
					Set_VR_Talk_Paragraph();
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
				linpattr_Refresh_Input(play_input);
			}
			break;

		case 1: /* Play / Next Question */
			if (talk_mode) {
				if (num_talk_questions) {
					if (cur_talk_question >= num_talk_questions - 1)
						cur_talk_question = 0;
					else
						cur_talk_question++;
					Set_VR_Talk_Paragraph();
					if (options_gbl.speech_active)
						talk_paragraph_timer = time + 264;
					talk_voice_question = (int16_t)(cur_talk_question + 1);
					talk_Start_Speech_Stream();
				}
			} else {
				if (player_Is_Map_Playing()) {
					player_Rewind_Page();
					linpattr_Refresh_Input(play_input);
				} else {
					player_Toggle_Map_Play();
					linpattr_Refresh_Input(stop_input);
				}
			}
			break;

		case 2: /* Skip / Next Page */
			if (talk_mode) {
				if (lio_Right_Button_Release()) {
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
				player_Clear_Page_Commands();
				linpattr_Show_Input(map_input);
				linpattr_Hide_Input(talk_input);
				talk_paragraph_timer = 0x7FFFFFFF;
			} else if (talk_mode == 1) {
				linpattr_Hide_Input(map_input);
				linpattr_Show_Input(talk_input);
				talk_voice_officer = 'o';
				talk_voice_question = 1;
				if (map_is_post_mission) {
					talk_voice_question = 0;
					talk_voice_mood = (mission.primary_complete == 1) ? 'd' : 'h';
				}
				Set_VR_Talk_To_Text(0);
				if (options_gbl.speech_active)
					talk_paragraph_timer = time + 264;
			} else if (talk_mode == 2) {
				linpattr_Hide_Input(map_input);
				linpattr_Show_Input(talk_input);
				talk_voice_officer = 'p';
				talk_voice_question = 1;
				if (map_is_post_mission) {
					talk_voice_question = 0;
					talk_voice_mood = (mission.secondary_complete == 1) ? 'd' : 'h';
				}
				Set_VR_Talk_To_Text(1);
				if (options_gbl.speech_active)
					talk_paragraph_timer = time + 264;
			}
			lview_Refresh_View();
			break;

		case 4: /* Enter Mission / Exit Debrief */
			if (shellext_Get_Cur_Scene() >= SCENE_COMBAT_MAP_A &&
				shellext_Get_Cur_Scene() <= SCENE_COMBAT_MAP_B) {
				lerror_Set_Landru_Exit(SCENE_FLIGHT_COMBAT);
				shipext_Update_Pilot();
			} else if (shellext_Get_Cur_Scene() == SCENE_BRIEF_MAP) {
				lerror_Set_Landru_Exit(SCENE_BRIEF);
			}
			soundext_Stop_SFX(sfxText);
			break;

		case 5: /* Exit */
			if (shellext_Get_Cur_Scene() == SCENE_TRAIN_MAP)
				lerror_Set_Landru_Exit(SCENE_TRAIN_B);
			else if (shellext_Get_Cur_Scene() >= SCENE_COMBAT_MAP_A &&
					 shellext_Get_Cur_Scene() <= SCENE_COMBAT_MAP_B)
				lerror_Set_Landru_Exit(SCENE_COMBAT_B);
			else if (shellext_Get_Cur_Scene() == SCENE_BRIEF_MAP)
				lerror_Set_Landru_Exit(SCENE_BRIEF);
			soundext_Stop_SFX(sfxText);
			break;
	}
}

static void idraw_Map(Input* input, Rect* r, Rect* clip_r, int16_t refresh) {
	if (!refresh)
		return;

	PushButton* btn = (PushButton*)input;
	int16_t down = 0;
	int16_t x = cmbticons->x;
	int16_t y = cmbticons->y;
	int16_t id, down_id;

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
		lactor_Set_Actor_State(cmbticons, down_id, 0);
	else
		lactor_Set_Actor_State(cmbticons, id, 0);

	lactanim_Draw_Anim_Actor(cmbticons, r, clip_r, x, y, refresh);

	if (linpattr_Is_Input_Dirty(input))
		ldirty_Dirty_Rect(clip_r);
}

/* ======================================================================
 * idraw_Talk — talk text overlay in MAP mode
 * ====================================================================== */

static void idraw_Talk(Input* input, Rect* r, Rect* clip_r, int16_t refresh) {
	char buf[64], fmt[32], str1[32];
	Rect tr;

	lrect_Copy_Rect(&tr, r);
	player_Stars_To_Back(r->top + 1);

	if (shellext_Get_Cur_Scene() == SCENE_TRAIN_MAP) {
		/* Training stats display */
		lfont_Enable_FontID_Shadow(0);
		int16_t saved_bold = lfont_Get_FontID_Bold_Color(0);
		lfont_Set_FontID_Bold_Color(0, 231);

		tr.top += 40;
		tr.bottom = tr.top + 10;

		uint8_t train_ship = shipext_Get_Train_Ship();
		shipext_Get_Ship_Name(buf, train_ship, 0, 0);
		textext_Copy_Text(fmt, txtMapTrainLevel);
		snprintf(str1, sizeof(str1), fmt, mission.train_level);
		strcat(buf, str1);
		lfont_Print_Centered_Text(buf, &tr, 228, 0);

		lrect_Offset_Rect(&tr, 0, 14);
		lpaint_Horiz_Clipped_Line(tr.left + 48, tr.top - 3, tr.right - tr.left - 96, 231);

		textext_Copy_Text(fmt, txtMapTrainScore);
		snprintf(buf, sizeof(buf), fmt, mission.mission_score);
		lfont_Print_Centered_Text(buf, &tr, 228, 0);
		lrect_Offset_Rect(&tr, 0, 10);

		if (mission.mission_new_rank) {
			textext_Copy_Text(str1, txtTalkRank);
			textext_Copy_Text(fmt, mission.mission_new_rank + 1);
			replace_italic_codes(str1);
			snprintf(buf, sizeof(buf), str1, fmt);
			lfont_Print_Centered_Text(buf, &tr, 228, 0);
			lrect_Offset_Rect(&tr, 0, 10);
		}

		if (train_pilot_medal_status) {
			textext_Copy_Text(buf, txtTalkTrainPatch);
			replace_italic_codes(buf);
			lfont_Print_Centered_Text(buf, &tr, 228, 0);
			lrect_Offset_Rect(&tr, 0, 10);
		}

		textext_Copy_Text(fmt, txtMapTrainPassed);
		snprintf(buf, sizeof(buf), fmt, (uint16_t)mission.train_gates_passed);
		lfont_Print_Centered_Text(buf, &tr, 228, 0);
		lrect_Offset_Rect(&tr, 0, 10);

		textext_Copy_Text(fmt, txtMapTrainRemain);
		snprintf(buf, sizeof(buf), fmt, (uint16_t)mission.train_gates_remaining);
		lfont_Print_Centered_Text(buf, &tr, 228, 0);
		lrect_Offset_Rect(&tr, 0, 10);

		textext_Copy_Text(fmt, txtMapTrainTargets);
		snprintf(buf, sizeof(buf), fmt, (uint16_t)mission.train_targets);
		lfont_Print_Centered_Text(buf, &tr, 228, 0);

		if (mission.training_badge_earned) {
			lrect_Offset_Rect(&tr, 0, 10);
			textext_Copy_Text(buf, txtMapTrainBadge);
			lfont_Print_Centered_Text(buf, &tr, 228, 0);
		}

		lfont_Set_FontID_Bold_Color(0, saved_bold);
		lfont_Disable_FontID_Shadow(0);
	} else {
		/* Combat/briefing talk text display */
		lfont_Enable_FontID_Shadow(0);
		tr.top += 2;
		tr.bottom = tr.top + 10;

		/* Officer/priest title + question indicator */
		textext_Copy_Text(buf, talk_person + txtMapBriefOfficer);
		textext_Copy_Text(fmt, txtMapQuestion);
		if (shellext_Get_Cur_Scene() == SCENE_COMBAT_MAP_B) {
			if (cur_talk_question == num_talk_questions - 1)
				snprintf(str1, sizeof(str1), fmt, 1, num_talk_questions);
			else
				snprintf(str1, sizeof(str1), fmt, cur_talk_question + 2, num_talk_questions);
		} else {
			snprintf(str1, sizeof(str1), fmt, cur_talk_question + 1, num_talk_questions);
		}
		strcat(buf, str1);
		lfont_Print_Centered_Text(buf, &tr, 14, 0);

		lpaint_Horiz_Clipped_Line(tr.left + 8, tr.bottom + 1, tr.right - tr.left - 16, 24);
		tr.top += 14;
		tr.bottom = tr.top + 10;

		/* Current question text */
		Get_VR_Talk_Question(buf, cur_talk_question);
		lfont_Print_Centered_Text(buf, &tr, 15, 0);
		lrect_Offset_Rect(&tr, 0, 10);

		/* Paragraph text */
		int16_t first_line = max_paragraph_size * cur_talk_paragraph;
		tr.top += 2;
		lrect_Inset_Rect(&tr, 46, 0);
		tr.bottom = tr.top + 78;
		lpaint_Horiz_Clipped_Line(r->left + 8, tr.top, r->right - r->left - 16, 24);
		tr.top += 4;
		tr.bottom = tr.top + 10;

		int16_t saved_bold = lfont_Get_FontID_Bold_Color(0);
		lfont_Set_FontID_Bold_Color(0, 231);

		for (int16_t i = first_line; i < first_line + max_paragraph_size; i++) {
			center_line = 0;
			Get_VR_Talk_Paragraph(buf, i);
			if (center_line)
				lfont_Print_Centered_Text(buf, &tr, 228, 0);
			else
				lfont_Print_Clipped_Text(buf, tr.left + 8, tr.top, 0, 228);
			lrect_Offset_Rect(&tr, 0, 10);
		}

		lfont_Set_FontID_Bold_Color(0, saved_bold);

		/* Page indicator */
		char page_str[16], of_str[16];
		strcpy(page_str, textext_Get_Text(txtMapPage));
		strcpy(of_str, textext_Get_Text(txtMapOf));
		snprintf(buf, sizeof(buf), "%s %d %s %d", page_str, cur_talk_paragraph + 1, of_str,
				 num_talk_paragraphs);
		lfont_Print_Clipped_Text(buf, r->right - 80, r->bottom - 10, 0, 24);

		lfont_Disable_FontID_Shadow(0);
	}

	if (linpattr_Is_Input_Dirty(input))
		ldirty_Dirty_Rect(clip_r);
}

/* ======================================================================
 * map_Map — main entry point
 * ====================================================================== */

typedef enum {
	MAP_PHASE_BEGIN = 0,
	MAP_PHASE_CLEANUP = 1,
} MapPhase;

typedef struct MapTask {
	SceneHeadStruct* scene_head;
	MapPhase phase;
} MapTask;

static LandruTaskStepResult map_task_step(void* self) {
	MapTask* mt = (MapTask*)self;
	SceneHeadStruct* scene_head = mt->scene_head;

	if (mt->phase != MAP_PHASE_BEGIN)
		goto cleanup;

	{
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
			lio_Set_Mouse_Position(276, 180);
		} else if (scene == SCENE_COMBAT_MAP_A) {
			if (!shipext_Is_Combat_Ship_Tour()) {
				combat_pilot_medal_init = 0;
				for (i = 0; i < 8; i++) {
					if (pilot_record.combat_complete[shipext_Get_Combat_Ship()][i])
						combat_pilot_medal_init++;
				}
			}
			talk_voice_mood = 'b';
			lio_Set_Mouse_Position(240, 180);
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
				lio_Set_Mouse_Position(112, 180);
			} else {
				talk_voice_mood = 'h';
				lio_Set_Mouse_Position(250, 180);
			}
		} else if (scene == SCENE_BRIEF_MAP) {
			lio_Set_Mouse_Position(240, 180);
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
			TieSnapshotBuilder_SetActiveFilm("MAP", "brief");
		}

		Update_Debrief_Scores();

		/* Load resources */
		file = shellext_Open_Empire_Resource(map_str[MAP_LFD]);
		pfile = shellext_Open_Empire_Resource(map_str[MAP_PLAYER_LFD]);
		lrect_Set_Rect(&r, 0, 0, 320, 200);

		if (scene == SCENE_BRIEF_MAP) {
			the_actor = lactdelt_Res_Delta_Actor(map_str[MAP_BRIEF_BG], &r, 0, 0, 50);
			/* Classic FB optimization: brfmap1 paints once at scene start
			 * and the diff bitmap restores its pixels on dirty-rect refresh,
			 * so it doesn't need to re-emit. The HD cutscene RT has no
			 * diff bitmap — leaving brfmap1 Non_Refreshable means moving
			 * actors (brfpanel slide) leave trails on the persistent RT
			 * because nothing repaints the underlying background. Re-
			 * enabling refresh costs one extra delta blit per classic-FB
			 * tick — negligible — and lets HD render correctly. */
			/* lactor_Non_Refreshable_Actor(the_actor); */
			the_actor = lactdelt_Res_Delta_Actor(map_str[MAP_BRIEF_BG2], &r, 0, 0, 50);
			the_actor = lactdelt_Res_Delta_Actor(map_str[MAP_BRIEF_PANEL], &r, 0, 0, 20);
			lactor_Set_Actor_User_Function(the_actor, user_Map_Panel);
			the_actor->id = 0;
			the_actor = lactanim_Res_Anim_Actor(map_str[MAP_PANEL_HANDLE], &r, 0, 0, 20);
			lactor_Set_Actor_User_Function(the_actor, user_Map_Panel);
			the_actor->id = 1;
			cmbticons = lactanim_Res_Anim_Actor(map_str[MAP_BRIEF_BUTTONS], &r, 0, 12, 0);
		} else if (scene == SCENE_TRAIN_MAP) {
			the_actor = lactdelt_Res_Delta_Actor(map_str[MAP_TRAIN_OVERLAY], &r, 0, 0, 0);
			lactor_Set_Actor_Draw_Function(the_actor, draw_Map_Text);
			the_actor->id = 1;
			the_actor = lactdelt_Res_Delta_Actor(map_str[MAP_TRAIN_TEXT], &r, 0, 0, 0);
			lactor_Non_Refreshable_Actor(the_actor);
			cmbticons = lactanim_Res_Anim_Actor(map_str[MAP_BUTTON_ICONS], &r, 0, 0, 0);
			title_actor = lactdelt_Res_Delta_Actor(map_str[MAP_TRAIN_TITLE], &r, 0, 0, 0);
			lactor_Set_Actor_Time(title_actor, 0, 0);
		} else {
			the_actor = lactdelt_Res_Delta_Actor(map_str[MAP_COMBAT_TEXT], &r, 0, 0, 0);
			lactor_Set_Actor_Draw_Function(the_actor, draw_Map_Text);
			the_actor->id = 1;
			the_actor = lactdelt_Res_Delta_Actor(map_str[MAP_COMBAT_BG], &r, 0, 0, 0);
			lactor_Non_Refreshable_Actor(the_actor);
			cmbticons = lactanim_Res_Anim_Actor(map_str[MAP_BUTTON_ICONS], &r, 0, 0, 0);
			title_actor = lactdelt_Res_Delta_Actor(map_str[MAP_COMBAT_TITLE], &r, 0, 0, 0);
			lactor_Set_Actor_Time(title_actor, 0, 0);
		}

		lactor_Set_Actor_Time(cmbticons, -1, -1);
		parent = linput_Alloc_Input(NULL, &r, 0, 0);
		index = (scene == SCENE_BRIEF_MAP) ? 6 : 0;
		max_paragraph_size = 10;

		/* Create buttons (not for training scene) */
		if (scene != SCENE_TRAIN_MAP) {
			/* Stop */
			the_input = (Input*)lbtnpush_Alloc_Button(parent, (Rect*)&map_rect[index], 0, iuser_Map, NULL, 0);
			linpattr_Set_Input_Update_Function(the_input, iupdate_Map);
			linpattr_Set_Input_Draw_Function(the_input, idraw_Map);
			linpattr_Refreshable_Input(the_input);
			the_input->mouseUsage = allInput;
			stop_input = the_input;

			/* Play */
			the_input =
				(Input*)lbtnpush_Alloc_Button(parent, (Rect*)&map_rect[index + 1], 0, iuser_Map, NULL, 1);
			linpattr_Set_Input_Update_Function(the_input, iupdate_Map);
			linpattr_Set_Input_Draw_Function(the_input, idraw_Map);
			linpattr_Refreshable_Input(the_input);
			the_input->mouseUsage = allInput;
			play_input = the_input;

			/* Skip */
			the_input =
				(Input*)lbtnpush_Alloc_Button(parent, (Rect*)&map_rect[index + 2], 0, iuser_Map, NULL, 2);
			linpattr_Set_Input_Update_Function(the_input, iupdate_Map);
			linpattr_Set_Input_Draw_Function(the_input, idraw_Map);
			linpattr_Refreshable_Input(the_input);
			the_input->mouseUsage = allInput;
		}

		/* Exit (always present) */
		the_input = (Input*)lbtnpush_Alloc_Button(parent, (Rect*)&map_rect[index + 3], 0, iuser_Map, NULL, 5);
		linpattr_Set_Input_Update_Function(the_input, iupdate_Map);
		linpattr_Set_Input_Draw_Function(the_input, idraw_Map);
		linpattr_Refreshable_Input(the_input);
		the_input->mouseUsage = allInput;

		/* Combat: Enter Mission + ViewOfficer/ViewPriest buttons */
		if (scene == SCENE_COMBAT_MAP_A || scene == SCENE_COMBAT_MAP_B) {
			if (scene == SCENE_COMBAT_MAP_A || !shipext_Get_Mission_Officer()) {
				the_input =
					(Input*)lbtnpush_Alloc_Button(parent, (Rect*)&map_rect[index + 4], 0, iuser_Map, NULL, 3);
				linpattr_Set_Input_Update_Function(the_input, iupdate_Map);
				linpattr_Set_Input_Draw_Function(the_input, idraw_Map);
				linpattr_Refreshable_Input(the_input);
				the_input->mouseUsage = allInput;
			}
			the_input =
				(Input*)lbtnpush_Alloc_Button(parent, (Rect*)&map_rect[index + 5], 0, iuser_Map, NULL, 4);
			linpattr_Set_Input_Update_Function(the_input, iupdate_Map);
			linpattr_Set_Input_Draw_Function(the_input, idraw_Map);
			linpattr_Refreshable_Input(the_input);
			the_input->mouseUsage = allInput;
		}

		/* Map + talk display areas */
		if (scene == SCENE_BRIEF_MAP)
			lrect_Set_Rect(&r, 13, 18, 307, 167);
		else
			lrect_Set_Rect(&r, 13, 6, 307, 155);

		map_input = linput_Alloc_Input(parent, &r, 0, 0);
		talk_input = linput_Alloc_Input(parent, &r, 0, 0);
		linpattr_Set_Input_Draw_Function(talk_input, idraw_Talk);

		/* Palette setup */
		if (scene == SCENE_COMBAT_MAP_A) {
			lpal_Set_Dest_Pal_Color(1, 255, 6, 6, 6);
			lpal_Dest_To_Screen_Palette(1, 1, 255);
		}
		lpal_Set_Dest_Palette(scene_head->def_palette);
		the_palette = lpal_Res_Palette("range");
		lpal_Set_Dest_Palette(the_palette);
		the_palette = lpal_Res_Palette((scene == SCENE_COMBAT_MAP_A) ? map_str[MAP_COMBAT_PAL]
																	 : map_str[MAP_BRIEF_PAL]);
		lpal_Set_Dest_Palette(the_palette);

		if (scene == SCENE_COMBAT_MAP_A)
			lfade_Start_Full_Fade(FADE_WIPE_SNAP_ON, FADE_COLOR_PAL_TO_PAL, 1, 0, 0);
		else
			lfade_Start_Full_Fade(FADE_WIPE_SNAP_ON, FADE_COLOR_TWO_PHASE, 1, 0, 1);

		lres_Close_Resource(pfile);
		lres_Close_Resource(file);

		/* Init talk mode */
		if (scene == SCENE_COMBAT_MAP_B || scene == SCENE_TRAIN_MAP) {
			linpattr_Hide_Input(map_input);
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
			linpattr_Hide_Input(talk_input);
			talk_mode = 0;
			last_voiced_paragraph = 0;
			talk_voice_officer = 'i';
			next_mode = (shipext_Get_Mission_Officer() == 2) ? 2 : 1;
		}

		player_Init_Brief_Display(map_input, NULL);
		talk_brief = player_Fetch_Brief();
		talk_fgroup = player_Fetch_FGroup();
		Set_VR_Talk_To_Text(talk_mode == 2 ? 1 : 0);

		/* Push the modal view task */
		lview_Set_View_Update_Function(end_View);
		lviewadd_Clear_View();
		lview_Disable_All_View_Erase();
		Set_Voice_Species_Mission();
		talk_Alloc_Speech_Sound();
		talk_paragraph_timer = 0x7FFFFFFF;
		lviewadd_Push_Handle_View_Task();

		mt->phase = MAP_PHASE_CLEANUP;
		return LANDRU_TASK_STEP_CONTINUE;
	}

cleanup:
	/* CLEANUP — view popped */
	talk_Free_Speech_Sound();
	player_Free_Brief_Display();
	lview_Enable_All_View_Erase();
	lview_Clear_View_Update_Function();

	if (lcursor_Is_Cursor_Visible())
		lcursor_Hide_Cursor();

	return LANDRU_TASK_STEP_DONE;
}

static const LandruTaskVtable map_task_vt = {
	.step = map_task_step,
};

void map_Push_Map_Task(SceneHeadStruct* scene_head) {
	MapTask* t = (MapTask*)landru_task_push(&map_task_vt);
	if (!t)
		return;
	t->scene_head = scene_head;
	t->phase = MAP_PHASE_BEGIN;
}
