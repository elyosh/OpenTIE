#ifndef TIE_FORMATS_XACT_H
#define TIE_FORMATS_XACT_H

#include <stdbool.h>
#include <stddef.h>

#include "tie_formats/common.h"

bool TieXact_DecodeRgba8(const void* bytes, size_t size, TieRgbaFrames* out, TieFormatError* error);

#endif
