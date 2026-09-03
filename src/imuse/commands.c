#include "internal/state.h"
#include <imuse/commands.h>

#include "internal/debug.h"
#include "internal/fades.h"
#include "internal/files.h"
#include "internal/groups.h"
#include "internal/midi.h"
#include "internal/midi_backend.h"
#include "internal/timer.h"
#include "internal/triggers.h"
#include "internal/wave.h"

#include <stdarg.h>
#include <string.h>

/* Host-driven timing model: the client calls imuse_advance(im, usec) each
 * frame with the elapsed microseconds. The engine runs its three tiers
 * (user-timer callback, 60 Hz, 10 Hz) off accumulated deltas rather than
 * a PIT interrupt. */

/* ===== Module-private state ===== */

/* Three tick accumulators (microseconds). The core accumulator consumes real
 * host time; the secondary tiers consume logical time from serviced ticks. */

/* Tier periods (microseconds). 16667 us = 60 Hz; 100000 us = 10 Hz.
 * The core tier's real period and logical duration are defined separately in
 * internal/timer.h. */
#define IM_TICK_60HZ_US 16667
#define IM_TICK_10HZ_US 100000

/* Catch-up cap: a single advance call never advances any tier by
 * more than this many of the *core* tier's ticks. Bounds the work
 * triggered by a long host stall (debugger break, alt-tab, slow
 * scene load) so a multi-second elapsed value can't fire a
 * multi-second burst of MIDI events / fades / ducks. */
#define IM_MAX_CATCHUP_TICKS 8

/* Sound-format magics (4 bytes each; embedded directly to avoid exporting
 * raw byte arrays). */
static const char MAGIC_MIDI[4] = { 'M', 'I', 'D', 'I' };
static const char MAGIC_CREA[4] = { 'C', 'r', 'e', 'a' };

/* ===== Init / Terminate ===== */

int ImCommands_Init(imuse_t* im, const ImuseHost* host, const ImuseConfig* cfg, ImuseMidiBackend* backend) {
	if (im->commands.initialized) {
		ImDebug_LogMsg(im, "ERROR:system already initialized...");
		ImMidi_BackendRelease(backend);
		return -1;
	}
	if (!host || !cfg) {
		ImMidi_BackendRelease(backend);
		return -1;
	}

	/* Copy host callbacks + config by value into the engine. The
	 * caller's structs may go out of scope on return. */
	im->files.host = *host;
	im->commands.config = *cfg;

	/* Mirror the host log callbacks into the commands fast-access
	 * slots; debug helpers read them on the hot path. */
	im->commands.logFunc = host->logFunc;
	im->commands.logUser = host->logUser;
	im->commands.timerCoreAccum = 0;
	im->commands.timer60HzAccum = 0;
	im->commands.timer10HzAccum = 0;

	ImDebug_LogMsg(im, "Initializing...COMMANDS module...");

	/* Take ownership of the host-supplied MIDI backend (NULL = MIDI
	 * silent) and bring it up at the configured output rate before
	 * anything that could emit MIDI events runs. Libimuse owns the
	 * backend from this point forward: it will be released here on
	 * any failure path or by ImCommands_Terminate on shutdown. */
	im->midi.backend = backend;
	int sr = cfg->outputSampleRate > 0 ? cfg->outputSampleRate : 44100;
	if (im->midi.backend && im->midi.backend->open) {
		if (im->midi.backend->open(im->midi.backend, sr, im->commands.logFunc, im->commands.logUser) != 0) {
			ImDebug_LogError(im, "MIDI backend open failed");
			ImMidi_BackendRelease(im->midi.backend);
			im->midi.backend = NULL;
			return -1;
		}
	}

	/* Cascade-init subsystems in the canonical iMUSE order. Any failure
	 * rolls back the ones that succeeded, leaving the engine uninitialized.
	 *
	 * Timer comes first for IDA-symbol parity with IMUSE.ENG; the
	 * port is a no-op now that the timer periods are compile-time
	 * constant. */
	if (ImTimer_Init(im) != 0)
		goto fail_timer;
	if (ImFiles_Init(im) != 0)
		goto fail_files;
	if (ImGroups_Init(im) != 0)
		goto fail_groups;
	if (ImFades_Init(im) != 0)
		goto fail_fades;
	if (ImTriggers_Init(im) != 0)
		goto fail_triggers;
	if (ImMidi_Init(im) != 0)
		goto fail_midi;
	if (ImWave_Init(im) != 0)
		goto fail_wave;

	im->commands.paused = 0;
	im->commands.initialized = 1;
	ImDebug_LogMsg(im, "Initialization complete...");
	return 48; /* iMUSE protocol-version / success sentinel */

fail_wave:
	ImMidi_Deinit(im);
fail_midi:
	ImTriggers_Deinit(im);
fail_triggers:
	ImFades_Deinit(im);
fail_fades:
	ImGroups_Deinit(im);
fail_groups:
	ImFiles_Deinit(im);
fail_files:
	ImTimer_Deinit(im);
fail_timer:
	if (im->midi.backend) {
		if (im->midi.backend->close)
			im->midi.backend->close(im->midi.backend);
		ImMidi_BackendRelease(im->midi.backend);
		im->midi.backend = NULL;
	}
	return -1;
}

int ImCommands_Terminate(imuse_t* im) {
	ImDebug_LogMsg(im, "Terminating...");

	/* Tear down in reverse-init order and preserve any failure. */
	int err = 0;
	err |= ImWave_Deinit(im);
	err |= ImMidi_Deinit(im);
	err |= ImTriggers_Deinit(im);
	err |= ImFades_Deinit(im);
	err |= ImGroups_Deinit(im);
	err |= ImFiles_Deinit(im);
	err |= ImTimer_Deinit(im);

	/* Close + release the MIDI backend last so the close hook's
	 * all-notes-off sweep has nothing else queueing events. The
	 * backend is library-owned — release frees the vtable struct and
	 * backend-private synthesis state. */
	if (im->midi.backend) {
		if (im->midi.backend->close)
			err |= im->midi.backend->close(im->midi.backend);
		ImMidi_BackendRelease(im->midi.backend);
		im->midi.backend = NULL;
	}

	im->commands.initialized = 0;
	im->commands.paused = 0;
	im->commands.timerCoreAccum = 0;
	im->commands.timer60HzAccum = 0;
	im->commands.timer10HzAccum = 0;
	im->commands.logFunc = 0;
	im->commands.logUser = 0;

	ImDebug_LogMsg(im, err ? "ERROR:termination error..." : "Termination complete...");
	return err;
}

/* ===== Host-driven Update ===== */

void imuse_advance(imuse_t* im, int32_t usecElapsed) {
	if (!im->commands.initialized || usecElapsed <= 0)
		return;

	/* Bound the number of original service ticks dispatched after a stall. */
	if (usecElapsed > IM_SERVICE_PERIOD_US * IM_MAX_CATCHUP_TICKS)
		usecElapsed = IM_SERVICE_PERIOD_US * IM_MAX_CATCHUP_TICKS;

	/* Core tier: MIDI sequencer step + wave frame pump.
	 *
	 * The DOS engine drove these from a fixed-rate PIT. Here we
	 * accumulate host-elapsed time and fire one service tick every
	 * IM_SERVICE_PERIOD_US. The original advances the sequencer by the nominal
	 * IM_LOGICAL_TICK_US on each service, which ImSeq_SetTempo bakes into
	 * its 16.16 step.
	 *
	 * Both updates self-gate on internal pause flags, so the loop
	 * runs unconditionally — debug refresh inside ImMidi_Update
	 * keeps ticking while the engine is paused. */
	int32_t logicalElapsed = 0;
	im->commands.timerCoreAccum += usecElapsed;
	while (im->commands.timerCoreAccum >= IM_SERVICE_PERIOD_US) {
		im->commands.timerCoreAccum -= IM_SERVICE_PERIOD_US;
		ImMidi_Update(im);
		ImWave_Update(im);
		logicalElapsed += IM_LOGICAL_TICK_US;
	}

	if (im->commands.paused || logicalElapsed == 0)
		return;

	/* 60 Hz tier: fades + triggers. */
	im->commands.timer60HzAccum += logicalElapsed;
	while (im->commands.timer60HzAccum >= IM_TICK_60HZ_US) {
		im->commands.timer60HzAccum -= IM_TICK_60HZ_US;
		ImFades_Update(im);
		ImTriggers_Update(im);
	}

	/* 10 Hz tier: asymmetric voice-ducks-music ramp.
	 *
	 * On each tick:
	 *   targetVol = groupMusic volume
	 *   if any live sound is in groupVoice: targetVol *= 47/128 (~0.367)
	 *   step the dipped-music group toward targetVol:
	 *     below  → +3 per tick (slow release, ~2.7 s full rise 0..127)
	 *     above  → -18 per tick (fast duck, ~0.45 s full drop 127..0)
	 */
	im->commands.timer10HzAccum += logicalElapsed;
	while (im->commands.timer10HzAccum >= IM_TICK_10HZ_US) {
		im->commands.timer10HzAccum -= IM_TICK_10HZ_US;

		uint32_t targetVol = imuse_set_group_volume(im, IMUSE_GROUP_MUSIC, -1);
		for (intptr_t id = imuse_next_sound(im, 0); id != 0; id = imuse_next_sound(im, id)) {
			if (imuse_get_param(im, id, IMUSE_PARAM_SOUND_GROUP) == IMUSE_GROUP_VOICE) {
				targetVol = (47u * targetVol) >> 7;
				break;
			}
		}

		uint32_t curVol = imuse_set_group_volume(im, IMUSE_GROUP_DIPPED, -1);
		uint32_t newVol;
		if (curVol < targetVol) {
			newVol = curVol + 3;
			if (newVol >= targetVol)
				newVol = targetVol;
		} else if (curVol > targetVol) {
			newVol = curVol - 18;
			if (newVol <= targetVol || newVol > curVol /* underflow */)
				newVol = targetVol;
		} else {
			continue;
		}
		imuse_set_group_volume(im, IMUSE_GROUP_DIPPED, (int)newVol);
	}
}

/* ===== Runtime control =====
 *
 * I9: ImCommands_Printf was dropped — hosts wanting a formatted log
 * helper format the message themselves and call host->logFunc. */

int imuse_pause(imuse_t* im) {
	int err = 0;
	if (!im->commands.paused) {
		err |= ImMidi_Pause(im);
		err |= ImWave_Pause(im);
	}
	int newDepth = ++im->commands.paused;
	return err ? err : newDepth;
}

int imuse_resume(imuse_t* im) {
	int err = 0;
	if (im->commands.paused == 1) {
		err |= ImMidi_Resume(im);
		err |= ImWave_Resume(im);
	}
	if (im->commands.paused)
		--im->commands.paused;
	return err ? err : im->commands.paused;
}

/* ===== Save / Restore ===== */

/* ===== Sound control ===== */

static int start_wave(imuse_t* im, intptr_t soundId, int priority, uint32_t flags) {
	const unsigned char* data = (const unsigned char*)ImFiles_GetSoundPtr(im, soundId);
	const uint32_t validFlags = IMUSE_WAVE_START_LOOP | IMUSE_WAVE_START_LOOP_FIRST_BLOCK;
	if (!data || memcmp(data, MAGIC_CREA, 4) != 0 || (flags & ~validFlags) != 0 ||
		((flags & IMUSE_WAVE_START_LOOP_FIRST_BLOCK) && !(flags & IMUSE_WAVE_START_LOOP)))
		return -1;
	return ImWave_StartSound(im, soundId, priority, flags);
}

int imuse_start_wave(imuse_t* im, intptr_t soundId, int priority, uint32_t flags) {
	return start_wave(im, soundId, priority, flags);
}

int imuse_start_sound(imuse_t* im, intptr_t soundId, int priority) {
	const unsigned char* data = (const unsigned char*)ImFiles_GetSoundPtr(im, soundId);
	if (!data) {
		ImDebug_LogTrace(im, "imuse_start_sound id=0x%llx pri=%d -> no-data", (unsigned long long)soundId,
						 priority);
		ImDebug_LogError(im, "ERR: null sound addr in StartSound()...");
		return -1;
	}

	/* Dispatch by 4-byte magic header: iMUSE MIDI containers start with
	 * 'MIDI', Creative VOC files with 'Crea'. Anything else is rejected.
	 * This is the single bottleneck every start-sound caller funnels
	 * through (hilevel Start{Music,Sfx,Voice}, lolevel ImStartSound,
	 * trigger/defer IMUSE_CMD_START_SOUND=8). Trace here to see every
	 * music/sfx/voice start regardless of the wrapper. */
	if (memcmp(data, MAGIC_MIDI, 4) == 0) {
		ImDebug_LogTrace(im, "imuse_start_sound id=0x%llx pri=%d kind=MIDI", (unsigned long long)soundId,
						 priority);
		return ImMidi_StartSound(im, soundId, priority);
	}
	if (memcmp(data, MAGIC_CREA, 4) == 0) {
		ImDebug_LogTrace(im, "imuse_start_sound id=0x%llx pri=%d kind=WAVE", (unsigned long long)soundId,
						 priority);
		return start_wave(im, soundId, priority, IMUSE_WAVE_START_NONE);
	}
	ImDebug_LogWarn(im, "imuse_start_sound id=0x%llx pri=%d -> bad-magic %02x%02x%02x%02x",
					(unsigned long long)soundId, priority, data[0], data[1], data[2], data[3]);
	return -1;
}

int imuse_stop_sound(imuse_t* im, intptr_t soundId) {
	int type = ImFiles_GetSoundType(im, soundId);
	{
		const char* kind = (type == IMUSE_SOUND_TYPE_MIDI)   ? "MIDI"
						   : (type == IMUSE_SOUND_TYPE_WAVE) ? "WAVE"
															 : "?";
		ImDebug_LogTrace(im, "imuse_stop_sound id=0x%llx kind=%s", (unsigned long long)soundId, kind);
	}
	if (type == IMUSE_SOUND_TYPE_MIDI)
		return ImMidi_StopSound(im, soundId);
	if (type == IMUSE_SOUND_TYPE_WAVE)
		return ImWave_StopSoundById(im, soundId);
	return -1;
}

int imuse_stop_all_sounds(imuse_t* im) {
	/* The original wipes Fades + Triggers first so no pending ramp or
	 * marker-trigger keeps a sound alive past the hard stop. Preserve
	 * that ordering and OR the return codes so a single errant subsystem
	 * doesn't mask the others. */
	int err = 0;
	err |= ImFades_Deinit(im);
	err |= ImTriggers_Deinit(im);
	err |= ImMidi_StopAllSounds(im);
	err |= ImWave_StopAllSounds(im);
	return err;
}

intptr_t imuse_next_sound(imuse_t* im, intptr_t soundId) {
	/* Return the smaller of the two next-sound candidates, skipping zeros.
	 * Zero is the end-of-enumeration sentinel. Both backends return
	 * soundIds in ascending order, so a plain comparison is enough.
	 *
	 * When soundIds are pointer values the "smaller" ordering is whatever
	 * the host's layout produces — consistent per Update pass, which is
	 * all this enumeration needs. */
	intptr_t m = ImMidi_GetNextSound(im, soundId);
	intptr_t w = ImWave_GetNextSound(im, soundId);
	if (m && (!w || (uintptr_t)m < (uintptr_t)w))
		return m;
	return w;
}

int imuse_set_param(imuse_t* im, intptr_t soundId, int param, int value) {
	int type = ImFiles_GetSoundType(im, soundId);
	if (type == IMUSE_SOUND_TYPE_MIDI)
		return ImMidi_SetParam(im, soundId, param, value);
	if (type == IMUSE_SOUND_TYPE_WAVE)
		return ImWave_SetParam(im, soundId, param, value);
	return -1;
}

int imuse_get_param(imuse_t* im, intptr_t soundId, int param) {
	int type = ImFiles_GetSoundType(im, soundId);

	/* soundType returns the backend classification directly, even for
	 * unloaded sounds (returns -1). */
	if (param == IMUSE_PARAM_SOUND_TYPE)
		return type;

	/* soundPendCount is a cross-module query that reads from the
	 * trigger queue rather than a specific player. */
	if (param == IMUSE_PARAM_SOUND_PEND_COUNT)
		return ImTriggers_GetPendingSoundCount(im, soundId);

	if (type == IMUSE_SOUND_TYPE_MIDI)
		return ImMidi_GetParam(im, soundId, param);
	if (type == IMUSE_SOUND_TYPE_WAVE)
		return ImWave_GetParam(im, soundId, param);

	/* soundPlayCount returns 0 for unknown sounds (the original allowed
	 * polling an unloaded id without logging). */
	if (param == IMUSE_PARAM_SOUND_PLAY_COUNT)
		return 0;
	return -1;
}

void imuse_set_hook(imuse_t* im, intptr_t soundId, uint32_t hookId) {
	int type = ImFiles_GetSoundType(im, soundId);
	if (type == IMUSE_SOUND_TYPE_MIDI)
		ImMidi_SetHook(im, soundId, hookId);
	else if (type == IMUSE_SOUND_TYPE_WAVE)
		ImWave_SetHook(im, soundId, hookId);
}

int imuse_get_hook(imuse_t* im, intptr_t soundId) {
	int type = ImFiles_GetSoundType(im, soundId);
	if (type == IMUSE_SOUND_TYPE_MIDI)
		return ImMidi_GetHook(im, soundId);
	if (type == IMUSE_SOUND_TYPE_WAVE)
		return ImWave_GetHook(im, soundId);
	return -1;
}

/* Opcode dispatcher for trigger/defer replay.
 *
 * args is the 10-slot arg array captured at trigger-set / defer-schedule
 * time. Only the leading slots the target handler consumes are read;
 * the rest are dead.
 *
 * SetTrigger and DeferCommand are disallowed because they would recursively
 * mutate the table being replayed.
 */
int ImCommands_ExecOpcode(imuse_t* im, int opcode, const intptr_t args[10]) {
	switch ((ImuseOpcode)opcode) {
		case IMUSE_CMD_INIT:
			return -1; /* not replayable: imuse_create only */
		case IMUSE_CMD_TERMINATE:
			return ImCommands_Terminate(im);
		case IMUSE_CMD_PRINTF:
			return 0; /* variadic: can't be replayed */
		case IMUSE_CMD_PAUSE:
			return imuse_pause(im);
		case IMUSE_CMD_RESUME:
			return imuse_resume(im);
		case IMUSE_CMD_SAVE:
			return -1; /* not replayable: see <imuse/state.h> */
		case IMUSE_CMD_RESTORE:
			return -1; /* not replayable: see <imuse/state.h> */
		case IMUSE_CMD_SET_GROUP_VOL:
			return imuse_set_group_volume(im, (int)args[0], (int)args[1]);
		case IMUSE_CMD_START_SOUND:
			return imuse_start_sound(im, args[0], (int)args[1]);
		case IMUSE_CMD_STOP_SOUND:
			return imuse_stop_sound(im, args[0]);
		case IMUSE_CMD_STOP_ALL_SOUNDS:
			return imuse_stop_all_sounds(im);
		case IMUSE_CMD_GET_NEXT_SOUND:
			return (int)imuse_next_sound(im, args[0]);
		case IMUSE_CMD_SET_PARAM:
			return imuse_set_param(im, args[0], (int)args[1], (int)args[2]);
		case IMUSE_CMD_GET_PARAM:
			return imuse_get_param(im, args[0], (int)args[1]);
		case IMUSE_CMD_FADE_PARAM:
			return ImFades_Param(im, args[0], (int)args[1], (int)args[2], (int)args[3]);
		case IMUSE_CMD_SET_HOOK:
			imuse_set_hook(im, args[0], (uint32_t)args[1]);
			return 0;
		case IMUSE_CMD_GET_HOOK:
			return imuse_get_hook(im, args[0]);
		case IMUSE_CMD_SET_TRIGGER:
			return -1; /* re-entry from replay not allowed */
		case IMUSE_CMD_CHECK_TRIGGER:
			return ImTriggers_Check(im, args[0], (int)args[1], args[2]);
		case IMUSE_CMD_CLEAR_TRIGGER:
			return ImTriggers_Clear(im, args[0], (int)args[1], args[2]);
		case IMUSE_CMD_DEFER_CMD:
			return -1; /* re-entry from replay not allowed */
		case IMUSE_CMD_JUMP_MIDI:
			return ImMidi_Jump(im, args[0], (int)args[1], (int)args[2], (int)args[3], (int)args[4],
							   (int)args[5]);
		case IMUSE_CMD_SCAN_MIDI:
			return ImMidi_Scan(im, args[0], (int)args[1], (int)args[2], (int)args[3], (int)args[4]);
		case IMUSE_CMD_SEND_MIDI_MSG:
			return ImMidi_SendMidiMsg(im, args[0], (int)args[1], (int)args[2], (int)args[3]);
		case IMUSE_CMD_SHARE_PARTS:
			return ImMidi_ShareParts(im, args[0], args[1]);
		case IMUSE_CMD_START_STREAM:
			return ImWave_StartStream(im, args[0], (int)args[1], (int)args[2]);
		case IMUSE_CMD_SWITCH_STREAM:
			return ImWave_SwitchStream(im, args[0], args[1], (void*)args[2], (int)args[3], (int)args[4]);
		case IMUSE_CMD_PROCESS_STREAM:
			return ImWave_ProcessStreams(im);
		case IMUSE_CMD_QUERY_STREAM:
			return (int)ImWave_QueryNextStream(im, args[0], (int*)args[1], (int*)args[2], (int*)args[3]);
		case IMUSE_CMD_PANIC_INTERNAL:
			return ImMidi_Panic(im);
	}
	ImDebug_LogMsg(im, "ERROR:bogus opcode...");
	return -1;
}
