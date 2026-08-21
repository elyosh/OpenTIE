#include "internal/tracks.h"
#include "internal/state.h"

#include "internal/debug.h"
#include "internal/dispatch.h"
#include "internal/fades.h"
#include "internal/files.h"
#include "internal/midi.h"
#include "internal/mixer.h"
#include "internal/streamer.h"
#include "internal/timer.h"
#include "internal/triggers.h"
#include "internal/utils.h"
#include "internal/wave.h"
#include "internal/wave_renderer.h"
#include <imuse/commands.h>
#include <imuse/groups.h>

#include <string.h>

/* Owns the 16 wave tracks and drives the per-tick mix/write pump. */

/* ===== Module-private state ===== */

/* Status block handed to the wave driver at INIT. sampleRateMode is
 * engine -> driver (we seed it from waveSpeed); the remaining fields
 * are driver -> driver (driver writes then mirrors into its own
 * ImDigitalOutBuf). The engine itself reads nothing back. */

/* 24-byte default output descriptor used in silent mode so the
 * mix pipeline still has a frame struct to pass around. All
 * fields zero = "no samples to write". */

/* ===== Lifecycle ===== */

int ImTracks_Init(imuse_t* im) {
	ImDebug_LogMsg(im, "TRACKS module...");

	/* Clamp waveMixCount to (0, 16]; default to 4 on invalid.
	 * The clamp is also pushed back into the engine config so
	 * save/restore version-checks see the resolved value. */
	int32_t mix = im->commands.config.waveMixCount;
	if (mix <= 0 || mix > 16) {
		ImDebug_LogMsg(im, "TR: waveMixCount out of range, defaulting to 4...");
		mix = 4;
		im->commands.config.waveMixCount = mix;
	}
	im->tracks.waveMixCount = mix;
	im->tracks.pauseTimer = 0;
	im->tracks.list = 0;

	/* Sample-timing selection. Either ~22 kHz or ~11 kHz. */
	if (im->commands.config.waveSpeed)
		im->tracks.nanosecsPerSample = 45454; /* ~22.0 kHz */
	else
		im->tracks.nanosecsPerSample = 90909; /* ~11.0 kHz */

	/* Pair each in-use track slot with its dispatch record and
	 * mark the slot free. Slots beyond waveMixCount are never
	 * allocated; leave them zeroed. */
	for (int i = 0; i < im->tracks.waveMixCount; ++i) {
		ImWaveTrack* t = &im->tracks.pool[i];
		t->prev = 0;
		t->next = 0;
		t->data = ImDispatch_GetById(im, i);
		t->data->sound = t;
		t->soundId = 0;
	}

	if (ImWaveRenderer_Init(im) != 0)
		return -1;
	if (ImMixer_Init(im) != 0)
		return -1;
	if (ImStreamer_Init(im) != 0)
		return -1;
	return 0;
}

int ImTracks_Deinit(imuse_t* im) {
	/* Reverse of Init: stop everything so nothing is mid-frame,
	 * then tear down renderer / streamer / mixer. Any stage
	 * returning non-zero aborts early. */
	ImTracks_StopAllSounds(im);
	if (ImWaveRenderer_Deinit(im) != 0)
		return -1;
	if (ImStreamer_Deinit(im) != 0)
		return -1;
	if (ImMixer_Deinit(im) != 0)
		return -1;
	return 0;
}

/* ===== Pause / Resume ===== */

void ImTracks_Pause(imuse_t* im) { im->tracks.pauseTimer = 1; }
void ImTracks_Resume(imuse_t* im) { im->tracks.pauseTimer = 0; }

/* ===== Save / Restore ===== */

int ImTracks_Save(imuse_t* im, void* buf, int size) {
	ImWave_Lock(im);
	int dispSize = ImDispatch_Save(im, buf, size);
	if (dispSize < 0) {
		ImWave_Unlock(im);
		return dispSize;
	}
	if ((unsigned int)(size - dispSize) < sizeof(im->tracks.pool)) {
		ImWave_Unlock(im);
		return -5;
	}
	memcpy((char*)buf + dispSize, im->tracks.pool, sizeof(im->tracks.pool));
	ImWave_Unlock(im);
	return dispSize + (int)sizeof(im->tracks.pool);
}

int ImTracks_Restore(imuse_t* im, void* buf) {
	ImWave_Lock(im);
	im->tracks.list = 0;

	int dispSize = ImDispatch_Restore(im, buf);
	memcpy(im->tracks.pool, (char*)buf + dispSize, sizeof(im->tracks.pool));

	/* Rewire pointer graph: prev/next from the save image are
	 * stale addresses. Re-bind dispatch records, and re-insert
	 * any slot whose soundId is non-zero back into the live list. */
	for (int i = 0; i < im->tracks.waveMixCount; ++i) {
		ImWaveTrack* t = &im->tracks.pool[i];
		t->prev = 0;
		t->next = 0;
		t->data = ImDispatch_GetById(im, i);
		t->data->sound = t;
		if (t->soundId)
			ImUtils_ListAddItem(im, &im->tracks.list, t);
	}

	ImDispatch_RestoreStreamers(im);
	ImWave_Unlock(im);
	return dispSize + (int)sizeof(im->tracks.pool);
}

/* ===== Group-volume propagation ===== */

void ImTracks_ApplyGroupVol(imuse_t* im) {
	/* effVol = (groupVol * (vol + 1)) >> 7. The +1 and >>7 keep
	 * all 128 steps representable (vol 0 → 1/128, vol 127 → 128/128). */
	for (ImWaveTrack* t = im->tracks.list; t; t = t->next) {
		int volPlus1 = t->vol + 1;
		t->effVol = (imuse_get_group_volume(im, t->group) * volPlus1) >> 7;
	}
}

/* ===== Per-tick pump ===== */

void ImTracks_Update(imuse_t* im) {
	/* Pause ramp: two extra ticks drain the renderer pipeline
	 * before we stop feeding frames; clamp at 3 thereafter so
	 * the counter doesn't grow unboundedly while paused. */
	if (im->tracks.pauseTimer) {
		if (++im->tracks.pauseTimer < 3)
			return;
		im->tracks.pauseTimer = 3;
	}

	ImWave_Lock(im);
	ImMidi_Lock(im);

	/* Acquire the next free half from the internal renderer. NULL
	 * means the ring is full — back-pressure: skip this tick. */
	ImDigitalOutBuf* outBuf = ImWaveRenderer_AcquireProduceFrame(im, &im->tracks.mixBufferOut);

	if (outBuf) {
		ImMixer_prepareDigitalOutput(im, outBuf, im->tracks.mixBufferOut);

		/* Skip the mix loop while paused, but still let the mixer
		 * commit any silence it just prepared so the renderer's
		 * ring keeps a primed half ready for the consumer. */
		if (!im->tracks.pauseTimer) {
			for (ImWaveTrack* cur = im->tracks.list; cur;) {
				/* Cache next: ImDispatch_PlaySoundFrame can call
				 * ImTracks_Clear(im, end-of-buffer) and unlink cur. */
				ImWaveTrack* next = cur->next;
				ImDispatch_PlaySoundFrame(im, cur, outBuf);
				cur = next;
			}
		}

		ImMixer_audioWriteToDriver(im);
	}

	ImMidi_Unlock(im);
	ImWave_Unlock(im);
}

/* ===== Sound control ===== */

int ImTracks_StartSound(imuse_t* im, intptr_t soundId, int priority, int bufId, uint32_t startFlags) {
	int pri = ImUtils_Clamp(im, priority, 0, 127);
	ImWaveTrack* t = ImTracks_GetFreeTrack(im, pri);
	if (!t)
		return -6;

	t->soundId = soundId;
	t->marker = 0;
	t->group = 0;
	t->priority = pri;
	t->vol = 127;
	t->effVol = imuse_get_group_volume(im, IMUSE_GROUP_MASTER);
	t->pan = 64;
	t->detune = 0;
	t->transpose = 0;
	t->detuneTrans = 0;
	t->mailbox = 0;
	t->jumpHook = 0;
	t->startFlags = startFlags;
	t->frequencyHz = 0;

	/* SetupSound parses the VOC header (in-memory) or allocates
	 * a streamer (bufId != 0). Failure → release the slot. */
	if (ImDispatch_SetupSound(im, t, bufId) != 0) {
		t->soundId = 0;
		return -1;
	}

	ImWave_Lock(im);
	ImUtils_ListAddItem(im, &im->tracks.list, t);
	ImWave_Unlock(im);
	return 0;
}

int ImTracks_StopSoundById(imuse_t* im, intptr_t soundId) {
	/* Multiple tracks may share a soundId (same sound started
	 * repeatedly). Walk and Clear every match. */
	int result = -1;
	for (ImWaveTrack* t = im->tracks.list; t;) {
		ImWaveTrack* next = t->next;
		if (t->soundId == soundId) {
			ImTracks_Clear(im, t);
			result = 0;
		}
		t = next;
	}
	return result;
}

int ImTracks_StopAllSounds(imuse_t* im) {
	ImWave_Lock(im);
	for (ImWaveTrack* cur = im->tracks.list; cur;) {
		ImWaveTrack* next = cur->next;
		ImTracks_Clear(im, cur);
		cur = next;
	}
	ImWave_Unlock(im);
	return 0;
}

intptr_t ImTracks_GetNextSound(imuse_t* im, intptr_t soundId) {
	intptr_t next = 0;
	for (ImWaveTrack* t = im->tracks.list; t; t = t->next) {
		if ((uintptr_t)t->soundId > (uintptr_t)soundId && (!next || (uintptr_t)t->soundId < (uintptr_t)next))
			next = t->soundId;
	}
	return next;
}

intptr_t ImTracks_QueryNextStream(imuse_t* im, intptr_t soundId, int* bufSize, int* lowWaterMark,
								  int* available) {
	ImWaveTrack* best = 0;
	for (ImWaveTrack* t = im->tracks.list; t; t = t->next) {
		if ((uintptr_t)t->soundId > (uintptr_t)soundId &&
			(!best || (uintptr_t)t->soundId < (uintptr_t)best->soundId) && t->data->streamPtr) {
			best = t;
		}
	}
	if (!best)
		return 0;
	ImStreamer_QueryStream(im, best->data->streamPtr, bufSize, lowWaterMark, available);
	return best->soundId;
}

void ImTracks_Clear(imuse_t* im, ImWaveTrack* track) {
	/* Unlink first so in-flight mixer/update steps skip the
	 * dying slot. Wildcard-cancel fades (param=-1) and triggers
	 * (marker=-1, opcode=-1) keyed on the soundId, then mark
	 * the slot free. */
	ImUtils_ListRemoveItem(im, &im->tracks.list, track);
	ImDispatch_Release(im, track);
	ImFades_DisableForSound(im, track->soundId, -1);
	ImTriggers_Clear(im, track->soundId, -1, -1);
	track->soundId = 0;
}

/* ===== Parameter dispatch ===== */

int ImTracks_SetParam(imuse_t* im, intptr_t soundId, int param, int value) {
	ImWaveTrack* t;
	for (t = im->tracks.list; t; t = t->next)
		if (t->soundId == soundId)
			break;
	if (!t)
		return -4;

	switch (param) {
		case IMUSE_PARAM_SOUND_GROUP:
			if ((unsigned int)value >= 0x10u)
				return -5;
			t->group = value;
			t->effVol = (imuse_get_group_volume(im, value) * (t->vol + 1)) >> 7;
			return 0;

		case IMUSE_PARAM_SOUND_PRIORITY:
			if ((unsigned int)value > 0x7Fu)
				return -5;
			t->priority = value;
			return 0;

		case IMUSE_PARAM_SOUND_VOL:
			if ((unsigned int)value > 0x7Fu)
				return -5;
			t->vol = value;
			t->effVol = (imuse_get_group_volume(im, t->group) * (value + 1)) >> 7;
			return 0;

		case IMUSE_PARAM_SOUND_PAN:
			if ((unsigned int)value > 0x7Fu)
				return -5;
			t->pan = value;
			return 0;

		case IMUSE_PARAM_SOUND_FREQUENCY: {
			if (value < 0 || t->data->streamPtr)
				return -5;
			if (!value) {
				t->frequencyHz = 0;
				t->data->resampleStep = 0;
				t->data->resamplePhase = 0;
				return 0;
			}
			uint32_t mixRateHz = (uint32_t)im->wave_renderer.waveSampleRate;
			uint64_t step = ((uint64_t)(uint32_t)value << 16) / mixRateHz;
			if (!step || step > UINT32_MAX)
				return -5;
			t->frequencyHz = value;
			t->data->resampleStep = (uint32_t)step;
			return 0;
		}

		case IMUSE_PARAM_SOUND_DETUNE:
			if (value < -9216 || value > 9216)
				return -5;
			t->detune = value;
			t->detuneTrans = (t->transpose << 8) + value;
			return 0;

		case IMUSE_PARAM_SOUND_TRANSPOSE:
			if (value < -12 || value > 12)
				return -5;
			/* Zero means RESET; non-zero adds and wraps, same
			 * convention as ImPlayers_SetParam. */
			t->transpose = value ? ImUtils_WrapSemitones(im, t->transpose + value, -12, 12) : 0;
			t->detuneTrans = (t->transpose << 8) + t->detune;
			return 0;

		case IMUSE_PARAM_SOUND_MAILBOX:
			t->mailbox = value;
			return 0;

		default:
			ImDebug_LogMsg(im, "ERR: TrSetParam() couldn't set param %lu...", (unsigned long)param);
			return -5;
	}
}

int ImTracks_GetParam(imuse_t* im, intptr_t soundId, int param) {
	/* soundPlayCount counts EVERY matching track; everything
	 * else returns from the first match. */
	int playCount = 0;
	for (ImWaveTrack* t = im->tracks.list; t; t = t->next) {
		if (t->soundId != soundId)
			continue;

		switch (param) {
			case IMUSE_PARAM_SOUND_TYPE:
				return -1;
			case IMUSE_PARAM_SOUND_PLAY_COUNT:
				++playCount;
				continue;
			case IMUSE_PARAM_SOUND_PEND_COUNT:
				return -1;
			case IMUSE_PARAM_SOUND_MARKER:
				return t->marker;
			case IMUSE_PARAM_SOUND_GROUP:
				return t->group;
			case IMUSE_PARAM_SOUND_PRIORITY:
				return t->priority;
			case IMUSE_PARAM_SOUND_VOL:
				return t->vol;
			case IMUSE_PARAM_SOUND_PAN:
				return t->pan;
			case IMUSE_PARAM_SOUND_FREQUENCY:
				return t->frequencyHz ? t->frequencyHz : (int)t->data->sourceRateHz;
			case IMUSE_PARAM_SOUND_DETUNE:
				return t->detune;
			case IMUSE_PARAM_SOUND_TRANSPOSE:
				return t->transpose;
			case IMUSE_PARAM_SOUND_MAILBOX:
				return t->mailbox;
			case IMUSE_PARAM_WAVE_STREAM_FLAG:
				return t->data->streamPtr != 0;
			default:
				return -5;
		}
	}
	return (param == IMUSE_PARAM_SOUND_PLAY_COUNT) ? playCount : -4;
}

/* ===== Allocator ===== */

int ImTracks_GetWaveMixCount(imuse_t* im) { return im->tracks.waveMixCount; }

ImWaveTrack* ImTracks_GetFreeTrack(imuse_t* im, int priority) {
	for (int i = 0; i < im->tracks.waveMixCount; ++i)
		if (!im->tracks.pool[i].soundId)
			return &im->tracks.pool[i];

	ImDebug_LogMsg(im, "ERR: no spare tracks, will try to preempt...");

	/* Preempt the lowest-priority live track. '<=' picks the
	 * LIFO-latest on ties — same as ImPlayers_GetFreePlayer. */
	int lowest = 127;
	ImWaveTrack* victim = 0;
	for (ImWaveTrack* t = im->tracks.list; t; t = t->next) {
		if (t->priority <= lowest) {
			lowest = t->priority;
			victim = t;
		}
	}
	if (!victim || priority < lowest)
		return 0;
	ImTracks_Clear(im, victim);
	return victim;
}
