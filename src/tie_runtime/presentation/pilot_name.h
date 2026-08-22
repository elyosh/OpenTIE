#ifndef TIE_RUNTIME_PRESENTATION_PILOT_NAME_H
#define TIE_RUNTIME_PRESENTATION_PILOT_NAME_H

#include <stddef.h>

/* Preserves the stored name while fitting the narrower TIE95 frontend layout. */
void TiePilotName_CopyForDisplay(char* dst, size_t capacity, const char* name);

#endif
