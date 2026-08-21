#include "tie/frontend_sound_tie98.h"

#include "tie/fsfx.h"
#include "tie_runtime/audio/flight_audio.h"

#include <stdio.h>
#include <string.h>

#define FRONTEND_SOUND_PENDING_CAPACITY 8
#define FRONTEND_SOUND_NAME_CAPACITY FSFX_SOUND_NAME_CAPACITY

typedef struct FrontendSoundPending {
	char name[FRONTEND_SOUND_NAME_CAPACITY];
	int start_mode;
	int loop;
	int priority;
	int volume;
	int pan;
	int use_voice_volume;
} FrontendSoundPending;

static FrontendSoundPending pending_sounds[FRONTEND_SOUND_PENDING_CAPACITY];
static int pending_sound_count;

static int loaded_sound_id(const char* name) { return fsfx_find_sound_id(name); }

static int find_pending(const char* name) {
	if (!name)
		return -1;
	for (int i = 0; i < pending_sound_count; ++i)
		if (strcmp(pending_sounds[i].name, name) == 0)
			return i;
	return -1;
}

static void remove_pending(int index) {
	if (index < 0 || index >= pending_sound_count)
		return;
	--pending_sound_count;
	if (index < pending_sound_count)
		memmove(&pending_sounds[index], &pending_sounds[index + 1],
				(size_t)(pending_sound_count - index) * sizeof pending_sounds[0]);
}

int FrontendSound_QueueSound(const char* name, int start_mode, int loop, int priority, int volume, int pan,
							 int use_voice_volume) {
	if (!name || loaded_sound_id(name) < 0 || pending_sound_count >= FRONTEND_SOUND_PENDING_CAPACITY ||
		priority < 0 || priority > 127 || volume < 0 || volume > 127 || pan < 0 || pan > 127)
		return 0;

	int insert = pending_sound_count;
	while (insert > 0 && pending_sounds[insert - 1].priority < priority) {
		pending_sounds[insert] = pending_sounds[insert - 1];
		--insert;
	}
	FrontendSoundPending* pending = &pending_sounds[insert];
	snprintf(pending->name, sizeof pending->name, "%s", name);
	pending->start_mode = start_mode;
	pending->loop = loop;
	pending->priority = priority;
	pending->volume = volume;
	pending->pan = pan;
	pending->use_voice_volume = use_voice_volume;
	++pending_sound_count;
	return 1;
}

void FrontendSound_FlushQueuedSounds(void) {
	while (pending_sound_count) {
		FrontendSoundPending pending = pending_sounds[0];
		remove_pending(0);
		int sound_id = loaded_sound_id(pending.name);
		if (sound_id < 0)
			continue;
		TieFlightWaveStart request = {
			.sound_id = (uint16_t)sound_id,
			.priority = (uint8_t)pending.priority,
			.volume = (uint8_t)pending.volume,
			.pan = (uint8_t)pending.pan,
			.loop = pending.loop != 0,
			.loop_first_voc_block = pending.loop != 0 && sound_id == FSFX_PLAYER_ENGINE_TIE_ID,
			.use_voice_group = pending.use_voice_volume != 0,
		};
		(void)TieFlightAudio_StartWave(&request);
	}
}

int FrontendSound_CountPlaying(const char* name) {
	int sound_id = loaded_sound_id(name);
	if (sound_id < 0)
		return 0;
	int active = TieFlightAudio_GetPlayCount((uint16_t)sound_id);
	if (active > 0)
		return active;
	int queued = 0;
	for (int i = 0; i < pending_sound_count; ++i)
		queued += strcmp(pending_sounds[i].name, name) == 0;
	return queued;
}

int FrontendSound_StopSoundByName(const char* name) {
	int sound_id = loaded_sound_id(name);
	if (sound_id < 0)
		return 0;
	if (TieFlightAudio_GetPlayCount((uint16_t)sound_id) > 0) {
		TieFlightAudio_StopWave((uint16_t)sound_id);
		return 1;
	}
	int pending = find_pending(name);
	if (pending < 0)
		return 0;
	remove_pending(pending);
	return 1;
}

int FrontendSound_GetVolume(const char* name) {
	int sound_id = loaded_sound_id(name);
	if (sound_id < 0)
		return -1;
	int active = TieFlightAudio_GetVolume((uint16_t)sound_id);
	if (active >= 0)
		return active;
	int pending = find_pending(name);
	return pending >= 0 ? pending_sounds[pending].volume : -1;
}

int FrontendSound_SetVolume(const char* name, int volume) {
	int sound_id = loaded_sound_id(name);
	if (sound_id < 0 || volume < 0 || volume > 127)
		return 0;
	if (TieFlightAudio_GetPlayCount((uint16_t)sound_id) > 0) {
		TieFlightAudio_SetVolume((uint16_t)sound_id, (uint8_t)volume);
		return 1;
	}
	int pending = find_pending(name);
	if (pending < 0)
		return 0;
	pending_sounds[pending].volume = volume;
	return 1;
}

int FrontendSound_SetPan(const char* name, int pan) {
	int sound_id = loaded_sound_id(name);
	if (sound_id < 0 || pan < 0 || pan > 127)
		return 0;
	if (TieFlightAudio_GetPlayCount((uint16_t)sound_id) > 0) {
		TieFlightAudio_SetPan((uint16_t)sound_id, (uint8_t)pan);
		return 1;
	}
	int pending = find_pending(name);
	if (pending < 0)
		return 0;
	pending_sounds[pending].pan = pan;
	return 1;
}

int FrontendSound_SetPriority(const char* name, int priority) {
	int sound_id = loaded_sound_id(name);
	if (sound_id < 0 || priority < 0 || priority > 127)
		return 0;
	if (TieFlightAudio_GetPlayCount((uint16_t)sound_id) > 0) {
		TieFlightAudio_SetPriority((uint16_t)sound_id, (uint8_t)priority);
		return 1;
	}
	int pending = find_pending(name);
	if (pending < 0)
		return 0;
	pending_sounds[pending].priority = priority;
	return 1;
}

int FrontendSound_GetPriority(const char* name) {
	int sound_id = loaded_sound_id(name);
	if (sound_id < 0)
		return -1;
	int active = TieFlightAudio_GetPriority((uint16_t)sound_id);
	if (active >= 0)
		return active;
	int pending = find_pending(name);
	return pending >= 0 ? pending_sounds[pending].priority : -1;
}

int FrontendSound_SetFrequency(const char* name, int frequency_hz) {
	int sound_id = loaded_sound_id(name);
	if (sound_id < 0 || frequency_hz < 0 || TieFlightAudio_GetPlayCount((uint16_t)sound_id) <= 0)
		return 0;
	TieFlightAudio_SetFrequency((uint16_t)sound_id, (uint32_t)frequency_hz);
	return 1;
}

int LOLEVEL_ImGetParam(uint16_t sound_id, int param) {
	if (sound_id < 4 || sound_id >= FSFX_NUM_SOUND_HANDLES)
		return param == 0x100 ? 0 : -1;
	const char* name = fsfx_sound_name(sound_id);
	if (!name)
		return param == 0x100 ? 0 : -1;
	if (param == 0x100)
		return FrontendSound_CountPlaying(name);
	if (param == 0x500)
		return FrontendSound_GetPriority(name);
	return -1;
}

int LOLEVEL_ImStopSound(uint16_t sound_id) {
	const char* name = fsfx_sound_name(sound_id);
	return name ? FrontendSound_StopSoundByName(name) : 0;
}

int LOLEVEL_ImSetParamByName(const char* name, int param, int value) {
	switch (param) {
		case 0x500:
			return FrontendSound_SetPriority(name, value);
		case 0x600:
			return FrontendSound_SetVolume(name, value);
		case 0x700:
			return FrontendSound_SetPan(name, value);
		case 0x777:
			return FrontendSound_SetFrequency(name, value);
		default:
			return 0;
	}
}
