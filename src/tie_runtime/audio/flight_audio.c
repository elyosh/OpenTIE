#include "tie_runtime/audio/flight_audio.h"

#include "tie_runtime/audio/imuse_session.h"

#include <imuse/commands.h>
#include <imuse/groups.h>

int TieFlightAudio_StartWave(const TieFlightWaveStart* request) {
	if (!im || !request || (request->loop_first_voc_block && !request->loop))
		return -1;
	uint32_t flags = request->loop ? IMUSE_WAVE_START_LOOP : IMUSE_WAVE_START_NONE;
	if (request->loop_first_voc_block)
		flags |= IMUSE_WAVE_START_LOOP_FIRST_BLOCK;
	if (imuse_start_wave(im, request->sound_id, request->priority, flags) != 0)
		return -1;
	int group = request->use_voice_group ? IMUSE_GROUP_VOICE : IMUSE_GROUP_SFX;
	if (imuse_set_param(im, request->sound_id, IMUSE_PARAM_SOUND_GROUP, group) != 0 ||
		imuse_set_param(im, request->sound_id, IMUSE_PARAM_SOUND_VOL, request->volume) != 0 ||
		imuse_set_param(im, request->sound_id, IMUSE_PARAM_SOUND_PAN, request->pan) != 0) {
		imuse_stop_sound(im, request->sound_id);
		return -1;
	}
	return 0;
}

void TieFlightAudio_StopWave(uint16_t sound_id) {
	if (im)
		(void)imuse_stop_sound(im, sound_id);
}

int TieFlightAudio_GetPlayCount(uint16_t sound_id) {
	return im ? imuse_get_param(im, sound_id, IMUSE_PARAM_SOUND_PLAY_COUNT) : 0;
}

int TieFlightAudio_GetVolume(uint16_t sound_id) {
	return im ? imuse_get_param(im, sound_id, IMUSE_PARAM_SOUND_VOL) : -1;
}

int TieFlightAudio_GetPriority(uint16_t sound_id) {
	return im ? imuse_get_param(im, sound_id, IMUSE_PARAM_SOUND_PRIORITY) : -1;
}

void TieFlightAudio_SetVolume(uint16_t sound_id, uint8_t volume) {
	if (im)
		(void)imuse_set_param(im, sound_id, IMUSE_PARAM_SOUND_VOL, volume);
}

void TieFlightAudio_SetPan(uint16_t sound_id, uint8_t pan) {
	if (im)
		(void)imuse_set_param(im, sound_id, IMUSE_PARAM_SOUND_PAN, pan);
}

void TieFlightAudio_SetPriority(uint16_t sound_id, uint8_t priority) {
	if (im)
		(void)imuse_set_param(im, sound_id, IMUSE_PARAM_SOUND_PRIORITY, priority);
}

void TieFlightAudio_SetFrequency(uint16_t sound_id, uint32_t frequency_hz) {
	if (im && frequency_hz <= INT32_MAX)
		(void)imuse_set_param(im, sound_id, IMUSE_PARAM_SOUND_FREQUENCY, (int)frequency_hz);
}
