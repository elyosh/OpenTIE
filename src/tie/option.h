#ifndef TIE_OPTION_H
#define TIE_OPTION_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Options, replay capture, and flight logic share this per-mission state.
 * Storage is owned by option.c. */

extern int8_t inflight_music_vol;
extern int8_t inflight_sound_vol;
extern int8_t inflight_speech_vol;
extern int8_t inflight_unlimited;
extern int8_t inflight_invulnerable;
extern int8_t inflight_collision;

enum { OPTION_ROW_COUNT = 14 };

typedef struct OptionRoomState {
	uint8_t values[OPTION_ROW_COUNT];
	uint8_t max_values[OPTION_ROW_COUNT];
	uint8_t kind_offsets[OPTION_ROW_COUNT];
	uint16_t prev_buttons;
	int16_t selection;
	int16_t previous_selection;
	int16_t redraw_all;
	int16_t exit_code;
} OptionRoomState;

void option_OpenRoom(OptionRoomState* state);
void option_RenderRows(OptionRoomState* state);
/* Poll result: 0 idle, 1 exit, 2 redraw. */
int option_PollOnce(OptionRoomState* state);
void option_ApplyFlightValues(const uint8_t* values);
void option_ApplyValues(const uint8_t* values);

/*
 * Pointer tables into stringdata_buf (filled by fediskio_loadstringdata):
 *   optionstrings[0..13]  — row labels ("GOURAUD SHADING", ...)
 *   settingstrings[0..15] — right-column labels; kind_offsets[row]
 *                           selects the sub-range, values[row] the entry
 *                           within it (see option.c for the layout).
 */
extern char** optionstrings;
extern char** settingstrings;

#ifdef __cplusplus
}
#endif

#endif
