#ifndef TIE_RUNTIME_HOOKS_AXIS_INPUT_H
#define TIE_RUNTIME_HOOKS_AXIS_INPUT_H

#include <stdbool.h>
#include <stdint.h>

/* Signed flight-axis scaling (config flight.fix_axis_input_bias).
 *
 * USER_inputforplane scales each input delta as
 * (rate_scale * (uint16_t)delta) >> 15 and keeps the low 16 bits. A negative
 * delta therefore picks up a 2 * rate_scale bias, which only wraps to ~0 when
 * rate_scale is near 0x8000. The Assault Gunboat's pitch rate (0x0F00) gives
 * rate_scale 0x7800, so every pitch-down input in both original executables
 * gains a constant -4096. When enabled, the modern build scales the delta as
 * a signed value instead. */
bool TieAxisInputHook_Enabled(void);
void TieAxisInputHook_SetEnabled(bool enabled);
int16_t TieAxisInputHook_Scale(uint16_t rate_scale, int16_t delta);

#endif
