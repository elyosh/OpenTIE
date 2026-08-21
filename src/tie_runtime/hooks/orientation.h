#ifndef TIE_RUNTIME_HOOKS_ORIENTATION_H
#define TIE_RUNTIME_HOOKS_ORIENTATION_H

#include <stdbool.h>
#include <stdint.h>

bool TieOrientationHook_Enabled(void);
void TieOrientationHook_SetEnabled(bool enabled);
void TieOrientationHook_Apply(int16_t heading, int16_t pitch, int16_t roll, int16_t delta_heading,
							  int16_t delta_pitch, bool allow_yaw, int16_t* out_heading, int16_t* out_pitch,
							  int16_t* out_roll);

#endif
