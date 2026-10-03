#include "tie_runtime/hooks/axis_input.h"

static bool s_enabled = true;

bool TieAxisInputHook_Enabled(void) { return s_enabled; }

void TieAxisInputHook_SetEnabled(bool enabled) { s_enabled = enabled; }

int16_t TieAxisInputHook_Scale(uint16_t rate_scale, int16_t delta) {
	if (!s_enabled)
		return (int16_t)(((uint32_t)rate_scale * (uint16_t)delta) >> 15);
	/* rate_scale <= 0x7FFF, so the signed product fits in 32 bits. Division
	 * truncates toward zero, keeping the response symmetric for +/- delta. */
	return (int16_t)(((int32_t)rate_scale * delta) / 32768);
}
