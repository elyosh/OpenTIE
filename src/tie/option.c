#include "tie_runtime/audio/config.h"
#include "tie_runtime/audio/imuse_session.h"
#include "tie_runtime/diagnostics/diagnostics.h"
#include "tie_runtime/display/classic_display.h"
#include "tie_runtime/display/classic_framebuffer.h"
#include "tie_runtime/flight_assets/model_types.h"
#include "tie_runtime/input/input.h"
#include "tie_runtime/runtime/exports.h"
#include "tie_runtime/runtime/profile.h"
#include "tie_runtime/storage/storage.h"
#include <landru/task.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "tie/option.h"

#include "tie/fediskio.h"
#include "tie/feinput.h"
#include "tie/festring.h"
#include "tie/flight_surface_tie98.h"
#include "tie/frontend_display_tie98.h"
#include "tie/gamesnd.h"
#include "tie/rtsvga2.h" /* stardetaillevel */
#include "tie/tie.h"
#include "tie/user.h" /* soundvolflag, musicvolflag, user_submodal_result */
#include "tie_runtime/audio/music_policy.h"
#include "tie_runtime/runtime/inflight_state.h" /* inflight_*_vol / inflight_* toggles */
#include "tie_runtime/runtime/profile.h"

#include <imuse/hilevel.h>

/* In-flight audio and gameplay settings persisted across missions. */
int8_t inflight_music_vol;
// GLOBAL: TIE 0xC1551
int8_t inflight_sound_vol;
int8_t inflight_speech_vol;
int8_t inflight_unlimited;
int8_t inflight_invulnerable;
int8_t inflight_collision;

/* --- Module-owned globals (watdbg: option.c) --------------------------- */

/*
 * Per-row background-band colour. Two palette bands:
 *   0x50 (80) - graphics/detail section
 *   0x48 (72) - gameplay + audio section
 * The row-colour scan drawn before the main loop paints a rectangle per
 * run of equal colour so the banded visual pattern survives even when
 * the highlight moves across row boundaries.
 */
static const uint8_t option_color[14] = {
	0x50, 0x50, 0x50, 0x50, 0x48, 0x48, 0x48, 0x48, 0x50, 0x50, 0x50, 0x48, 0x48, 0x48,
};

static const char option_filename[] = "options.cfg";

/*
 * optionTop / optionBottom are module-global in watdbg. Source-level they
 * drive the row-colour-band render and the top-of-text cursor; no one
 * outside OPTION reads them, so keep them file-static.
 *
 * Layout per flightResolution at entry:
 *   0x13    (320x200 VGA)  -> top=21,  bottom=182
 *   0x101   (640x480 VESA) -> top=51,  bottom=436
 *   fallback               -> top=21,  bottom=182
 */
static int32_t option_top;
static int32_t option_bottom;

/*
 * Filled by fediskio_loadstringdata — point into the relocated
 * stringdata_buf. optionstrings has 14 entries (one label per row);
 * settingstrings has 16 slots (15 used). See kind_offsets[] below for
 * which range each row indexes.
 */
char** optionstrings;
char** settingstrings;

/* --- Local constants --------------------------------------------------- */

#define NUM_ROWS 14
#define BG_DEFAULT 0x44
#define TEXT_COLOUR 0x43
#define CURSOR_COLOUR 0x46
#define VOLUME_KIND 15  /* kind_offsets[] marker for volume rows */
#define VOLUME_CELLS 16 /* width of a volume bar, in half-font cells */

static const uint8_t option_default_values[NUM_ROWS] = {
	1, 2, 3, 1, 1, 1, 1, 1, 1, 0, 0, 16, 16, 16,
};

static const uint8_t option_max_values[NUM_ROWS] = {
	1, 2, 3, 1, 1, 1, 1, 1, 1, 1, 1, 16, 16, 16,
};

static uint8_t option_values[NUM_ROWS];
static bool option_values_loaded;

/* --- Static: volume bar ------------------------------------------------ */

/*
 * Render a 16-cell volume bar for one option row. The track is a clear
 * background (0x40) spanning 16 half-font-height cells right-aligned to
 * (screenXRes - fontheight/2). The first `vol` cells are then over-filled
 * with the highlight colour (0x53). Restores the default text bound
 * before returning so callers don't need to.
 *
 * The binary uses __usercall(vol@<ax>, y@<dx>); the C port drops the
 * register annotation — the compiler's ABI is a detail of the original
 * linker, not something we need to preserve here.
 */
static void out_volume_bar(uint16_t vol, int16_t y) {
	const int half_fh = (int)(int8_t)fontheight >> 1;
	const int16_t left = (int16_t)((int)screenXRes - 17 * half_fh);

	festring_setbound((int16_t)(left - 1), (int16_t)(y + 1), (int16_t)((int)screenXRes - half_fh),
					  (int16_t)(y + fontheight - 1));
	festring_setbackcolor(0x40);
	clearwindow();

	for (uint16_t cell = 0; cell < vol; cell++) {
		festring_setbound((int16_t)(left + cell * half_fh), (int16_t)(y + 2),
						  (int16_t)(left + (cell + 1) * half_fh - 1), (int16_t)(y + fontheight - 2));
		festring_setbackcolor(0x53);
		clearwindow();
	}

	festring_setbound(2, 0, (int16_t)((int)screenXRes - 2), (int16_t)screenYRes);
}

/* --- Static helpers for the main routine ------------------------------ */

/*
 * Pick the layout-specific top/bottom pixels. Mirrors the three-branch
 * block at the head of OPTION_optionsroom.
 */
static void pick_layout(void) {
	if (flightResolution == TIE_FLIGHT_RES_VGA) {
		option_top = 21;
		option_bottom = 182;
	} else if (tie_is_high_resolution_flight()) {
		option_top = 51;
		option_bottom = 436;
	} else {
		option_top = 21;
		option_bottom = 182;
	}
}

/*
 * Initial row-colour-band render. Walks option_color[] and flushes a
 * filled rectangle for each run of equal colour. The final rectangle
 * extends from the last colour change down to option_bottom.
 */
static void paint_row_bands(void) {
	const int row_step = fontheight + 2;
	const int16_t right = (int16_t)((int)screenXRes - 2);

	int16_t run_top = (int16_t)option_top;
	int16_t y = run_top;
	uint16_t color = option_color[0];

	for (int i = 0; i < NUM_ROWS; i++) {
		if (option_color[i] != color) {
			festring_setbound(2, (int16_t)(run_top - 1), right, (int16_t)(y - 1));
			festring_setbackcolor(color);
			clearwindow();
			y = (int16_t)(y + 2);
			run_top = y;
			color = option_color[i];
		}
		y = (int16_t)(y + row_step);
	}

	festring_setbound(2, (int16_t)(run_top - 1), right, (int16_t)option_bottom);
	festring_setbackcolor(color);
	clearwindow();
}

/*
 * Re-render the 14 text rows. The outstring() / outstringright() pair
 * puts the label on the left and the value label (or volume bar) on the
 * right of each row. Newline injections bracket the selection cursor
 * visually — when a cell is adjacent to the selected row, or when the
 * selection is at the list-wrap boundary, a '\n' is emitted to widen the
 * highlight strip into the next row.
 */
static void render_rows(const uint8_t* values, const uint8_t* kind_offsets, int16_t selection) {
	int16_t cursor_y = (int16_t)option_top;
	uint8_t prev_color = option_color[0];

	festring_setbound(2, 0, (int16_t)((int)screenXRes - 2), (int16_t)screenYRes);

	for (int16_t row = 0; row < NUM_ROWS; row++) {
		if (option_color[row] != prev_color) {
			prev_color = option_color[row];
			cursor_y = (int16_t)(cursor_y + 2);
		}

		const uint16_t bg = (row == selection) ? CURSOR_COLOUR : option_color[row];
		festring_setbackcolor(bg);

		festring_setcursor(2, cursor_y);
		festring_outstring((const uint8_t*)optionstrings[row]);

		/*
		 * Newline bracketing. `\n` sets the LAYOUT_NEWLINE flag on the
		 * current cursor row which, combined with the highlight colour,
		 * extends the cursor band into the adjacent row. Matches the
		 * three cases the binary emits:
		 *   1. rows within distance 1 of the selection
		 *   2. row 0 when selection is at or past 13 (wrap pre-paint)
		 *   3. row 13 when selection is at or before 0 (wrap post-paint)
		 */
		const int16_t dist = (int16_t)(row - selection);
		if (dist > -2 && dist < 2)
			outchar('\n');
		if (row == 0 && selection >= NUM_ROWS - 1)
			outchar('\n');
		if (row == NUM_ROWS - 1 && selection <= 0)
			outchar('\n');

		festring_setcursor(0, cursor_y);
		const int kind = kind_offsets[row];
		if (kind == VOLUME_KIND) {
			out_volume_bar(values[row], cursor_y);
		} else {
			festring_outstringright((const uint8_t*)settingstrings[kind + values[row]]);
		}
		cursor_y = (int16_t)(cursor_y + fontheight + 2);
	}
}

/*
 * Cycle the current row's value forward with wrap.
 *   v == max -> 0
 *   else        -> v + 1
 */
static void cycle_forward(uint8_t* values, const uint8_t* max_values, int16_t selection) {
	const uint8_t cur = values[selection];
	values[selection] = (cur == max_values[selection]) ? 0 : (uint8_t)(cur + 1);
}

/*
 * Cycle the current row's value backward with wrap.
 *   v == 0 -> max
 *   else   -> v - 1
 */
static void cycle_backward(uint8_t* values, const uint8_t* max_values, int16_t selection) {
	const uint8_t cur = values[selection];
	values[selection] = cur ? (uint8_t)(cur - 1) : max_values[selection];
}

static void apply_visual_to_globals(const uint8_t* values) {
	gouraudflag = (uint8_t)(values[0] << 6);
	shipdetailvalue = (int16_t)(1 - values[1]);
	shipdetailpolycnt = (uint16_t)(4 * values[1] + 8);
	starshipdetail = (uint16_t)(values[2] + 1);
	starshipexplodetail = (uint16_t)(((uint32_t)4096 << values[1]) - 1);
	drawmarkingsflag = values[3];
	drawbackdropflag = values[4];
	drawdebrisflag = values[5];
	palette_cycle_user = values[6];
	stardetaillevel = (uint16_t)(2 - values[7]);
	hyperspacedetail = (int16_t)(75 - 25 * (2 - values[7]));
}

static void apply_inflight_to_globals(const uint8_t* values) {
	inflight_collision = (int8_t)values[8];
	inflight_invulnerable = (int8_t)values[9];
	inflight_unlimited = (int8_t)values[10];
	inflight_sound_vol = (int8_t)values[11];
	inflight_music_vol = (int8_t)values[12];
	inflight_speech_vol = (int8_t)values[13];

	/*
	 * Volume 0..16 -> iMUSE 0..127 (0 mutes, else 8*v - 1).
	 * The binary reads the three inflight_*_vol bytes via an unaligned
	 * dword load on *_vol_loadbase + sar 24. We read the byte field
	 * directly — the result is identical.
	 */
	imuse_set_sfx_vol(im, inflight_sound_vol ? (int)inflight_sound_vol * 8 - 1 : 0);
	imuse_set_voice_vol(im, inflight_speech_vol ? (int)inflight_speech_vol * 8 - 1 : 0);
	imuse_set_music_vol(im, inflight_music_vol ? (int)inflight_music_vol * 8 - 1 : 0);
	if (TieMusicPolicy_UsesTie98())
		gamesnd_Set_CD_Volume(inflight_music_vol);

	soundvolflag = (uint8_t)(inflight_speech_vol + inflight_sound_vol);
	musicvolflag = (uint8_t)inflight_music_vol;
	cheatingflag = (uint8_t)(cheatingflag | (uint8_t)inflight_invulnerable | (uint8_t)inflight_unlimited);
}

/* Apply all 14 original options. Mirrors LABEL_112 of the binary. */
static void apply_to_globals(const uint8_t* values) {
	apply_visual_to_globals(values);
	apply_inflight_to_globals(values);
}

void TieInflightOptions_Load(void) {
	if (option_values_loaded)
		return;
	memcpy(option_values, option_default_values, sizeof option_values);
	TieFile* file = TieStorage_Open(TIE_FILE_ROOT_USER, option_filename, "rb");
	if (file) {
		(void)TieStorage_Read(option_values, 1, sizeof option_values, file);
		(void)TieStorage_Close(file);
	}
	for (int i = 0; i < NUM_ROWS; ++i) {
		if (option_values[i] > option_max_values[i])
			option_values[i] = option_default_values[i];
	}
	option_values_loaded = true;
}

void TieInflightOptions_Reset(void) {
	memset(option_values, 0, sizeof option_values);
	option_values_loaded = false;
}

void TieInflightOptions_Get(TieInflightOptions* out) {
	if (!out)
		return;
	TieInflightOptions_Load();
	*out = (TieInflightOptions) {
		.starfighter_collision_damage = option_values[8] != 0,
		.player_invulnerable = option_values[9] != 0,
		.unlimited_ammunition = option_values[10] != 0,
		.sound_effects_volume = option_values[11],
		.music_volume = option_values[12],
		.speech_volume = option_values[13],
	};
}

bool TieInflightOptions_Set(const TieInflightOptions* options) {
	if (!options || options->sound_effects_volume > 16 || options->music_volume > 16 ||
		options->speech_volume > 16)
		return false;
	TieInflightOptions_Load();
	option_values[8] = options->starfighter_collision_damage ? 1 : 0;
	option_values[9] = options->player_invulnerable ? 1 : 0;
	option_values[10] = options->unlimited_ammunition ? 1 : 0;
	option_values[11] = options->sound_effects_volume;
	option_values[12] = options->music_volume;
	option_values[13] = options->speech_volume;
	if (maingameflag && !replayviewmode)
		apply_inflight_to_globals(option_values);
	return true;
}

bool TieInflightOptions_Flush(char* error, size_t error_capacity) {
	TieInflightOptions_Load();
	if (TieStorage_WriteAllAtomic(TIE_FILE_ROOT_USER, option_filename, option_values, sizeof option_values) ==
		0)
		return true;
	if (error && error_capacity)
		snprintf(error, error_capacity, "could not save in-flight options");
	return false;
}

/* --- Public entry point ------------------------------------------------ */

/* --- apply_only path (cache -> globals, no UI) ----------------------
 *
 * Applied at flight entry from `tie_simulator`'s INIT phase. The shared
 * cache is loaded during runtime initialization and updated by either UI. */
void option_apply_options_cfg(void) {
	pick_layout();
	TieInflightOptions_Load();
	apply_to_globals(option_values);
}

typedef enum {
	OPTION_PHASE_RENDER = 0,
	OPTION_PHASE_POLL,
} OptionPhase;

typedef struct OptionTask {
	uint8_t values[NUM_ROWS];
	uint8_t max_values[NUM_ROWS];
	uint8_t kind_offsets[NUM_ROWS];
	uint16_t prev_buttons;
	int16_t selection;
	int16_t exit_code;
	OptionPhase phase;
} OptionTask;

/* Single input-poll iteration. 1 = exit_after_write fires (caller
 * persists + applies + DONE), 2 = redraw needed, 0 = no input. */
static int option_poll_once(OptionTask* t) {
	feinput_getrawinput();
	feinput_checkinput();
	feinput_degitterinput();
	inputdeltay = (int16_t)(inputdeltay * 2);

	const uint16_t key = (uint16_t)inputkey;
	int redraw = 0;
	int input_consumed = 0;

	/* Key dispatch. The binary uses a cascaded range tree; we mirror
	 * each branch so the behaviour matches, quirks included (notably
	 * the '-' case not setting input_consumed). */
	if (key == 1) {
		/* LEFT */
		if (t->kind_offsets[t->selection] == VOLUME_KIND) {
			if (t->values[t->selection])
				t->values[t->selection]--;
			redraw = 1;
		} else {
			t->exit_code = -1;
			return 1;
		}
		input_consumed = 1;
	} else if (key == 2) {
		/* RIGHT */
		if (t->kind_offsets[t->selection] == VOLUME_KIND) {
			if (t->values[t->selection] < t->max_values[t->selection])
				t->values[t->selection]++;
			redraw = 1;
		} else {
			t->exit_code = 1;
			return 1;
		}
		input_consumed = 1;
	} else if (key == 3 || key == 0x38 /* '8' */) {
		/* UP */
		t->selection = t->selection ? (int16_t)(t->selection - 1) : (int16_t)(NUM_ROWS - 1);
		input_consumed = 1;
		redraw = 1;
	} else if (key == 4 || key == 0x32 /* '2' */) {
		/* DOWN */
		t->selection = (int16_t)(t->selection + 1);
		if (t->selection == NUM_ROWS)
			t->selection = 0;
		input_consumed = 1;
		redraw = 1;
	} else if (key == 0x0D    /* CR / '\r' */
			   || key == 0x20 /* ' ' */
			   || key == 0x2B /* '+' */
			   || key == 0x3D /* '=' */) {
		cycle_forward(t->values, t->max_values, t->selection);
		input_consumed = 1;
		redraw = 1;
	} else if (key == 0x2D /* '-' */) {
		/* Binary quirk: cycles backward but does NOT set
		 * input_consumed, so the poll loop re-enters. Kept for
		 * parity. */
		cycle_backward(t->values, t->max_values, t->selection);
		redraw = 1;
	} else if (key == 0x1B    /* ESC */
			   || key == 0x51 /* 'Q' */
			   || key == 0x71 /* 'q' */) {
		t->exit_code = 2;
		return 1;
	}

	/* Mouse: edge-detect a button-1 or button-2 release.
	 * Button 1 release -> selection++ (wrap).
	 * Button 2 release -> cycle current value forward. */
	const uint16_t cur_buttons = (uint16_t)(inputbuttons & 0x0F);
	if ((t->prev_buttons == 1 || t->prev_buttons == 2) && cur_buttons == 0) {
		if (t->prev_buttons == 1) {
			t->selection = (int16_t)(t->selection + 1);
			if (t->selection == NUM_ROWS)
				t->selection = 0;
		} else {
			cycle_forward(t->values, t->max_values, t->selection);
		}
		input_consumed = 1;
		redraw = 1;
	}
	t->prev_buttons = cur_buttons;

	(void)input_consumed;
	return redraw ? 2 : 0;
}

// FUNCTION: TIE 0x34C40, TIE98 0x458340 (task-split recovery)
static LandruTaskStepResult option_task_step(void* self) {
	OptionTask* t = (OptionTask*)self;

	if (t->phase == OPTION_PHASE_RENDER) {
		const bool tie98_display = TieClassicDisplay_UsesDx5();
		if (tie98_display)
			FlightSurface_Lock();
		render_rows(t->values, t->kind_offsets, t->selection);
		if (tie98_display) {
			FlightSurface_Unlock();
			FrontendDisplay_BlitOffscreenToRenderSurface();
			FrontendDisplay_PresentFrame();
		}
		t->phase = OPTION_PHASE_POLL;
		return LANDRU_TASK_STEP_CONTINUE;
	}

	int r = option_poll_once(t);
	if (r == 1) {
		memcpy(option_values, t->values, sizeof option_values);
		option_values_loaded = true;
		char error[128];
		if (!TieInflightOptions_Flush(error, sizeof error))
			TieDiagnostics_Log(TIE_LOG_ERROR, "%s\n", error);
		apply_to_globals(t->values);
		user_submodal_result = (int32_t)t->exit_code;
		return LANDRU_TASK_STEP_DONE;
	}
	if (r == 2)
		t->phase = OPTION_PHASE_RENDER;
	return LANDRU_TASK_STEP_CONTINUE;
}

static const LandruTaskVtable option_task_vt = {
	.step = option_task_step,
};

void option_Push_OptionsRoom_Task(void) {
	const bool tie98_display = TieClassicDisplay_UsesDx5();
	if (tie98_display)
		FlightSurface_Lock();
	pick_layout();

	dropflag = 1;
	festring_setlinewrap(0);
	festring_setautofill(1);
	festring_setfontsize(1);
	festring_setbound(0, 0, (int16_t)screenXRes, (int16_t)screenYRes);
	festring_setbackcolor(BG_DEFAULT);
	festring_settextcolor(TEXT_COLOUR);

	/* Static row tables; the task struct keeps its own copy so the
	 * step body doesn't depend on file-scope state. */
	static const uint8_t init_kind_offsets[NUM_ROWS] = {
		0, 4, 7, 0, 0, 0, 0, 2, 0, 11, 13, VOLUME_KIND, VOLUME_KIND, VOLUME_KIND,
	};

	paint_row_bands();
	festring_setbound(0, 0, (int16_t)screenXRes, (int16_t)screenYRes);
	festring_setbackcolor(BG_DEFAULT);
	if (tie98_display)
		FlightSurface_Unlock();

	OptionTask* t = (OptionTask*)landru_task_push(&option_task_vt);
	if (!t)
		return;

	/* Pack owning globals into values[0..13]. values[1]/values[7]
	 * invert the stored range (higher index = more detail on-screen). */
	t->values[0] = (uint8_t)(gouraudflag != 0);
	t->values[1] = (uint8_t)(1 - shipdetailvalue);
	t->values[2] = (uint8_t)(starshipdetail - 1);
	t->values[3] = drawmarkingsflag;
	t->values[4] = drawbackdropflag;
	t->values[5] = drawdebrisflag;
	t->values[6] = palette_cycle_user;
	t->values[7] = (uint8_t)(2 - stardetaillevel);
	t->values[8] = (uint8_t)inflight_collision;
	t->values[9] = (uint8_t)inflight_invulnerable;
	t->values[10] = (uint8_t)inflight_unlimited;
	t->values[11] = (uint8_t)inflight_sound_vol;
	t->values[12] = (uint8_t)inflight_music_vol;
	t->values[13] = (uint8_t)inflight_speech_vol;

	memcpy(t->max_values, option_max_values, sizeof(option_max_values));
	memcpy(t->kind_offsets, init_kind_offsets, sizeof(init_kind_offsets));

	t->prev_buttons = 0;
	t->selection = 0;
	t->exit_code = 0;
	t->phase = OPTION_PHASE_RENDER;
}
