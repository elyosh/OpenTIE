#ifndef TIE_OPTION_H
#define TIE_OPTION_H

#include <stdint.h>

/* In-flight options. Configuration loading is synchronous; the editor runs
 * as a task and writes its result to user_submodal_result. */
void option_apply_options_cfg(void);
void option_Push_OptionsRoom_Task(void);

/*
 * Pointer tables into stringdata_buf (filled by fediskio_loadstringdata):
 *   optionstrings[0..13]  — row labels ("GOURAUD SHADING", ...)
 *   settingstrings[0..15] — right-column labels; kind_offsets[row]
 *                           selects the sub-range, values[row] the entry
 *                           within it (see option.c for the layout).
 */
extern char** optionstrings;
extern char** settingstrings;

#endif /* TIE_OPTION_H */
