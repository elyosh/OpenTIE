#include "tie_runtime/audio/frontend_voice.h"

#include "tie_runtime/audio/config.h"
#include "tie_runtime/runtime/profile.h"

TieFile* TieFrontendVoice_Open(const char* path, TieFrontendVoiceSource* source) {
	if (source)
		*source = TieProfile_FrontendId() == TIE_FRONTEND_PROFILE_TIE98 ? TIE_FRONTEND_VOICE_SOURCE_TIE98
																		: TIE_FRONTEND_VOICE_SOURCE_TIE95;
	if (!path)
		return NULL;

	/* TIE95 ships higher-rate copies under the same frontend voice paths. */
	if (TieAudio_Config()->prefer_tie95_frontend_voices &&
		TieProfile_FrontendId() == TIE_FRONTEND_PROFILE_TIE98) {
		TieFile* file = TieStorage_OpenTie95Voice(path);
		if (file) {
			if (source)
				*source = TIE_FRONTEND_VOICE_SOURCE_TIE95;
			return file;
		}
	}
	return TieStorage_Open(TIE_FILE_ROOT_FRONTEND_ASSET, path, "rb");
}

const char* TieFrontendVoice_SourceName(TieFrontendVoiceSource source) {
	return source == TIE_FRONTEND_VOICE_SOURCE_TIE95 ? "tie95" : "tie98";
}
