#ifndef TIE_RUNTIME_AUDIO_FRONTEND_VOICE_H
#define TIE_RUNTIME_AUDIO_FRONTEND_VOICE_H

#include "tie_runtime/storage/storage.h"

typedef enum TieFrontendVoiceSource {
	TIE_FRONTEND_VOICE_SOURCE_TIE95,
	TIE_FRONTEND_VOICE_SOURCE_TIE98,
} TieFrontendVoiceSource;

TieFile* TieFrontendVoice_Open(const char* path, TieFrontendVoiceSource* source);
const char* TieFrontendVoice_SourceName(TieFrontendVoiceSource source);

#endif /* TIE_RUNTIME_AUDIO_FRONTEND_VOICE_H */
