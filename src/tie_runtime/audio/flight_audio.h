#ifndef TIE_RUNTIME_AUDIO_FLIGHT_AUDIO_H
#define TIE_RUNTIME_AUDIO_FLIGHT_AUDIO_H

#include <stdbool.h>
#include <stdint.h>

/* Runtime request translated from the recovered FrontendSound queue. */
typedef struct TieFlightWaveStart {
	uint16_t sound_id;
	uint8_t priority;
	uint8_t volume;
	uint8_t pan;
	bool loop;
	bool loop_first_voc_block;
	bool use_voice_group;
} TieFlightWaveStart;

int TieFlightAudio_StartWave(const TieFlightWaveStart* request);
void TieFlightAudio_StopWave(uint16_t sound_id);
int TieFlightAudio_GetPlayCount(uint16_t sound_id);
int TieFlightAudio_GetVolume(uint16_t sound_id);
int TieFlightAudio_GetPriority(uint16_t sound_id);
void TieFlightAudio_SetVolume(uint16_t sound_id, uint8_t volume);
void TieFlightAudio_SetPan(uint16_t sound_id, uint8_t pan);
void TieFlightAudio_SetPriority(uint16_t sound_id, uint8_t priority);
void TieFlightAudio_SetFrequency(uint16_t sound_id, uint32_t frequency_hz);

#endif /* TIE_RUNTIME_AUDIO_FLIGHT_AUDIO_H */
