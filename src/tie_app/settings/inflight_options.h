#ifndef TIE_APP_INFLIGHT_OPTIONS_H
#define TIE_APP_INFLIGHT_OPTIONS_H

#include <stdbool.h>
#include <stddef.h>

#include "tie_runtime/runtime/inflight_state.h"

bool TieInflightSettings_Configure(const TieInflightOptions* options);
void TieInflightSettings_Shutdown(void);
void TieInflightSettings_Get(TieInflightOptions* out);
bool TieInflightSettings_Set(const TieInflightOptions* options, char* error, size_t error_capacity);
bool TieInflightSettings_Flush(char* error, size_t error_capacity);

#endif
