#ifndef TIE_TIE98_OPT_H
#define TIE_TIE98_OPT_H

#include <stdint.h>

#include "tie_runtime/flight_assets/model_types.h"

const Tie98OptimizedPolyObject* TieNativeOpt_Acquire(uint16_t model_type);
const Tie98OptimizedPolyObject* TieNativeOpt_AcquireNamed(const char* model_name, uint16_t* out_model_type);

#endif
