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

int32_t option_optionsroom(int16_t load_settings);
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
