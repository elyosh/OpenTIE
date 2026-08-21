#include "tie_formats/internal.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool TieFormat_SetError(TieFormatError* error, int code, const char* format, ...) {
	if (error) {
		va_list args;
		error->code = code;
		va_start(args, format);
		vsnprintf(error->message, sizeof error->message, format, args);
		va_end(args);
	}
	return false;
}

void TieRgbaFrames_Free(TieRgbaFrames* frames) {
	if (!frames)
		return;
	for (uint16_t index = 0; index < frames->count; ++index)
		free(frames->frames[index].rgba);
	free(frames->frames);
	memset(frames, 0, sizeof *frames);
}
