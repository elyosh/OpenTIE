#include "tie_runtime/presentation/pilot_name.h"

#include <string.h>

#include "tie_runtime/runtime/profile.h"

#define TIE95_PILOT_NAME_DISPLAY_MAX 13u

void TiePilotName_CopyForDisplay(char* dst, size_t capacity, const char* name) {
	if (!dst || !capacity)
		return;
	if (!name)
		name = "";

	size_t limit = capacity - 1;
	if (TieProfile_FrontendId() == TIE_FRONTEND_PROFILE_TIE95 && limit > TIE95_PILOT_NAME_DISPLAY_MAX)
		limit = TIE95_PILOT_NAME_DISPLAY_MAX;

	size_t length = 0;
	while (length < limit && name[length])
		length++;
	memcpy(dst, name, length);
	dst[length] = '\0';
}
