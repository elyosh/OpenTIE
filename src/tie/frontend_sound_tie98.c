#include "tie/frontend_sound_tie98.h"
#include "tie/fsfx.h"
#include "tie_runtime/audio/flight_audio.h"
#include "tie_runtime/audio/flight_sound_bank.h"

#include <stdio.h>
#include <string.h>

enum {
	FRONTEND_SOUND_PENDING_CAPACITY = 8,
	FRONTEND_SOUND_NAME_CAPACITY = 64,
};

typedef struct FrontendSoundPending {
	char name[FRONTEND_SOUND_NAME_CAPACITY];
	int volume;
	int pan;
	int loop;
	int start_mode;
	int priority;
	int use_voice_volume;
} FrontendSoundPending;

/* Original storage includes one spare record beyond the eight-entry queue. */
// GLOBAL: TIE98 0x5F27FC
static FrontendSoundPending pending_sounds[FRONTEND_SOUND_PENDING_CAPACITY + 1];
// GLOBAL: TIE98 0x5F2B14
static int pending_sound_count;

// FUNCTION: TIE98 0x484560
int FrontendSound_QueueSound(const char* name, int start_mode, int loop, int priority, int volume, int pan,
							 int use_voice_volume) {
	int insert;
	FrontendSoundPending* pending;

	if (!name || TieFlightSound_FindId(name) < 0 || pending_sound_count >= FRONTEND_SOUND_PENDING_CAPACITY ||
		priority < 0 || priority > 127 || volume < 0 || volume > 127 || pan < 0 || pan > 127)
		return 0;

	insert = pending_sound_count;
	while (insert > 0 && pending_sounds[insert - 1].priority < priority) {
		pending_sounds[insert] = pending_sounds[insert - 1];
		--insert;
	}
	pending = &pending_sounds[insert];
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

// FUNCTION: TIE98 0x484510
void FrontendSound_FlushQueuedSounds(void) {
	while (pending_sound_count) {
		FrontendSoundPending pending = pending_sounds[0];
		int sound_id;
		TieFlightWaveStart request;

		--pending_sound_count;
		if (pending_sound_count > 0)
			memmove(&pending_sounds[0], &pending_sounds[1],
					(size_t)pending_sound_count * sizeof pending_sounds[0]);
		sound_id = TieFlightSound_FindId(pending.name);
		if (sound_id < 0)
			continue;
		request.sound_id = (uint16_t)sound_id;
		request.priority = (uint8_t)pending.priority;
		request.volume = (uint8_t)pending.volume;
		request.pan = (uint8_t)pending.pan;
		request.loop = pending.loop != 0;
		request.loop_first_voc_block = pending.loop != 0 && sound_id == FSFX_PLAYER_ENGINE_TIE_ID;
		request.use_voice_group = pending.use_voice_volume != 0;
		(void)TieFlightAudio_StartWave(&request);
	}
}

// FUNCTION: TIE98 0x4851a0
int FrontendSound_CountPlaying(const char* name) {
	int sound_id = TieFlightSound_FindId(name);
	int active;
	int queued = 0;
	int i;
	if (sound_id < 0)
		return 0;
	active = TieFlightAudio_GetPlayCount((uint16_t)sound_id);
	if (active > 0)
		return active;
	for (i = 0; i < pending_sound_count; ++i)
		queued += strcmp(pending_sounds[i].name, name) == 0;
	return queued;
}

// FUNCTION: TIE98 0x484a70
int FrontendSound_StopSoundByName(const char* name) {
	int sound_id = TieFlightSound_FindId(name);
	int pending;
	int i;
	if (sound_id < 0)
		return 0;
	if (TieFlightAudio_GetPlayCount((uint16_t)sound_id) > 0) {
		TieFlightAudio_StopWave((uint16_t)sound_id);
		return 1;
	}
	pending = -1;
	for (i = 0; name && i < pending_sound_count; ++i) {
		if (strcmp(pending_sounds[i].name, name) == 0) {
			pending = i;
			break;
		}
	}
	if (pending < 0)
		return 0;
	--pending_sound_count;
	if (pending < pending_sound_count)
		memmove(&pending_sounds[pending], &pending_sounds[pending + 1],
				(size_t)(pending_sound_count - pending) * sizeof pending_sounds[0]);
	return 1;
}

// FUNCTION: TIE98 0x484ec0
int FrontendSound_GetVolume(const char* name) {
	int sound_id = TieFlightSound_FindId(name);
	int pending;
	int i;
	int active;
	if (sound_id < 0)
		return -1;
	active = TieFlightAudio_GetVolume((uint16_t)sound_id);
	if (active >= 0)
		return active;
	pending = -1;
	for (i = 0; name && i < pending_sound_count; ++i) {
		if (strcmp(pending_sounds[i].name, name) == 0) {
			pending = i;
			break;
		}
	}
	return pending >= 0 ? pending_sounds[pending].volume : -1;
}

// FUNCTION: TIE98 0x484db0
int FrontendSound_SetVolume(const char* name, int volume) {
	int sound_id = TieFlightSound_FindId(name);
	int pending;
	int i;
	if (sound_id < 0 || volume < 0 || volume > 127)
		return 0;
	if (TieFlightAudio_GetPlayCount((uint16_t)sound_id) > 0) {
		TieFlightAudio_SetVolume((uint16_t)sound_id, (uint8_t)volume);
		return 1;
	}
	pending = -1;
	for (i = 0; name && i < pending_sound_count; ++i) {
		if (strcmp(pending_sounds[i].name, name) == 0) {
			pending = i;
			break;
		}
	}
	if (pending < 0)
		return 0;
	pending_sounds[pending].volume = volume;
	return 1;
}

// FUNCTION: TIE98 0x484f30
int FrontendSound_SetPan(const char* name, int pan) {
	int sound_id = TieFlightSound_FindId(name);
	int pending;
	int i;
	if (sound_id < 0 || pan < 0 || pan > 127)
		return 0;
	if (TieFlightAudio_GetPlayCount((uint16_t)sound_id) > 0) {
		TieFlightAudio_SetPan((uint16_t)sound_id, (uint8_t)pan);
		return 1;
	}
	pending = -1;
	for (i = 0; name && i < pending_sound_count; ++i) {
		if (strcmp(pending_sounds[i].name, name) == 0) {
			pending = i;
			break;
		}
	}
	if (pending < 0)
		return 0;
	pending_sounds[pending].pan = pan;
	return 1;
}

// FUNCTION: TIE98 0x4850b0
int FrontendSound_SetPriority(const char* name, int priority) {
	int sound_id = TieFlightSound_FindId(name);
	int pending;
	int i;
	if (sound_id < 0 || priority < 0 || priority > 127)
		return 0;
	if (TieFlightAudio_GetPlayCount((uint16_t)sound_id) > 0) {
		TieFlightAudio_SetPriority((uint16_t)sound_id, (uint8_t)priority);
		return 1;
	}
	pending = -1;
	for (i = 0; name && i < pending_sound_count; ++i) {
		if (strcmp(pending_sounds[i].name, name) == 0) {
			pending = i;
			break;
		}
	}
	if (pending < 0)
		return 0;
	pending_sounds[pending].priority = priority;
	return 1;
}

// FUNCTION: TIE98 0x485170
int FrontendSound_GetPriority(const char* name) {
	int sound_id = TieFlightSound_FindId(name);
	int pending;
	int i;
	int active;
	if (sound_id < 0)
		return -1;
	active = TieFlightAudio_GetPriority((uint16_t)sound_id);
	if (active >= 0)
		return active;
	pending = -1;
	for (i = 0; name && i < pending_sound_count; ++i) {
		if (strcmp(pending_sounds[i].name, name) == 0) {
			pending = i;
			break;
		}
	}
	return pending >= 0 ? pending_sounds[pending].priority : -1;
}

// FUNCTION: TIE98 0x485060
int FrontendSound_SetFrequency(const char* name, int frequency_hz) {
	int sound_id = TieFlightSound_FindId(name);
	if (sound_id < 0 || frequency_hz < 0 || TieFlightAudio_GetPlayCount((uint16_t)sound_id) <= 0)
		return 0;
	TieFlightAudio_SetFrequency((uint16_t)sound_id, (uint32_t)frequency_hz);
	return 1;
}

// FUNCTION: TIE98 0x42fd70
int LOLEVEL_ImGetParam(uint16_t sound_id, int param) {
	const char* name;
	if (sound_id < 4 || sound_id >= FSFX_NUM_SOUND_HANDLES)
		return param == 0x100 ? 0 : -1;
	name = TieFlightSound_Name(sound_id);
	if (!name)
		return param == 0x100 ? 0 : -1;
	if (param == 0x100)
		return FrontendSound_CountPlaying(name);
	if (param == 0x500)
		return FrontendSound_GetPriority(name);
	return -1;
}

// FUNCTION: TIE98 0x42fe20
int LOLEVEL_ImStopSound(uint16_t sound_id) {
	const char* name = TieFlightSound_Name(sound_id);
	return name ? FrontendSound_StopSoundByName(name) : 0;
}

// FUNCTION: TIE98 0x4300f0
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
