#ifndef TIE_INPUT_TRANSFORM_H
#define TIE_INPUT_TRANSFORM_H

#include <stdbool.h>

bool TieInputTransform_Relative(float relative_x, float relative_y, int source_w, int source_h,
								int presented_w, int presented_h, float* source_dx, float* source_dy);

#endif
