#ifndef TIE_FRONTEND_SOUND_TIE98_H
#define TIE_FRONTEND_SOUND_TIE98_H

#include <stdint.h>

/* Recovered TIE98 FrontendSound subset used by the flight engine loop. */
int FrontendSound_QueueSound(const char* name, int start_mode, int loop, int priority, int volume, int pan,
							 int use_voice_volume);
void FrontendSound_FlushQueuedSounds(void);
int FrontendSound_CountPlaying(const char* name);
int FrontendSound_StopSoundByName(const char* name);
int FrontendSound_GetVolume(const char* name);
int FrontendSound_SetVolume(const char* name, int volume);
int FrontendSound_SetPan(const char* name, int pan);
int FrontendSound_SetPriority(const char* name, int priority);
int FrontendSound_GetPriority(const char* name);
int FrontendSound_SetFrequency(const char* name, int frequency_hz);

int LOLEVEL_ImGetParam(uint16_t sound_id, int param);
int LOLEVEL_ImStopSound(uint16_t sound_id);
int LOLEVEL_ImSetParamByName(const char* name, int param, int value);

#endif /* TIE_FRONTEND_SOUND_TIE98_H */
