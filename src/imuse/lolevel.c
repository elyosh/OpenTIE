#include <imuse/commands.h> /* ImuseCmd */
#include <imuse/lolevel.h>

#include "internal/fades.h"
#include "internal/midi.h"
#include "internal/triggers.h"
#include "internal/wave.h"

/*
 * libimuse — low-level command surface (impl).
 *
 * Each entry is a thin forwarder to the matching internal module.
 * The "high-level" (commands.h) entries — start_sound, stop_sound,
 * pause, set_param, etc. — live in commands.c since the renamed
 * ImCommands_* functions already have the canonical names. lolevel.c
 * handles triggers, deferred commands, MIDI control, VOC streaming,
 * and fade ramps.
 */

/* ===== Triggers / deferred commands =====
 *
 * The internal ImTriggers_Set / ImTriggers_DeferCommand take an
 * intptr_t[10] arg array. The public ImuseCmd carries the same data
 * plus the opcode in one struct; we pass through the array so the
 * existing dispatch path is unchanged. */

int imuse_set_trigger(imuse_t* im, intptr_t soundId, int marker, const ImuseCmd* cmd) {
	if (!cmd)
		return -5;
	return ImTriggers_Set(im, soundId, marker, cmd->opcode, cmd->args);
}

int imuse_check_trigger(imuse_t* im, intptr_t soundId, int marker, intptr_t opcode) {
	return ImTriggers_Check(im, soundId, marker, opcode);
}

int imuse_clear_trigger(imuse_t* im, intptr_t soundId, int marker, intptr_t opcode) {
	return ImTriggers_Clear(im, soundId, marker, opcode);
}

int imuse_defer_command(imuse_t* im, int tick_count, const ImuseCmd* cmd) {
	if (!cmd)
		return -5;
	return ImTriggers_DeferCommand(im, tick_count, (int)cmd->opcode, cmd->args);
}

/* ===== Fade ramp ===== */
int imuse_fade_param(imuse_t* im, intptr_t soundId, int param, int target, int time) {
	return ImFades_Param(im, soundId, param, target, time);
}

/* ===== MIDI control ===== */

int imuse_midi_jump(imuse_t* im, intptr_t soundId, int chunk, int measure, int beat, int tick, int sustain) {
	return ImMidi_Jump(im, soundId, chunk, measure, beat, tick, sustain);
}

int imuse_midi_scan(imuse_t* im, intptr_t soundId, int chunk, int measure, int beat, int tick) {
	return ImMidi_Scan(im, soundId, chunk, measure, beat, tick);
}

int imuse_send_midi(imuse_t* im, intptr_t soundId, int status, int d1, int d2) {
	return ImMidi_SendMidiMsg(im, soundId, status, d1, d2);
}

int imuse_share_parts(imuse_t* im, intptr_t srcSoundId, intptr_t dstSoundId) {
	return ImMidi_ShareParts(im, srcSoundId, dstSoundId);
}

/* ===== VOC streaming (opcodes 25..28) =====
 *
 * Forwarders only — the engine-side internals (ImWave_*) carry the
 * load. No TIE caller exercises these today. */

int imuse_stream_start(imuse_t* im, intptr_t soundId, int priority, int bufId) {
	return ImWave_StartStream(im, soundId, priority, bufId);
}

int imuse_stream_switch(imuse_t* im, intptr_t oldSoundId, intptr_t newSoundId, void* crossFadeBuffer,
						int crossFadeBufferSize, int vocLoopFlag) {
	return ImWave_SwitchStream(im, oldSoundId, newSoundId, crossFadeBuffer, crossFadeBufferSize, vocLoopFlag);
}

int imuse_stream_process(imuse_t* im) { return ImWave_ProcessStreams(im); }

intptr_t imuse_stream_next(imuse_t* im, intptr_t soundId, int* bufSize, int* lowWaterMark, int* available) {
	return ImWave_QueryNextStream(im, soundId, bufSize, lowWaterMark, available);
}
