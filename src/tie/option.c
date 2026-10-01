#include "tie/option.h"
#include "tie/edition.h"
#include "tie/fediskio.h"
#include "tie/flight_surface_tie98.h"
#include "tie/frontend_display_tie98.h"
#include "tie_runtime/audio/imuse_api.h"
#include "tie_runtime/audio/imuse_session.h"
#ifdef TIE_MODERN
#include "tie_runtime/diagnostics/diagnostics.h"
#include "tie_runtime/display/classic_display.h"
#include "tie_runtime/runtime/inflight_state.h"
#include "tie_runtime/runtime/options_task.h"
#endif
#include "tie/feinput.h"
#include "tie/festring.h"
#include "tie/rtsvga2.h"
#include "tie/tie.h"
#include "tie/user.h"

#include <imuse/hilevel.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#ifdef TIE_MODERN
#include <landru/task.h>
#endif

/* In-flight audio and gameplay settings persisted across missions. */
// GLOBAL: TIE95 0xc1550
// GLOBAL: TIE98 0x4f4118
int8_t inflight_music_vol;
// GLOBAL: TIE95 0xc1551
// GLOBAL: TIE98 0x4f411c
int8_t inflight_sound_vol;
// GLOBAL: TIE95 0xc1552
// GLOBAL: TIE98 0x4f4120
int8_t inflight_speech_vol;
// GLOBAL: TIE95 0xc1553
// GLOBAL: TIE98 0x58ac48
int8_t inflight_unlimited;
// GLOBAL: TIE95 0xc1554
// GLOBAL: TIE98 0x58ac4c
int8_t inflight_invulnerable;
// GLOBAL: TIE95 0xc1555
// GLOBAL: TIE98 0x4f4124
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
// GLOBAL: TIE95 0xc588c
static const uint8_t option_color[14] = {
	0x50, 0x50, 0x50, 0x50, 0x48, 0x48, 0x48, 0x48, 0x50, 0x50, 0x50, 0x48, 0x48, 0x48,
};

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
// GLOBAL: TIE95 0xd5048
// GLOBAL: TIE98 0x5fce48
static int32_t option_top;
// GLOBAL: TIE95 0xd5054
// GLOBAL: TIE98 0x5fce40
static int32_t option_bottom;

/*
 * Filled by fediskio_loadstringdata — point into the relocated
 * stringdata_buf. optionstrings has 14 entries (one label per row);
 * settingstrings has 16 slots (15 used). See kind_offsets[] below for
 * which range each row indexes.
 */
// GLOBAL: TIE95 0xD504C
// GLOBAL: TIE98 0x5FCE44
char** optionstrings;
// GLOBAL: TIE95 0xD5050
// GLOBAL: TIE98 0x5FCE4C
char** settingstrings;

/* --- Local constants --------------------------------------------------- */

enum {
	BG_DEFAULT = 0x44,
	TEXT_COLOUR = 0x43,
	CURSOR_COLOUR = 0x46,
	VOLUME_KIND = 15,  /* kind_offsets[] marker for volume rows */
	VOLUME_CELLS = 16, /* width of a volume bar, in half-font cells */
};

/* --- Static: volume bar ------------------------------------------------ */

/*
 * Render a 16-cell volume bar for one option row. The track is a clear
 * background (0x40) spanning 16 half-font-height cells right-aligned to
 * (screenXRes - fontheight/2). The first `vol` cells are then over-filled
 * with the highlight colour (0x53). Restores the default text bound
 * before returning so callers don't need to.
 */
// FUNCTION: TIE95 0x35504
// FUNCTION: TIE98 0x458BE0
static void option_outvolumebar(uint16_t vol, int16_t y) {
	const int half_fh = (int)(int8_t)fontheight >> 1;
	const int16_t left = (int16_t)(screenXRes - 17 * half_fh);
	uint16_t cell;

	festring_setbound((int16_t)(left - 1), (int16_t)(y + 1), (int16_t)(screenXRes - half_fh),
					  (int16_t)(y + fontheight - 1));
	festring_setbackcolor(0x40);
	clearwindow();

	for (cell = 0; cell < vol; cell++) {
		festring_setbound((int16_t)(left + cell * half_fh), (int16_t)(y + 2),
						  (int16_t)(left + (cell + 1) * half_fh - 1), (int16_t)(y + fontheight - 2));
		festring_setbackcolor(0x53);
		clearwindow();
	}

	festring_setbound(2, 0, (int16_t)(screenXRes - 2), (int16_t)screenYRes);
}

// FUNCTION: TIE95 0x34C40
int32_t option_optionsroom(int16_t load_settings) {
	uint8_t values[OPTION_ROW_COUNT];
	uint8_t max_values[OPTION_ROW_COUNT];
	uint8_t kind_offsets[OPTION_ROW_COUNT];
	uint16_t prev_buttons;
	int16_t selection;
	int16_t previous_selection;
	int16_t redraw_all;
	int16_t exit_code;
	int render_again = 1;
#ifdef TIE_MODERN
	OptionRoomState* continuation;
	if (load_settings) {
		TieInflightOptions_Apply();
		return 0;
	}
	continuation = landru_task_top();
	memcpy(values, continuation->values, sizeof values);
	memcpy(max_values, continuation->max_values, sizeof max_values);
	memcpy(kind_offsets, continuation->kind_offsets, sizeof kind_offsets);
	prev_buttons = continuation->prev_buttons;
	selection = continuation->selection;
	previous_selection = continuation->previous_selection;
	redraw_all = continuation->redraw_all;
	exit_code = continuation->exit_code;
	render_again = continuation->render;
#else
	{
		if (flightResolution == TIE_FLIGHT_RES_VGA) {
			option_top = 21;
			option_bottom = 182;
		} else if (flightResolution == TIE_FLIGHT_RES_SVGA || flightResolution == TIE_FLIGHT_RES_SVGA_16 ||
				   flightResolution == TIE_FLIGHT_RES_SVGA_D3D) {
			option_top = 51;
			option_bottom = 436;
		} else {
			option_top = 21;
			option_bottom = 182;
		}
	}
#endif
	if (!load_settings) {
#ifdef TIE_MODERN
		if (!continuation->started)
#endif
		{
			if (TIE_DISPLAY_DX5)
				FlightSurface_Lock();

#ifdef TIE_MODERN
			{
				if (flightResolution == TIE_FLIGHT_RES_VGA) {
					option_top = 21;
					option_bottom = 182;
				} else if (flightResolution == TIE_FLIGHT_RES_SVGA ||
						   flightResolution == TIE_FLIGHT_RES_SVGA_16 ||
						   flightResolution == TIE_FLIGHT_RES_SVGA_D3D) {
					option_top = 51;
					option_bottom = 436;
				} else {
					option_top = 21;
					option_bottom = 182;
				}
			}
#endif

			dropflag = 1;
			festring_setlinewrap(0);
			festring_setautofill(1);
			festring_setfontsize(1);
			festring_setbound(0, 0, (int16_t)screenXRes, (int16_t)screenYRes);
			festring_setbackcolor(BG_DEFAULT);
			festring_settextcolor(TEXT_COLOUR);

			{
				const int row_step = fontheight + 2;
				const int16_t right = (int16_t)(screenXRes - 2);

				int16_t run_top = (int16_t)option_top;
				int16_t y = run_top;
				uint16_t color = option_color[0];
				int i;

				for (i = 0; i < OPTION_ROW_COUNT; i++) {
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
			festring_setbound(0, 0, (int16_t)screenXRes, (int16_t)screenYRes);
			festring_setbackcolor(BG_DEFAULT);

			/* Pack owning globals into values[0..13]. values[1]/values[7]
			 * invert the stored range (higher index = more detail on-screen). */
			values[0] = (uint8_t)(gouraudflag != 0);
			values[1] = (uint8_t)(1 - shipdetailvalue);
			values[2] = (uint8_t)(starshipdetail - 1);
			values[3] = drawmarkingsflag;
			values[4] = drawbackdropflag;
			values[5] = drawdebrisflag;
			values[6] = palette_cycle_user;
			values[7] = (uint8_t)(2 - stardetaillevel);
			values[8] = (uint8_t)inflight_collision;
			values[9] = (uint8_t)inflight_invulnerable;
			values[10] = (uint8_t)inflight_unlimited;
			values[11] = (uint8_t)inflight_sound_vol;
			values[12] = (uint8_t)inflight_music_vol;
			values[13] = (uint8_t)inflight_speech_vol;

			max_values[0] = 1;
			max_values[1] = 2;
			max_values[2] = 3;
			max_values[3] = 1;
			max_values[4] = 1;
			max_values[5] = 1;
			max_values[6] = 1;
			max_values[7] = 1;
			max_values[8] = 1;
			max_values[9] = 1;
			max_values[10] = 1;
			max_values[11] = 16;
			max_values[12] = 16;
			max_values[13] = 16;
			kind_offsets[0] = 0;
			kind_offsets[1] = 4;
			kind_offsets[2] = 7;
			kind_offsets[3] = 0;
			kind_offsets[4] = 0;
			kind_offsets[5] = 0;
			kind_offsets[6] = 0;
			kind_offsets[7] = 2;
			kind_offsets[8] = 0;
			kind_offsets[9] = 11;
			kind_offsets[10] = 13;
			kind_offsets[11] = 15;
			kind_offsets[12] = 15;
			kind_offsets[13] = 15;

			prev_buttons = 0;
			selection = 0;
			exit_code = 0;
			previous_selection = 0;
			redraw_all = 1;
			if (TIE_DISPLAY_DX5)
				FlightSurface_Unlock();
#ifdef TIE_MODERN
			memcpy(continuation->values, values, sizeof values);
			memcpy(continuation->max_values, max_values, sizeof max_values);
			memcpy(continuation->kind_offsets, kind_offsets, sizeof kind_offsets);
			continuation->prev_buttons = prev_buttons;
			continuation->selection = selection;
			continuation->previous_selection = previous_selection;
			continuation->redraw_all = redraw_all;
			continuation->exit_code = exit_code;
			continuation->started = true;
			continuation->render = true;
			return 0;
#endif
		}
		for (;;) {
			if (render_again) {
				if (TIE_DISPLAY_DX5)
					FlightSurface_Lock();
				{
					int16_t cursor_y = (int16_t)option_top;
					uint8_t prev_color = option_color[0];
					int16_t row;

					festring_setbound(2, 0, (int16_t)(screenXRes - 2), (int16_t)screenYRes);
					for (row = 0; row < OPTION_ROW_COUNT; row++) {
						const uint16_t bg = row == selection ? CURSOR_COLOUR : option_color[row];
						if (option_color[row] != prev_color) {
							prev_color = option_color[row];
							cursor_y = (int16_t)(cursor_y + 2);
						}
						festring_setbackcolor(bg);
						if (redraw_all || row == selection || row == previous_selection) {
							const int kind = kind_offsets[row];
							festring_setcursor(2, cursor_y);
							festring_outstring((const uint8_t*)optionstrings[row]);
							outchar('\n');
							festring_setcursor(0, cursor_y);
							if (kind == VOLUME_KIND)
								option_outvolumebar(values[row], cursor_y);
							else
								festring_outstringright((const uint8_t*)settingstrings[kind + values[row]]);
						}
						cursor_y = (int16_t)(cursor_y + fontheight + 2);
					}
					previous_selection = selection;
					redraw_all = 0;
				}
				if (TIE_DISPLAY_DX5) {
					FlightSurface_Unlock();
					FrontendDisplay_BlitOffscreenToRenderSurface();
					FrontendDisplay_PresentFrame();
				}
#ifdef TIE_MODERN
				memcpy(continuation->values, values, sizeof values);
				memcpy(continuation->max_values, max_values, sizeof max_values);
				memcpy(continuation->kind_offsets, kind_offsets, sizeof kind_offsets);
				continuation->prev_buttons = prev_buttons;
				continuation->selection = selection;
				continuation->previous_selection = previous_selection;
				continuation->redraw_all = redraw_all;
				continuation->exit_code = exit_code;
				continuation->render = false;
				return 0;
#endif
			}
			{
				uint16_t key;
				uint16_t cur_buttons;
				int redraw = 0;
				int exit_room = 0;

				feinput_getrawinput();
				feinput_checkinput();
				feinput_degitterinput();
				inputdeltay = (int16_t)(inputdeltay * 2);

				key = (uint16_t)inputkey;

				if (key == 1) {
					/* LEFT */
					if (kind_offsets[selection] == VOLUME_KIND) {
						if (values[selection])
							values[selection]--;
						redraw = 1;
					} else {
						exit_code = -1;
						exit_room = 1;
					}
				} else if (key == 2) {
					/* RIGHT */
					if (kind_offsets[selection] == VOLUME_KIND) {
						if (values[selection] < max_values[selection])
							values[selection]++;
						redraw = 1;
					} else {
						exit_code = 1;
						exit_room = 1;
					}
				} else if (key == 3 || key == 0x38 /* '8' */) {
					/* UP */
					selection = selection ? (int16_t)(selection - 1) : (int16_t)(OPTION_ROW_COUNT - 1);
					redraw = 1;
				} else if (key == 4 || key == 0x32 /* '2' */) {
					/* DOWN */
					selection = (int16_t)(selection + 1);
					if (selection == OPTION_ROW_COUNT)
						selection = 0;
					redraw = 1;
				} else if (key == 0x0D    /* CR / '\r' */
						   || key == 0x20 /* ' ' */
						   || key == 0x2B /* '+' */
						   || key == 0x3D /* '=' */) {
					{
						const uint8_t cur = values[selection];
						values[selection] = (cur == max_values[selection]) ? 0 : (uint8_t)(cur + 1);
					}
					redraw = 1;
				} else if (key == 0x2D /* '-' */) {
					{
						const uint8_t cur = values[selection];
						values[selection] = cur ? (uint8_t)(cur - 1) : max_values[selection];
					}
					redraw = 1;
				} else if (key == 0x1B    /* ESC */
						   || key == 0x51 /* 'Q' */
						   || key == 0x71 /* 'q' */) {
					exit_code = 2;
					exit_room = 1;
				}

#ifndef TIE_MODERN
				if (key == 0xB0) {
					brightness_setting += 64;
					if (brightness_setting == 768)
						brightness_setting = 256;
					unblank();
				}
#endif

				/* Mouse: edge-detect a button-1 or button-2 release.
				 * Button 1 release -> selection++ (wrap).
				 * Button 2 release -> cycle current value forward. */
				cur_buttons = (uint16_t)(inputbuttons & 0x0F);
				if ((prev_buttons == 1 || prev_buttons == 2) && cur_buttons == 0) {
					if (prev_buttons == 1) {
						selection = (int16_t)(selection + 1);
						if (selection == OPTION_ROW_COUNT)
							selection = 0;
					} else {
						{
							const uint8_t cur = values[selection];
							values[selection] = (cur == max_values[selection]) ? 0 : (uint8_t)(cur + 1);
						}
					}
					redraw = 1;
				}
				prev_buttons = cur_buttons;

				if (exit_room)
					break;
				render_again = redraw;
			}
#ifdef TIE_MODERN
			memcpy(continuation->values, values, sizeof values);
			memcpy(continuation->max_values, max_values, sizeof max_values);
			memcpy(continuation->kind_offsets, kind_offsets, sizeof kind_offsets);
			continuation->prev_buttons = prev_buttons;
			continuation->selection = selection;
			continuation->previous_selection = previous_selection;
			continuation->redraw_all = redraw_all;
			continuation->exit_code = exit_code;
			continuation->render = render_again != 0;
			return 0;
#endif
		}
#ifdef TIE_MODERN
		{
			char error[128];
			TieInflightOptions_StoreLegacy(values);
			if (!TieInflightOptions_Flush(error, sizeof error))
				TieDiagnostics_Log(TIE_LOG_ERROR, "%s\n", error);
		}
#else
		if (fediskio_tryopenfile(TIE_FILE_ROOT_USER, "options.cfg", "wb", 0)) {
			int16_t i;
			for (i = 0; i < OPTION_ROW_COUNT; ++i)
				fputc(values[i], fileptr);
			fputc((brightness_setting - 256) >> 6, fileptr);
			fediskio_tryclosefile(0);
		}
#endif
	} else {
#ifndef TIE_MODERN
		int16_t i;
		exit_code = 0;
		if (fediskio_tryopenfile(TIE_FILE_ROOT_USER, "options.cfg", "rb", 0)) {
			for (i = 0; i < OPTION_ROW_COUNT; ++i)
				values[i] = (uint8_t)fgetc(fileptr);
			brightness_setting = fgetc(fileptr);
			if (brightness_setting > 7)
				brightness_setting = 7;
			brightness_setting = (brightness_setting << 6) + 256;
			fediskio_tryclosefile(0);
		} else {
			for (i = 0; i < OPTION_ROW_COUNT; ++i)
				values[i] = 1;
			values[1] = 2;
			values[2] = 3;
			values[9] = 0;
			values[10] = 0;
			values[11] = 15;
			values[12] = 12;
			values[13] = 15;
			brightness_setting = 256;
		}
#endif
	}

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
	inflight_collision = (int8_t)values[8];
	inflight_invulnerable = (int8_t)values[9];
	inflight_unlimited = (int8_t)values[10];
	inflight_sound_vol = (int8_t)values[11];
	inflight_music_vol = (int8_t)values[12];
	inflight_speech_vol = (int8_t)values[13];

	soundvolflag = (uint8_t)(inflight_speech_vol + inflight_sound_vol);
	musicvolflag = (uint8_t)inflight_music_vol;
	cheatingflag = (uint8_t)(cheatingflag | (uint8_t)inflight_invulnerable | (uint8_t)inflight_unlimited);
#ifdef TIE_MODERN
	TieInflightOptions_ApplyAudio();
	continuation->finished = true;
#else
	hilevel_ImSetSfxVol(inflight_sound_vol ? inflight_sound_vol * 8 - 1 : 0);
	hilevel_ImSetVoiceVol(inflight_speech_vol ? inflight_speech_vol * 8 - 1 : 0);
	hilevel_ImSetMusicVol(inflight_music_vol ? inflight_music_vol * 8 - 1 : 0);
#endif
	return exit_code;
}
