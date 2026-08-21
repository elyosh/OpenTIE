#include "tie_runtime/audio/imuse_session.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tie/mission.h"
#include "tie/player.h"
#include "tie/rand.h"
#include "tie/shade.h"
#include "tie/shellext.h"
#include "tie/shipext.h"
#include "tie/talk.h"
#include "tie/textext.h"
#include "tie/tie.h"
#include "tie_runtime/audio/config.h"
#include "tie_runtime/diagnostics/diagnostics.h"
#include "tie_runtime/display/classic_display.h"
#include "tie_runtime/display/classic_framebuffer.h"
#include "tie_runtime/flight_assets/model_types.h"
#include "tie_runtime/input/input.h"
#include "tie_runtime/runtime/exports.h"
#include "tie_runtime/runtime/profile.h"
#include "tie_runtime/snapshot/snapshot.h"
#include "tie_runtime/snapshot/snapshot_internal.h"
#include "tie_runtime/storage/storage.h"
#include <landru/task.h>

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
#include "landru/paint.h"
#include "landru/pal.h"
#include "landru/rect.h"
#include "landru/res.h"
#include "landru/sound.h"
#include "landru/view.h"
#include "landru/viewadd.h"

#include <imuse/lolevel.h>

/* ======================================================================
 * Static data — resource name table (16 entries × 14 chars)
 * ====================================================================== */

static const char talk_str[16][14] = { "talk.lfd", "brf_off", "brf_ss", "dbrf_off", "dbrf_ss", "eyes",
									   "ssface",   "offbak",  "ssbak",  "doffbak",  "dssbak",  "offtxt",
									   "sstxt",    "dofftxt", "dsstxt", "mouth" };

/* Officer face animation state tables (indexed by mood 0-4) */
static const int16_t officer_mood_eye[5] = { 0, 5, 9, 6, 9 };
static const int16_t officer_mood_blink[5] = { 4, 4, 4, 7, 7 };
static const int16_t officer_mood_mouth[5] = { 0, 2, 1, 3, 0 };
static const int16_t priest_mood_eye[5] = { 0, 5, 5, 6, 5 };
static const int16_t priest_mood_blink[6] = { 4, 5, 5, 7, 5, 0 };

/* ======================================================================
 * Static BSS globals
 * ====================================================================== */

static int16_t talk_win_id[6];
static EBriefStruct* talk_brief;
static Actor* eye_actor;
static Input* parent;
static Actor* mouth_actor;
static Input* talk_input; /* renamed from 'talk' to avoid keyword conflict */
static Film* talk_film;
static Input* answer;
static EFArrayStruct* talk_fgroup;
static int16_t talk_win_status[5];
// GLOBAL: TIE 0xF5744
static int16_t max_paragraph_size;
static int16_t cur_talk_question;
static int16_t num_talk_paragraphs;
static int16_t center_line;
// GLOBAL: TIE 0xF5752
static int16_t cur_talk_paragraph;
static int16_t talk_mode;
static int16_t num_talk_questions;
static int16_t active_talk_question;
static int16_t officer_mood_val[5]; /* officer_mood — 5 int16 mood state */

/* Auto-advance timer for paragraph display. Retail bumps this by 264 per
 * paragraph step; auto-advance triggers when iuser time arg exceeds it.
 * Stays at INT32_MAX (inert) when speech is disabled or when the current
 * question is the final debrief paragraph. Shared with map.c. */
// GLOBAL: TIE 0xCE586
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
// GLOBAL: TIE 0xF5704
Sound* talk_speech_sound = NULL;
int16_t talk_voice_species = 0; /* >0: numeric "Nm…" filename */
								/* <0: char-encoded "[fibagdm]m…" */
int16_t talk_voice_mission = 0;
int16_t talk_voice_question = 0;
uint8_t talk_voice_officer = 0; /* 'o', 'p', 'i' */
uint8_t talk_voice_mood = 0;    /* 'b', 'd', 'h', 'o', etc. */

#define TALK_SPEECH_BUF_SIZE 2048000 /* matches retail allocation */

// GLOBAL: TIE 0xF5708
static int32_t talk_speech_pos = 0;
// GLOBAL: TIE 0xF575C
static uint8_t talk_speech_streaming = 0;

/* ======================================================================
 * Helper: replace ASCII '1'/'2' in string with control codes 0x01/0x02
 * (italic on/off). Used by all debrief text formatting.
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
 * Mood get/set
 * ====================================================================== */

void talk_Set_Officer_Mood(int16_t mood) { officer_mood_val[0] = mood; }

int16_t talk_Get_Officer_Mood(void) { return officer_mood_val[0]; }

/* ======================================================================
 * Check_Talk_Questions — scan talk data for visibility conditions
 * ====================================================================== */

static void Check_Talk_Questions(void) {
	int16_t slot_idx = 0;

	do {
		uint16_t handle = (uint16_t)(uintptr_t)talk_brief->talk_data[5 * talk_mode + slot_idx];
		int16_t status = 0;

		if (handle) {
			char* data = (char*)talk_brief->talk_data[5 * talk_mode + slot_idx];
			int16_t pos = 0;
			if (data[0]) {
				status = 1;
				while (data[pos] && data[pos] != '\n') {
					if (data[pos] == 4 || data[pos] == 5) {
						if (data[pos] == 4)
							status = (int8_t)data[pos + 1] + 1;
						else
							status = (int8_t)data[pos + 1] + 3;
						pos++;
					}
					pos++;
				}
			}
		}
		talk_win_status[slot_idx] = status;
		slot_idx++;
	} while (slot_idx < 5);
}

/* ======================================================================
 * Count_Debrief_* — page count helpers
 * ====================================================================== */

static int16_t Count_Debrief_Header(void) { return 1; }

static int16_t Count_Debrief_Kills(void) {
	int16_t count = 0;
	for (int16_t craft = 0; craft < 69; craft++) {
		int16_t has_kill = 0;
		for (int16_t side = 0; side < 6; side++) {
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

static int16_t Count_Debrief_Losses(void) {
	int16_t count = 0;
	for (int16_t craft = 0; craft < 69; craft++) {
		int16_t has_loss = 0;
		for (int16_t side = 0; side < 6; side++) {
			if (!player_Is_Side_Enemy(side) && mission.kills_losses[side][craft])
				has_loss = 1;
		}
		if (has_loss)
			count++;
	}
	return (max_paragraph_size + count - 3) / (max_paragraph_size - 2);
}

static int16_t Count_Debrief_Captures(void) {
	int16_t count = 0;
	for (int16_t craft = 0; craft < 69; craft++) {
		if (mission.captures_by_type[craft])
			count++;
	}
	return (max_paragraph_size + count - 3) / (max_paragraph_size - 2);
}

static int16_t Count_Debrief_Pages(void) {
	int16_t total = 2; /* header + goals pages */
	total += Count_Debrief_Kills();
	total += Count_Debrief_Losses();
	total += Count_Debrief_Captures();
	return total;
}

/* ======================================================================
 * Debrief entry renderers — Get_Debrief_Kills/Losses/Captures
 * ====================================================================== */

static void Get_Debrief_Kills(char* string, int16_t craft_idx) {
	uint16_t count = 0;
	char name[40];
	char buf[80];

	if (craft_idx >= 69) {
		count = pstate.player_total_kills;
	} else {
		for (int16_t side = 0; side < 6; side++) {
			if (player_Is_Side_Enemy(side))
				count += mission.kills_losses[side][craft_idx];
		}
	}
	if (count) {
		if (craft_idx >= 69) {
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

static void Get_Debrief_Losses(char* string, int16_t craft_idx) {
	uint16_t count = 0;
	char name[40];
	char buf[80];

	if (craft_idx < 69) {
		for (int16_t side = 0; side < 6; side++) {
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

static void Get_Debrief_Captures(char* string, int16_t craft_idx) {
	uint16_t count = mission.captures_by_type[craft_idx];
	if (count) {
		char name[40];
		char buf[80];
		textext_Get_Ship_Text(name, craft_idx);
		snprintf(buf, sizeof(buf), "  %s: %d", name, count);
		strcpy(string, buf);
	}
}

/* ======================================================================
 * Debrief title renderers — Get_Debrief_Kill/Loss/Capture_Title
 * ====================================================================== */

static void Get_Debrief_Kill_Title(char* string) {
	uint16_t total_kills = 0, player_total = 0;
	char fmt[40], buf[80];

	for (uint16_t craft = 0; craft < 69; craft++) {
		for (uint16_t side = 0; side < 6; side++) {
			if (player_Is_Side_Enemy(side))
				total_kills += mission.kills_losses[side][craft];
		}
		player_total += pstate.player_kills_per_species[craft];
	}
	textext_Copy_Text(fmt, txtTalkDestroyed);
	replace_italic_codes(fmt);
	snprintf(buf, sizeof(buf), fmt, total_kills, player_total);
	strcpy(string, buf);
	center_line = 1;
}

static void Get_Debrief_Loss_Title(char* string) {
	uint16_t total = 0;
	char fmt[40], buf[80];

	for (uint16_t craft = 0; craft < 69; craft++) {
		for (uint16_t side = 0; side < 6; side++) {
			if (!player_Is_Side_Enemy(side))
				total += mission.kills_losses[side][craft];
		}
	}
	textext_Copy_Text(fmt, txtTalkLost);
	replace_italic_codes(fmt);
	snprintf(buf, sizeof(buf), fmt, total);
	strcpy(string, buf);
	center_line = 1;
}

static void Get_Debrief_Capture_Title(char* string) {
	uint16_t total = 0;
	char fmt[40], buf[80];

	for (uint16_t craft = 0; craft < 69; craft++)
		total += mission.captures_by_type[craft];

	textext_Copy_Text(fmt, txtTalkCaptured);
	replace_italic_codes(fmt);
	snprintf(buf, sizeof(buf), fmt, total);
	strcpy(string, buf);
	center_line = 1;
}

/* ======================================================================
 * Debrief section Find_ renderers (paginated iteration)
 * ====================================================================== */

static void Find_Debrief_Kills(char* string, int16_t page, int16_t line) {
	int16_t line_in_page = line % max_paragraph_size;

	if (line_in_page == 0) {
		Get_Debrief_Kill_Title(string);
	} else if (line_in_page == 1) {
		textext_Copy_Text(string, txtTalkDash);
		center_line = 1;
	} else {
		int16_t skip = line - 2 * (page + 1);
		for (int16_t craft = 0; craft <= 69; craft++) {
			Get_Debrief_Kills(string, craft);
			if (*string) {
				if (!skip)
					return;
				skip--;
				*string = '\0';
			}
		}
	}
}

static void Find_Debrief_Losses(char* string, int16_t page, int16_t line) {
	int16_t line_in_page = line % max_paragraph_size;

	if (line_in_page == 0) {
		Get_Debrief_Loss_Title(string);
	} else if (line_in_page == 1) {
		textext_Copy_Text(string, txtTalkDash);
		center_line = 1;
	} else {
		int16_t skip = line - 2 * (page + 1);
		for (int16_t craft = 0; craft < 69; craft++) {
			Get_Debrief_Losses(string, craft);
			if (*string) {
				if (!skip)
					return;
				skip--;
				*string = '\0';
			}
		}
	}
}

static void Find_Debrief_Captures(char* string, int16_t page, int16_t line) {
	int16_t line_in_page = line % max_paragraph_size;

	if (line_in_page == 0) {
		Get_Debrief_Capture_Title(string);
	} else if (line_in_page == 1) {
		textext_Copy_Text(string, txtTalkDash);
		center_line = 1;
	} else {
		int16_t skip = line - 2 * (page + 1);
		for (int16_t craft = 0; craft < 69; craft++) {
			Get_Debrief_Captures(string, craft);
			if (*string) {
				if (!skip)
					return;
				skip--;
				*string = '\0';
			}
		}
	}
}

/* ======================================================================
 * Find/Get_Debrief_Header — mission header info
 * ====================================================================== */

static void render_header_line(char* string, int16_t line_idx) {
	char buf[80], fmt[40], rank_name[40];
	int16_t cur_battle = pilot_record.cur_battle;

	switch (line_idx) {
		case 0:
			shipext_Get_Battle_Ship_Name(buf);
			strcpy(string, buf);
			textext_Cat_Text(string, mission.difficulty + txtTalkEasy);
			center_line = 1;
			return;
		case 1: {
			textext_Copy_Text(fmt, txtTalkBattle);
			replace_italic_codes(fmt);
			int16_t mission_num;
			if (shipext_Is_Mission_Success())
				mission_num = pilot_record.battle_cursor[cur_battle];
			else
				mission_num = pilot_record.battle_cursor[cur_battle] + 1;
			snprintf(buf, sizeof(buf), fmt, cur_battle + 1, mission_num);
			strcpy(string, buf);
			textext_Copy_Text(fmt, txtTalkScore);
			replace_italic_codes(fmt);
			snprintf(buf, sizeof(buf), fmt, mission.mission_score);
			strcat(string, buf);
			center_line = 1;
			return;
		}
		case 2:
		case 6:
			textext_Copy_Text(string, txtTalkDash);
			center_line = 1;
			return;
		case 3:
			if (shipext_Is_Mission_Success())
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
			if (!shipext_Get_TOD_Medal())
				return;
			textext_Copy_Text(buf, txtTalkMedal);
			replace_italic_codes(buf);
			strcpy(string, buf);
			return;
		case 7: {
			uint16_t pct =
				pstate.player_laser_hit ? 100 * pstate.player_laser_hit / pstate.player_laser_fired : 0;
			textext_Copy_Text(fmt, txtCompInfoLaser);
			snprintf(buf, sizeof(buf), fmt, pstate.player_laser_hit, pstate.player_laser_fired, pct);
			strcpy(string, buf);
			return;
		}
		case 8: {
			uint16_t pct =
				pstate.player_missile_hit ? 100 * pstate.player_missile_hit / pstate.player_missile_fired : 0;
			textext_Copy_Text(fmt, txtCompInfoIon);
			snprintf(buf, sizeof(buf), fmt, pstate.player_missile_hit, pstate.player_missile_fired, pct);
			strcpy(string, buf);
			return;
		}
		case 9: {
			if (pstate.player_warhead_hit > pstate.player_warhead_fired)
				pstate.player_warhead_fired = pstate.player_warhead_hit;
			uint16_t pct =
				pstate.player_warhead_hit ? 100 * pstate.player_warhead_hit / pstate.player_warhead_fired : 0;
			textext_Copy_Text(fmt, txtCompInfoRocket);
			snprintf(buf, sizeof(buf), fmt, pstate.player_warhead_hit, pstate.player_warhead_fired, pct);
			strcpy(string, buf);
			return;
		}
	}
}

static void Find_Debrief_Header(char* string, int16_t line, int16_t max_size) {
	int16_t skip = line;
	for (int16_t i = 0; i < max_paragraph_size; i++) {
		render_header_line(string, i);
		if (*string) {
			if (!skip)
				return;
			skip--;
			*string = '\0';
		}
	}
}

static void Get_Debrief_Header(char* string, int16_t line) { render_header_line(string, line); }

/* ======================================================================
 * Find/Get_Debrief_Goals — mission goal completion
 * ====================================================================== */

static void render_goals_line(char* string, int16_t line_idx) {
	char buf[80], fmt[40], count_str[40];
	int16_t cur_battle = pilot_record.cur_battle;
	int16_t num_fgs = (talk_fgroup)->num_fgs;

	switch (line_idx) {
		case 0:
			shipext_Get_Battle_Ship_Name(buf);
			strcpy(string, buf);
			textext_Cat_Text(string, mission.difficulty + txtTalkEasy);
			break;
		case 1: {
			textext_Copy_Text(fmt, txtTalkBattle);
			replace_italic_codes(fmt);
			int16_t mn;
			if (shipext_Is_Mission_Success())
				mn = pilot_record.battle_cursor[cur_battle];
			else
				mn = pilot_record.battle_cursor[cur_battle] + 1;
			snprintf(buf, sizeof(buf), fmt, cur_battle + 1, mn);
			strcpy(string, buf);
			textext_Copy_Text(fmt, txtTalkScore);
			replace_italic_codes(fmt);
			snprintf(buf, sizeof(buf), fmt, mission.mission_score);
			strcat(string, buf);
			break;
		}
		case 2:
			textext_Copy_Text(string, txtTalkDash);
			break;
		case 3: {
			int16_t done = 0, fail = 0;
			for (int16_t fg = 0; fg < num_fgs; fg++) {
				if (mission.primary_fg[fg]) {
					if (mission.primary_fg[fg] == 1)
						done++;
					else
						fail++;
				}
			}
			if (mission.primary_global) {
				if (mission.primary_global == 1)
					done++;
				else
					fail++;
			}
			if (!done && !fail) {
				*string = '\0';
				break;
			}
			if (mission.primary_complete == 1) {
				textext_Copy_Text(string, txtTalkAllPri);
			} else {
				if (done) {
					textext_Copy_Text(fmt, txtTalkOf);
					snprintf(count_str, sizeof(count_str), fmt, done, done + fail);
				} else {
					textext_Copy_Text(count_str, txtTalkNo);
				}
				textext_Copy_Text(fmt, txtTalkSomePri);
				snprintf(buf, sizeof(buf), fmt, count_str);
				strcpy(string, buf);
			}
			break;
		}
		case 4: {
			int16_t done = 0, fail = 0;
			for (int16_t fg = 0; fg < num_fgs; fg++) {
				if (mission.secondary_fg[fg]) {
					if (mission.secondary_fg[fg] == 1)
						done++;
					else
						fail++;
				}
			}
			if (mission.secondary_global) {
				if (mission.secondary_global == 1)
					done++;
				else
					fail++;
			}
			if (!done && !fail) {
				*string = '\0';
				break;
			}
			if (mission.secondary_complete == 1) {
				textext_Copy_Text(string, txtTalkAllSec);
			} else {
				if (done) {
					textext_Copy_Text(fmt, txtTalkOf);
					snprintf(count_str, sizeof(count_str), fmt, done, done + fail);
				} else {
					textext_Copy_Text(count_str, txtTalkNo);
				}
				textext_Copy_Text(fmt, txtTalkSomeSec);
				snprintf(buf, sizeof(buf), fmt, count_str);
				strcpy(string, buf);
			}
			break;
		}
		case 5: {
			int16_t done = 0, fail = 0;
			for (int16_t fg = 0; fg < num_fgs; fg++) {
				if (mission.bonus_fg[fg] && (talk_fgroup)->fg[fg].bonus_points >= 0) {
					if (mission.bonus_fg[fg] == 1)
						done++;
					else
						fail++;
				}
			}
			if (mission.bonus_global) {
				if (mission.bonus_global == 1)
					done++;
				else
					fail++;
			}
			if (!done && !fail) {
				*string = '\0';
				break;
			}
			if (mission.bonus_complete == 1) {
				textext_Copy_Text(string, txtTalkAllBonus);
			} else {
				if (done) {
					textext_Copy_Text(fmt, txtTalkOf);
					snprintf(count_str, sizeof(count_str), fmt, done, done + fail);
				} else {
					textext_Copy_Text(count_str, txtTalkNo);
				}
				textext_Copy_Text(fmt, txtTalkSomeBonus);
				snprintf(buf, sizeof(buf), fmt, count_str);
				strcpy(string, buf);
			}
			break;
		}
		default:
			break;
	}
	center_line = 1;
}

static void Find_Debrief_Goals(char* string, int16_t line, int16_t max_size) {
	int16_t skip = line;
	for (int16_t i = 0; i < max_paragraph_size; i++) {
		render_goals_line(string, i);
		if (*string) {
			if (!skip)
				return;
			skip--;
			*string = '\0';
		}
	}
}

static void Get_Debrief_Goals(char* string, int16_t line) { render_goals_line(string, line); }

/* ======================================================================
 * Get_Debrief_Line — dispatch a single debrief line to the right section
 * ====================================================================== */

static void Get_Debrief_Line(char* string, int16_t line, int16_t max_size) {
	int16_t abs_line = line;
	int16_t section_page = line / max_paragraph_size;

	*string = '\0';
	center_line = 0;

	for (int16_t section = 0; section < 5 && section_page >= 0; section++) {
		int16_t section_pages;
		switch (section) {
			case 0:
				section_pages = Count_Debrief_Header();
				if (section_page < section_pages)
					Find_Debrief_Header(string, abs_line, max_size);
				break;
			case 1:
				section_pages = Count_Debrief_Header();
				if (section_page < section_pages)
					Find_Debrief_Goals(string, abs_line, max_size);
				break;
			case 2:
				section_pages = Count_Debrief_Kills();
				if (section_page < section_pages) {
					talk_Set_Officer_Mood(1);
					Find_Debrief_Kills(string, section_page, abs_line);
				}
				break;
			case 3:
				section_pages = Count_Debrief_Losses();
				if (section_page < section_pages) {
					talk_Set_Officer_Mood(3);
					Find_Debrief_Losses(string, section_page, abs_line);
				}
				break;
			case 4:
				section_pages = Count_Debrief_Captures();
				if (section_page < section_pages) {
					talk_Set_Officer_Mood(1);
					Find_Debrief_Captures(string, section_page, abs_line);
				}
				break;
		}
		abs_line -= section_pages * max_paragraph_size;
		section_page -= section_pages;
	}
}

/* ======================================================================
 * user_Talk_Eyes — actor callback for face animation
 * ====================================================================== */

static void user_Talk_Eyes(Actor* actor, int32_t time) {
	int16_t mood = officer_mood_val[0];

	if (actor->id == 0) {
		/* Officer face: separate eye + mouth actors */
		if (!lactor_Is_Actor_Visible(actor))
			return;
		int16_t blink = officer_mood_blink[mood];
		int16_t mouth_st = officer_mood_mouth[mood];

		if (time && actor->var1 > 0) {
			int16_t eye = officer_mood_eye[mood];
			actor->var1--;
			lactor_Set_Actor_State(actor, eye, 0);
		} else {
			if (actor->var1 == -2)
				actor->var1 = rand_rand() & 0x5F;
			else
				actor->var1--;
			lactor_Set_Actor_State(actor, blink, 0);
		}
		lactor_Set_Actor_State(mouth_actor, mouth_st, 0);
	} else if (actor->id == 1) {
		/* Priest face: single actor with eye states */
		if (!lactor_Is_Actor_Visible(actor))
			return;
		int16_t eye = priest_mood_eye[mood];
		int16_t blink = priest_mood_blink[mood];

		if (time && actor->var1 > 0) {
			actor->var1--;
			lactor_Set_Actor_State(actor, eye, 0);
		} else {
			if (actor->var1 == -3)
				actor->var1 = rand_rand() & 0x5F;
			else
				actor->var1--;
			lactor_Set_Actor_State(actor, blink, 0);
		}
	}
}

/* ======================================================================
 * talk_Get_Talk_Question — extract question display text
 * ====================================================================== */

void talk_Get_Talk_Question(char* out, int16_t id) {
	if (id >= 0 && id < num_talk_questions) {
		int16_t question_id = talk_win_id[id];
		if (question_id == 5) {
			strcpy(out, textext_Get_Text(txtTalkDebrief));
			return;
		}
		char* data = (char*)talk_brief->talk_data[5 * talk_mode + question_id];
		if (data) {
			int16_t buf_len = 0;
			for (int16_t i = 0; data[i] && data[i] != '\n'; i++) {
				if (data[i] == 4 || data[i] == 5)
					i++; /* skip control code + param */
				else
					out[buf_len++] = data[i];
			}
			out[buf_len] = '\0';
		} else {
			*out = '\0';
		}
	} else if (talk_win_id[id] == 6) {
		strcpy(out, textext_Get_Text(txtTalkExit));
	}
}

/* ======================================================================
 * talk_Get_Talk_Paragraph — extract paragraph line text
 * ====================================================================== */

// FUNCTION: TIE 0x69868
void talk_Get_Talk_Paragraph(char* out, int16_t line) {
	*out = '\0';
	int16_t italic = 0;
	officer_mood_val[0] = 0;

	if (cur_talk_question < 0 || cur_talk_question >= num_talk_questions)
		return;

	int16_t question_id = talk_win_id[cur_talk_question];
	if (question_id == 5) {
		/* Debrief mode: dispatch to section renderers */
		*out = '\0';
		center_line = 0;
		int16_t rem_line = line;
		int16_t section_page = line / max_paragraph_size;

		for (int16_t section = 0; section < 5 && section_page >= 0; section++) {
			int16_t section_pages;
			switch (section) {
				case 0:
					section_pages = Count_Debrief_Header();
					if (section_page < section_pages)
						Find_Debrief_Header(out, rem_line, max_paragraph_size);
					break;
				case 1:
					section_pages = Count_Debrief_Header();
					if (section_page < section_pages)
						Find_Debrief_Goals(out, rem_line, max_paragraph_size);
					break;
				case 2:
					section_pages = Count_Debrief_Kills();
					if (section_page < section_pages) {
						talk_Set_Officer_Mood(1);
						Find_Debrief_Kills(out, section_page, rem_line);
					}
					break;
				case 3:
					section_pages = Count_Debrief_Losses();
					if (section_page < section_pages) {
						talk_Set_Officer_Mood(3);
						Find_Debrief_Losses(out, section_page, rem_line);
					}
					break;
				case 4:
					section_pages = Count_Debrief_Captures();
					if (section_page < section_pages) {
						talk_Set_Officer_Mood(1);
						Find_Debrief_Captures(out, section_page, rem_line);
					}
					break;
			}
			rem_line -= section_pages * max_paragraph_size;
			section_page -= section_pages;
		}
		return;
	}

	/* Normal talk data: parse line-by-line */
	char* data = (char*)talk_brief->talk_data[5 * talk_mode + question_id];
	if (!data)
		return;

	/* Skip the question line (first line) */
	int16_t pos = 0;
	while (data[pos] && data[pos] != '\n')
		pos++;
	pos++; /* skip newline */

	int16_t line_idx = 0;
	while (data[pos] && line_idx <= line) {
		if (line_idx == line) {
			int16_t out_len = 0;
			if (italic) {
				out_len = 1;
				out[0] = 2; /* italic on */
			}
			while (data[pos] && data[pos] != '\n') {
				if (data[pos] == 3) {
					pos += 2;
					officer_mood_val[0] = data[pos - 1] - 1;
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
					pos += 2;
					officer_mood_val[0] = data[pos - 1] - 1;
				} else {
					pos++;
				}
			}
		}
		if (data[pos])
			pos++;
		line_idx++;
	}
}

/* ======================================================================
 * talk_Set_Talk_Paragraph — recalculate paragraph count
 * ====================================================================== */

void talk_Set_Talk_Paragraph(void) {
	if (cur_talk_question < 0 || cur_talk_question >= num_talk_questions) {
		cur_talk_paragraph = -1;
		num_talk_paragraphs = 0;
		return;
	}

	int16_t question_id = talk_win_id[cur_talk_question];
	if (question_id == 5) {
		cur_talk_paragraph = 0;
		num_talk_paragraphs = Count_Debrief_Pages();
		return;
	}

	char* data = (char*)talk_brief->talk_data[5 * talk_mode + question_id];
	if (!data) {
		cur_talk_paragraph = -1;
		num_talk_paragraphs = 0;
		return;
	}

	/* Skip question line */
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
	cur_talk_paragraph = num_talk_paragraphs ? 0 : -1;
}

/* ======================================================================
 * talk_Set_Talk_To_Text — reinitialize talk for text mode
 * ====================================================================== */

void talk_Set_Talk_To_Text(void) {
	active_talk_question = -1;
	cur_talk_question = -1;
	num_talk_paragraphs = 0;
	cur_talk_paragraph = -1;
	num_talk_questions = 0;

	/* Scan talk data slots for visibility status */
	Check_Talk_Questions();

	/* Build filtered question list */
	for (int16_t i = 0; i < 5; i++) {
		int16_t status = talk_win_status[i];
		int16_t show = 0;
		switch (status) {
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

	/* For debrief scenes, add debrief question */
	if (shellext_Get_Cur_Scene() == SCENE_TALK_DEBRIEF_OFFICER ||
		shellext_Get_Cur_Scene() == SCENE_TALK_DEBRIEF_PRIEST) {
		if (num_talk_questions == 5)
			talk_win_id[4] = 5; /* overwrite last slot */
		else
			talk_win_id[num_talk_questions++] = 5;
		cur_talk_question = num_talk_questions - 1;
		active_talk_question = num_talk_questions - 1;
		cur_talk_paragraph = 0;
		num_talk_paragraphs = Count_Debrief_Pages();
	}

	/* Add exit entry and size the talk widget */
	talk_win_id[num_talk_questions] = 6;

	int16_t total_h = 0, max_w = 0;
	char question_buf[80];
	for (int16_t q = 0; q <= num_talk_questions; q++) {
		talk_Get_Talk_Question(question_buf, q);
		int16_t saved_font = lfont_Get_Font();
		lfont_Set_Font(0);
		int16_t w = lfont_Get_String_Width(question_buf);
		lfont_Set_Font(saved_font);
		if (max_w < w)
			max_w = w;
		total_h += 10;
	}

	Rect r;
	lrect_Set_Rect(&r, 318 - (max_w + 6), 198 - (total_h + 3), 318, 198);
	linpattr_Set_Input_Frame(talk_input, &r);
}

/* ======================================================================
 * iupdate/iuser/idraw callbacks
 * ====================================================================== */

static int16_t iupdate_Talk(Input* input, Rect* r, Rect* clip_r, int16_t key, uint8_t left, uint8_t right,
							int16_t x, int16_t y) {
	(void)r;
	(void)clip_r;

	if (key)
		return 0;

	uint8_t button = left ? left : right;

	if (button <= 2) {
		int16_t hover;
		if (y < 0 || y >= 10 * (num_talk_questions + 1))
			hover = -1;
		else
			hover = y / 10;
		if (hover != active_talk_question) {
			active_talk_question = hover;
			linpattr_Refresh_Input(input);
			linpattr_Refresh_Input(answer);
			if (button) {
				input->var1 = 1;
				input->var2 = 1;
				return 1;
			}
		}
	} else if (button == 3) {
		if (active_talk_question != -1) {
			if (cur_talk_question == active_talk_question) {
				linpattr_Selected_Input(answer);
				answer->var2 = (right == 3);
			} else {
				linpattr_Selected_Input(input);
			}
			linpattr_Refresh_Input(input);
			linpattr_Refresh_Input(answer);
		}
		input->var1 = 0;
	}
	input->var2 = 1;
	return 1;
}

static void iuser_Talk(Input* input, int32_t time) {
	if (!time) {
		shade_Build_Shaded_Palette();
		linpattr_Show_Input(talk_input);
		linpattr_Refresh_Input(talk_input);
	}

	if (linpattr_Get_Input_Selected(input)) {
		if (active_talk_question == num_talk_questions) {
			/* Exit selected */
			int16_t scene = shellext_Get_Cur_Scene();
			if (scene == SCENE_TALK_BRIEF_OFFICER || scene == SCENE_TALK_BRIEF_PRIEST)
				lerror_Set_Landru_Exit(SCENE_BRIEF);
			else if (scene == SCENE_TALK_DEBRIEF_OFFICER || scene == SCENE_TALK_DEBRIEF_PRIEST)
				lerror_Set_Landru_Exit(SCENE_DEBRIEF);
		} else {
			cur_talk_question = active_talk_question;
			if (active_talk_question < 0 || active_talk_question >= num_talk_questions) {
				cur_talk_paragraph = -1;
				num_talk_paragraphs = 0;
			} else {
				int16_t qid = talk_win_id[cur_talk_question];
				if (qid == 5) {
					num_talk_paragraphs = Count_Debrief_Pages();
					cur_talk_paragraph = 0;
				} else {
					/* Count lines in talk data */
					char* data = (char*)talk_brief->talk_data[5 * talk_mode + qid];
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
					cur_talk_paragraph = num_talk_paragraphs ? 0 : -1;
				}

				/* Voice-over: kick off the .voc for this question and
				 * arm auto-advance. Retail TALK_iuser_Talk also
				 * suppresses auto-advance when the clicked question
				 * is the last debrief one (mood 'd' + last index) so
				 * the player reads the closing paragraph manually. */
				talk_voice_question = (int16_t)(cur_talk_question + 1);
				talk_Start_Speech_Stream();
				if (options_gbl.speech_active) {
					int16_t last = (int16_t)(num_talk_questions - 1);
					if (talk_voice_mood == 'd' && cur_talk_question == last)
						talk_paragraph_timer = 0x7FFFFFFF;
					else
						talk_paragraph_timer = time + 264;
				} else {
					talk_paragraph_timer = 0x7FFFFFFF;
				}
			}
			linpattr_Refresh_Input(input);
			linpattr_Refresh_Input(answer);
		}
	}

	if (input->var2) {
		input->var2 = 0;
	} else if (active_talk_question != -1) {
		active_talk_question = -1;
		linpattr_Refresh_Input(input);
		linpattr_Refresh_Input(answer);
	}
}

static void idraw_Talk(Input* input, Rect* r, Rect* clip_r, int16_t refresh) {
	if (!refresh)
		return;

	shade_Draw_Talk_Shade_Rect(r);
	Rect tr;
	lrect_Copy_Rect(&tr, r);
	tr.bottom = tr.top + 10 * num_talk_questions + 4;
	lfont_Enable_FontID_Shadow(0);

	char question_buf[80];
	int16_t y_offset = 0;
	for (int16_t q = 0; q <= num_talk_questions; q++) {
		int16_t color;
		if (q == active_talk_question)
			color = input->var1 ? 14 : 28;
		else
			color = (q == cur_talk_question) ? 9 : 24;

		int16_t qid = talk_win_id[q];
		if (qid == 6) {
			strcpy(question_buf, textext_Get_Text(txtTalkExit));
		} else if (qid == 5) {
			strcpy(question_buf, textext_Get_Text(txtTalkDebrief));
		} else {
			/* Extract question title from talk data */
			char* data = (char*)talk_brief->talk_data[5 * talk_mode + qid];
			if (data) {
				int16_t len = 0;
				for (int16_t i = 0; data[i] && data[i] != '\n'; i++) {
					if (data[i] == 4 || data[i] == 5)
						i++;
					else
						question_buf[len++] = data[i];
				}
				question_buf[len] = '\0';
			} else {
				question_buf[0] = '\0';
			}
		}
		lfont_Print_Clipped_Text(question_buf, tr.left + 3, y_offset + tr.top + 2, 0, color);
		y_offset += 10;
	}
	lfont_Disable_FontID_Shadow(0);

	if (linpattr_Is_Input_Dirty(input))
		ldirty_Dirty_Rect(clip_r);
}

static int16_t iupdate_Answer(Input* input, Rect* r, Rect* clip_r, int16_t key, uint8_t left, uint8_t right,
							  int16_t x, int16_t y) {
	(void)r;
	(void)clip_r;
	(void)x;
	(void)y;

	if (key)
		return 0;

	uint8_t button = left ? left : right;
	if (cur_talk_question != -1 && button) {
		if (button <= 1) {
			input->var1 = 1;
		} else if (button == 3) {
			input->var1 = 0;
			input->var2 = (right == 3);
			linpattr_Selected_Input(input);
		} else {
			return 1;
		}
		linpattr_Refresh_Input(input);
		linpattr_Refresh_Input(talk_input);
	}
	return 1;
}

/* ======================================================================
 * Voice-over streaming
 * ====================================================================== */

/* Per-frame user callback on the streaming Sound. Retail filled the
 * next 2 KB chunk from the CD streamer here; with our synchronous I/O
 * the file is fully loaded at trigger time so this is a no-op. */
// FUNCTION: TIE 0x6B43F
static void talk_Speech_User_Func(Sound* snd, int32_t time) {
	(void)snd;
	(void)time;
}

/* Allocate the talk-speech Sound + 2 MB streaming buffer. Mirrors
 * retail TALK_Alloc_Speech_Sound (sub_6B363). Idempotent — already
 * allocated returns the existing sound. */
// FUNCTION: TIE 0x6B363
void talk_Alloc_Speech_Sound(void) {
	if (talk_speech_sound)
		return;

	talk_speech_sound = lsound_Alloc_Sound(0);
	if (!talk_speech_sound)
		return;

	/* Retail TALK_Alloc_Speech_Sound writes 0x564F4943 (= FOURCC_VOIC as
	 * a little-endian DWORD). Use the same FOURCC so anything that
	 * looks at the sound list by res_type matches. */
	talk_speech_sound->res_type = FOURCC_VOIC;
	talk_speech_sound->type = digitalSound;
	talk_speech_sound->data = calloc(1, TALK_SPEECH_BUF_SIZE);
	talk_speech_sound->size = 0;
	talk_speech_pos = 0;
	talk_speech_streaming = 0;

	/* Match retail flag clears so the sound is freed normally on
	 * scene shutdown rather than being held alive. */
	lsound_Discard_Sound_Data(talk_speech_sound);
	lsound_Clear_Sound_Keep(talk_speech_sound);
	lsound_Clear_Sound_Keepable(talk_speech_sound);
	lsound_Clear_Sound_User_Keep(talk_speech_sound);
	lsound_Set_Sound_User_Function(talk_speech_sound, talk_Speech_User_Func);
}

/* Tear down the streaming state. Retail leaves the Sound and its
 * buffer to be reclaimed by the global sound free pass at scene
 * shutdown; we do the same — just clear our reference so the next
 * scene can re-allocate. */
// FUNCTION: TIE 0x6B400
void talk_Free_Speech_Sound(void) {
	talk_speech_sound = NULL;
	talk_speech_pos = 0;
	talk_speech_streaming = 0;
}

/* Build the .voc filename for the current talk_voice_* state into
 * `out`. Returns 1 on success, 0 on overflow / unsupported species.
 * Mirrors retail TALK_Start_Speech_Stream's sprintf logic, including
 * three branches:
 *
 *   1. officer 'i' (info briefing) — no mood char in filename.
 *      Mission name ending in 'w' (wraith variants) decrements idx.
 *
 *   2. mission-1 retry special case — when species == "1", mission ==
 *      1, mood == 'h', primary_complete != 1, secondary_complete ==
 *      1, and idx is 1 or 2: use the hardcoded fallback paths.
 *
 *   3. default — full sp/mission/officer/mood/idx filename.
 *
 * Path separators are forward slashes — lfile_Open_File handles
 * cross-platform translation. */
static int talk_Build_Voc_Path(char* out, size_t cap) {
	char sp[8] = { 0 };

	if (talk_voice_species >= 0) {
		/* Numeric species (1m1, 2m1, ...) */
		snprintf(sp, sizeof(sp), "%d", talk_voice_species);
	} else {
		/* Character-encoded species (1=f, 2=i, 3=b, 4=a, 5=g, 6=d, 7=m) */
		static const char species_chars[] = { 0, 'f', 'i', 'b', 'a', 'g', 'd', 'm' };
		int idx = -talk_voice_species;
		if (idx < 1 || idx > 7) {
			TieDiagnostics_Log(TIE_LOG_INFO, "talk_Build_Voc_Path: species %d out of range\n",
							   talk_voice_species);
			sp[0] = 'f';
		} else {
			sp[0] = species_chars[idx];
		}
	}

	int n;
	if (talk_voice_officer == 'i') {
		/* Briefing-officer voiceover: no mood character. Retail
		 * shaves one off the idx for missions whose name ends in
		 * 'w' — those use a separate (one-shorter) voice list. */
		const char* mission_name = shipext_Get_Mission_Name();
		size_t mn_len = mission_name ? strlen(mission_name) : 0;
		int16_t idx = talk_voice_question;
		if (mn_len > 0 && mission_name[mn_len - 1] == 'w')
			idx = (int16_t)(idx - 1);
		n = snprintf(out, cap, "voice/%sm%d/%sm%d%c%d.voc", sp, talk_voice_mission, sp, talk_voice_mission,
					 talk_voice_officer, idx);
	} else if (strcmp(sp, "1") == 0 && talk_voice_mission == 1 && talk_voice_mood == 'h' &&
			   mission.primary_complete != 1 && mission.secondary_complete == 1 &&
			   (talk_voice_question == 1 || talk_voice_question == 2)) {
		/* mission-1 secondary-only retry: retail picks fixed files
		 * for the first two questions. */
		const char* fixed = (talk_voice_question == 1) ? "voice/1m1/1m1od2.voc" : "voice/1m1/1m1oh1.voc";
		n = snprintf(out, cap, "%s", fixed);
	} else {
		n = snprintf(out, cap, "voice/%sm%d/%sm%d%c%c%d.voc", sp, talk_voice_mission, sp, talk_voice_mission,
					 talk_voice_officer, talk_voice_mood, talk_voice_question);
	}
	return (n > 0 && (size_t)n < cap);
}

/* Stop any running speech, build the next .voc filename, load the
 * file into the staging buffer, and start playback. Mirrors retail
 * TALK_Start_Speech_Stream (sub_6AFF9). Reads the file directly with
 * lfile rather than going through the lstream chain queue — retail's
 * 2 KB/frame streaming is unnecessary with synchronous I/O, and
 * lstream's chain queue is cumbersome when each click loads a fresh
 * file. Caller must ensure talk_speech_sound has been allocated.
 *
 * Falls back silently if speech is disabled or the .voc file is
 * missing — the screen still works without voice. */
// FUNCTION: TIE 0x6AFF9
void talk_Start_Speech_Stream(void) {
	if (!talk_speech_sound || !talk_speech_sound->data)
		return;
	if (!options_gbl.speech_active)
		return;

	char path[64];
	if (!talk_Build_Voc_Path(path, sizeof(path)))
		return;

	/* Stop any currently playing speech. */
	lsound_Stop_Sound(talk_speech_sound);
	talk_speech_streaming = 0;
	talk_speech_pos = 0;

	LandruFile* fp = lfile_Open_File(LANDRU_FILE_ROOT_ASSET, path, "rb");
	if (!fp) {
		TieDiagnostics_Log(TIE_LOG_INFO, "[talk-voice] missing %s\n", path);
		return;
	}
	TieDiagnostics_Log(TIE_LOG_INFO, "[talk-voice] play %s\n", path);

	memset(talk_speech_sound->data, 0, TALK_SPEECH_BUF_SIZE);
	size_t bytes_read = TieStorage_Read(talk_speech_sound->data, 1, TALK_SPEECH_BUF_SIZE, fp);
	lfile_Close_File(fp);

	talk_speech_pos = (int32_t)bytes_read;
	talk_speech_sound->size = (int32_t)bytes_read;
	talk_speech_streaming = (bytes_read >= TALK_SPEECH_BUF_SIZE);

	if (bytes_read == 0)
		return;

	lsound_Start_Speech(talk_speech_sound);
	imuse_set_param(im, (intptr_t)talk_speech_sound, 0x500, 100);
}

/* Initialize species/mission for a talk briefing/debrief. Mirrors
 * retail TALK_Set_Voice_Species_Mission (sub_6AF31): species and
 * mission come from the pilot record's tour-battle position; the
 * mood char is forced to 'h' (hostile/failed) when the relevant
 * objective is incomplete. */
// FUNCTION: TIE 0x6AF31
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

/* Combat-sim / training variant. Mirrors retail
 * MAP_Set_Voice_Species_Mission (sub_77C4C): when not in briefing
 * map mode, species/mission come from pilot_record.cur_combat_ship
 * and combat_course_cursor[ship]. Ships 0..NUM_SHIPS-1 (12 ship
 * categories) are encoded as char-coded species (negative species
 * code → 'f','i','b','a','g','d','m' lookup). Ship indices >=
 * NUM_SHIPS denote tour battles and are encoded numerically. */
void talk_Set_Voice_Species_Mission_Combat(void) {
	uint8_t ship = pilot_record.cur_combat_ship;
	if (ship >= NUM_SHIPS) {
		talk_voice_species = (int16_t)(ship - (NUM_SHIPS - 1)); /* 1, 2, ... */
	} else {
		talk_voice_species = (int16_t)(-(ship + 1)); /* -1..-NUM_SHIPS */
	}
	talk_voice_mission = (int16_t)(pilot_record.combat_course_cursor[ship] + 1);
}

static void iuser_Answer(Input* input, int32_t time) {
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

	if (linpattr_Get_Input_Selected(input)) {
		if (input->var2) {
			/* Right-click: previous page; disable auto-advance */
			if (cur_talk_paragraph) {
				talk_paragraph_timer = 0x7FFFFFFF;
				cur_talk_paragraph--;
			}
		} else {
			/* Left-click: next page; push threshold forward 264 ms,
			 * clamping on signed overflow. */
			talk_paragraph_timer += 264;
			if (talk_paragraph_timer < 0)
				talk_paragraph_timer = 0x7FFFFFFF;
			if (++cur_talk_paragraph >= num_talk_paragraphs) {
				talk_paragraph_timer = 0x7FFFFFFF;
				cur_talk_question = -1;
				cur_talk_paragraph = -1;
			}
		}
	}
}

static void idraw_Answer(Input* input, Rect* r, Rect* clip_r, int16_t refresh) {
	if (!refresh)
		return;
	if (cur_talk_paragraph == -1)
		goto dirty_check;

	Rect dst;
	lrect_Copy_Rect(&dst, r);
	int16_t first_line = max_paragraph_size * cur_talk_paragraph;
	int16_t text_color = input->var1 ? 1 : 9;

	lfont_Enable_FontID_Shadow(0);

	char line_buf[80];
	int16_t y_off = 0;
	for (int16_t cur_line = first_line; cur_line < first_line + max_paragraph_size; cur_line++) {
		line_buf[0] = '\0';
		center_line = 0;
		talk_Set_Officer_Mood(0);

		if (cur_talk_question >= 0 && cur_talk_question < num_talk_questions) {
			int16_t qid = talk_win_id[cur_talk_question];
			if (qid == 5) {
				Get_Debrief_Line(line_buf, cur_line, 0);
			} else {
				talk_Get_Talk_Paragraph(line_buf, cur_line);
			}
		}

		int16_t line_y = y_off + dst.top;
		if (center_line) {
			Rect line_rect;
			lrect_Set_Rect(&line_rect, dst.left, line_y, dst.right, line_y + 10);
			lfont_Print_Centered_Text(line_buf, &line_rect, text_color, 0);
		} else {
			lfont_Print_Clipped_Text(line_buf, dst.left, line_y, 0, text_color);
		}
		y_off += 10;
	}

	/* Page indicator */
	char page_buf[40];
	textext_Copy_Text(line_buf, txtTalkOf);
	snprintf(page_buf, sizeof(page_buf), line_buf, cur_talk_paragraph + 1, num_talk_paragraphs);
	textext_Copy_Text(line_buf, txtMapPage);
	strcat(line_buf, " ");
	strcat(line_buf, page_buf);
	lfont_Print_Clipped_Text(line_buf, dst.right - 84, y_off + dst.top, 0, text_color);
	lfont_Disable_FontID_Shadow(0);

dirty_check:
	if (linpattr_Is_Input_Dirty(input))
		ldirty_Dirty_Rect(clip_r);
}

/* ======================================================================
 * end_View — view update callback
 * ====================================================================== */

static void end_View(int32_t refresh) {
	if (refresh)
		return;
	if (lcursor_Is_Cursor_Visible())
		return;
	lcursor_Show_Cursor();
}

/* ======================================================================
 * talk_Talk — main entry point
 * ====================================================================== */

typedef enum {
	TALK_PHASE_BEGIN = 0,
	TALK_PHASE_CLEANUP = 1,
} TalkPhase;

typedef struct TalkTask {
	SceneHeadStruct* scene_head;
	ResFile* res_file;
	TalkPhase phase;
} TalkTask;

static LandruTaskStepResult talk_task_step(void* self) {
	TalkTask* t = (TalkTask*)self;

	if (t->phase != TALK_PHASE_BEGIN)
		goto cleanup;

	{
		Rect frame;
		int16_t talk_type = 0;
		int16_t i;

		t->res_file = shellext_Open_Empire_Resource(talk_str[0]);
		lrect_Set_Rect(&frame, 0, 0, 320, 200);

		int16_t scene = shellext_Get_Cur_Scene();
		switch (scene) {
			case SCENE_TALK_BRIEF_OFFICER:
				max_paragraph_size = 10;
				talk_type = 1;
				break;
			case SCENE_TALK_BRIEF_PRIEST:
				talk_type = 2;
				max_paragraph_size = 10;
				break;
			case SCENE_TALK_DEBRIEF_OFFICER:
				talk_type = 3;
				max_paragraph_size = 10;
				break;
			case SCENE_TALK_DEBRIEF_PRIEST:
				talk_type = 4;
				max_paragraph_size = 10;
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
		talk_film = lfilm_Res_Film(talk_str[talk_type], &frame, 0, 0, 0);
		TieSnapshotBuilder_SetActiveFilm("TALK", talk_str[talk_type]);
		lfilm_Set_Film_Def_Palette(talk_film, t->scene_head->def_palette);

		/* Find and disable the text overlay delta actor */
		Actor* delt = lactor_Find_Actor(FOURCC_DELT, talk_str[talk_type + 6]);
		// lactor_Non_Refreshable_Actor(delt);

		/* Set up face animation actors and voice-over filename chars.
		 * Retail TALK_Talk seeds officer/mood directly: 'o','b' for brief
		 * officer, 'p','b' for brief priest, 'o','d' for debrief officer,
		 * 'p','d' for debrief priest. */
		scene = shellext_Get_Cur_Scene();
		switch (scene) {
			case SCENE_TALK_BRIEF_OFFICER:
				eye_actor = lactor_Find_Actor(FOURCC_ANIM, "eyes");
				mouth_actor = lactor_Find_Actor(FOURCC_ANIM, "mouth");
				lactor_Set_Actor_User_Function(eye_actor, user_Talk_Eyes);
				eye_actor->id = 0;
				talk_voice_officer = 'o';
				talk_voice_mood = 'b';
				break;
			case SCENE_TALK_DEBRIEF_OFFICER:
				eye_actor = lactor_Find_Actor(FOURCC_ANIM, "eyes");
				mouth_actor = lactor_Find_Actor(FOURCC_ANIM, "mouth");
				lactor_Set_Actor_User_Function(eye_actor, user_Talk_Eyes);
				eye_actor->id = 0;
				talk_voice_officer = 'o';
				talk_voice_mood = 'd';
				break;
			case SCENE_TALK_BRIEF_PRIEST:
				eye_actor = lactor_Find_Actor(FOURCC_ANIM, "ssface");
				mouth_actor = NULL;
				lactor_Set_Actor_User_Function(eye_actor, user_Talk_Eyes);
				eye_actor->id = 1;
				talk_voice_officer = 'p';
				talk_voice_mood = 'b';
				break;
			case SCENE_TALK_DEBRIEF_PRIEST:
				eye_actor = lactor_Find_Actor(FOURCC_ANIM, "ssface");
				mouth_actor = NULL;
				lactor_Set_Actor_User_Function(eye_actor, user_Talk_Eyes);
				eye_actor->id = 1;
				talk_voice_officer = 'p';
				talk_voice_mood = 'd';
				break;
		}

		/* Build the input widget tree */
		parent = linput_Alloc_Input(NULL, &frame, 0, 0);

		lrect_Set_Rect(&frame, 122, 116 - 10 * (max_paragraph_size + 1), 318, 116);
		answer = linput_Alloc_Input(parent, &frame, 0, 0);
		linpattr_Set_Input_Update_Function(answer, iupdate_Answer);
		linpattr_Set_Input_User_Function(answer, iuser_Answer);
		linpattr_Set_Input_Draw_Function(answer, idraw_Answer);
		linpattr_Refreshable_Input(answer);
		answer->mouseUsage = allInput;
		answer->id = 0;

		lrect_Set_Rect(&frame, 122, 135, 318, 195);
		talk_input = linput_Alloc_Input(parent, &frame, 0, 0);
		linpattr_Set_Input_Update_Function(talk_input, iupdate_Talk);
		linpattr_Set_Input_User_Function(talk_input, iuser_Talk);
		linpattr_Set_Input_Draw_Function(talk_input, idraw_Talk);
		linpattr_Refreshable_Input(talk_input);
		talk_input->mouseUsage = allInput;
		talk_input->id = 0;

		/* Initialize talk state */
		player_Init_Brief_For_Talk();
		talk_brief = player_Fetch_Brief();
		talk_fgroup = player_Fetch_FGroup();

		num_talk_questions = 0;
		num_talk_paragraphs = 0;
		active_talk_question = -1;
		cur_talk_question = -1;
		cur_talk_paragraph = -1;

		Check_Talk_Questions();

		/* Build filtered question list */
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

		/* For debrief scenes, add debrief question */
		if (shellext_Get_Cur_Scene() == SCENE_TALK_DEBRIEF_OFFICER ||
			shellext_Get_Cur_Scene() == SCENE_TALK_DEBRIEF_PRIEST) {
			if (num_talk_questions == 5)
				talk_win_id[4] = 5;
			else
				talk_win_id[num_talk_questions++] = 5;
			cur_talk_question = num_talk_questions - 1;
			active_talk_question = num_talk_questions - 1;
			num_talk_paragraphs = Count_Debrief_Pages();
			cur_talk_paragraph = 0;
		}

		/* Add exit entry and size the talk widget */
		talk_win_id[num_talk_questions] = 6;

		int16_t total_h = 0, max_w = 0;
		char question_buf[80];
		for (i = 0; i <= num_talk_questions; i++) {
			talk_Get_Talk_Question(question_buf, i);
			int16_t saved_font = lfont_Get_Font();
			lfont_Set_Font(0);
			int16_t w = lfont_Get_String_Width(question_buf);
			lfont_Set_Font(saved_font);
			if (max_w < w)
				max_w = w;
			total_h += 10;
		}

		Rect r;
		lrect_Set_Rect(&r, 318 - (max_w + 6), 198 - (total_h + 3), 318, 198);
		linpattr_Set_Input_Frame(talk_input, &r);
		officer_mood_val[0] = 0;

		/* Position mouse */
		scene = shellext_Get_Cur_Scene();
		if (scene == SCENE_TALK_BRIEF_OFFICER || scene == SCENE_TALK_BRIEF_PRIEST)
			lio_Set_Mouse_Position(260, 192 - 10 * num_talk_questions);
		else if (scene == SCENE_TALK_DEBRIEF_OFFICER || scene == SCENE_TALK_DEBRIEF_PRIEST)
			lio_Set_Mouse_Position(260, 182);

		/* Resolve species/mission and arm the streaming speech sound.
		 * Retail does the same dance: TALK_Set_Voice_Species_Mission
		 * (sub_6AF31) then TALK_Alloc_Speech_Sound (sub_6B363). */
		talk_Set_Voice_Species_Mission();
		talk_Alloc_Speech_Sound();

		/* Push the modal view task */
		lview_Set_View_Update_Function(end_View);
		lviewadd_Clear_View();
		lview_Disable_All_View_Erase();
		lviewadd_Push_Handle_View_Task();

		t->phase = TALK_PHASE_CLEANUP;
		return LANDRU_TASK_STEP_CONTINUE;
	}

cleanup:
	/* CLEANUP — view popped */
	talk_Free_Speech_Sound();
	lview_Enable_All_View_Erase();
	lview_Clear_View_Update_Function();

	if (lcursor_Is_Cursor_Visible())
		lcursor_Hide_Cursor();

	player_Free_Display_Map();
	lres_Close_Resource(t->res_file);
	return LANDRU_TASK_STEP_DONE;
}

static const LandruTaskVtable talk_task_vt = {
	.step = talk_task_step,
};

void talk_Push_Talk_Task(SceneHeadStruct* scene_head) {
	TalkTask* t = (TalkTask*)landru_task_push(&talk_task_vt);
	if (!t)
		return;
	t->scene_head = scene_head;
	t->res_file = NULL;
	t->phase = TALK_PHASE_BEGIN;
}
