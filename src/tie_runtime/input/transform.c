#include "tie_runtime/input/transform.h"

bool TieInputTransform_Relative(float relative_x, float relative_y, int source_w, int source_h,
								int presented_w, int presented_h, float* source_dx, float* source_dy) {
	if (!source_dx || !source_dy || source_w <= 0 || source_h <= 0 || presented_w <= 0 || presented_h <= 0)
		return false;
	*source_dx = relative_x * (float)source_w / (float)presented_w;
	*source_dy = relative_y * (float)source_h / (float)presented_h;
	return true;
}
