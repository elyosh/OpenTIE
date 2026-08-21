#include "internal/midi.h"
#include "internal/state.h"

#include "internal/debug.h"
#include "internal/players.h"
#include "internal/slots.h"
#include "internal/sustain.h"
#include <imuse/commands.h>

/* Debug-overlay refresh cadence: every 76th tick (~261 ms at the DOS
 * PIT rate). The host-driven Update call is still ticked nominally at
 * the engine's native rate, so the same 76-tick period gives an
 * equivalent refresh frequency regardless of the host's deltas. */
#define IM_MIDI_DEBUG_REFRESH_TICKS 76

/* ===== Lifecycle ===== */

int ImMidi_Init(imuse_t* im) {
	ImDebug_LogMsg(im, "MIDI engine...");
	/* SLOTS → SUSTAIN → PLAYERS. On any failure, unwind in reverse. */
	if (ImSlots_Init(im) != 0)
		return -1;
	if (ImSustain_Init(im) != 0) {
		ImSlots_Deinit(im);
		return -1;
	}
	if (ImPlayers_Init(im) != 0) {
		ImSustain_Deinit(im);
		ImSlots_Deinit(im);
		return -1;
	}

	im->midi.paused = 0;
	im->midi.lock = 0;
	return 0;
}

int ImMidi_Deinit(imuse_t* im) {
	/* Fence the tick first so any in-flight Update() finishes and
	 * subsequent ones skip — then tear down. OR the return codes so
	 * all three subsystems get a chance to Deinit even on error. */
	im->midi.paused = 1;
	im->midi.lock = 1;

	int err = 0;
	err |= ImPlayers_Deinit(im);
	err |= ImSustain_Deinit(im);
	err |= ImSlots_Deinit(im);
	return err;
}

/* ===== Tick + reentrancy lock ===== */

void ImMidi_Lock(imuse_t* im) { ++im->midi.lock; }

void ImMidi_Unlock(imuse_t* im) {
	if (im->midi.lock)
		--im->midi.lock;
}

void ImMidi_Update(imuse_t* im) {
	if (im->midi.lock)
		return;

	/* Self-lock for the duration of the tick so re-entry from inside
	 * PLAYERS / SUSTAIN (trigger callbacks, etc.) hits the lock gate
	 * above and skips rather than recursing. */
	ImMidi_Lock(im);

	if (++im->midi.debugRefreshCounter > IM_MIDI_DEBUG_REFRESH_TICKS - 1) {
		im->midi.debugRefreshCounter = 0;
		ImPlayers_Debug(im);
		ImSlots_Debug(im);
	}

	/* Sequencer advance is paused-sensitive. Debug refresh above still
	 * runs so the operator can watch state while paused. */
	if (!im->midi.paused) {
		ImPlayers_Update(im);
		ImSustain_Update(im);
	}

	ImMidi_Unlock(im);
}

/* ===== Pause / Resume ===== */

int ImMidi_Pause(imuse_t* im) {
	im->midi.paused = 1;
	return 0;
}

int ImMidi_Resume(imuse_t* im) {
	im->midi.paused = 0;
	return 0;
}

/* ===== Save / Restore ===== */

int ImMidi_Save(imuse_t* im, void* buf, int size) { return ImPlayers_Save(im, buf, size); }

int ImMidi_Restore(imuse_t* im, void* buf) { return ImPlayers_Restore(im, buf); }

/* ===== Sound control ===== */

int ImMidi_StartSound(imuse_t* im, intptr_t soundId, int priority) {
	return ImPlayers_StartSound(im, soundId, priority);
}

int ImMidi_StopSound(imuse_t* im, intptr_t soundId) { return ImPlayers_StopSound(im, soundId); }

int ImMidi_StopAllSounds(imuse_t* im) { return ImPlayers_StopAllSounds(im); }

intptr_t ImMidi_GetNextSound(imuse_t* im, intptr_t soundId) { return ImPlayers_FindNextSound(im, soundId); }

int ImMidi_SetParam(imuse_t* im, intptr_t soundId, int param, int value) {
	return ImPlayers_SetParam(im, soundId, param, value);
}

int ImMidi_GetParam(imuse_t* im, intptr_t soundId, int param) {
	return ImPlayers_GetParam(im, soundId, param);
}

void ImMidi_SetHook(imuse_t* im, intptr_t soundId, uint32_t hookId) {
	(void)ImPlayers_SetHook(im, soundId, hookId);
}

int ImMidi_GetHook(imuse_t* im, intptr_t soundId) { return ImPlayers_GetHook(im, soundId); }

/* ===== MIDI-specific opcodes ===== */

int ImMidi_Jump(imuse_t* im, intptr_t soundId, int chunk, int measure, int beat, int tick, int sustain) {
	return ImPlayers_Jump(im, soundId, chunk, measure, beat, tick, sustain);
}

int ImMidi_Scan(imuse_t* im, intptr_t soundId, int chunk, int measure, int beat, int tick) {
	return ImPlayers_Scan(im, soundId, chunk, measure, beat, tick);
}

int ImMidi_SendMidiMsg(imuse_t* im, intptr_t soundId, int status, int data1, int data2) {
	return ImPlayers_SendMidiMsg(im, soundId, status, data1, data2);
}

int ImMidi_ShareParts(imuse_t* im, intptr_t src, intptr_t dst) { return ImPlayers_ShareParts(im, src, dst); }

int ImMidi_Panic(imuse_t* im) {
	ImDebug_LogMsg(im, "Panic Button!...");
	return ImPlayers_Panic(im);
}

/* ===== Group-volume propagation ===== */

void ImMidi_ApplyGroupVol(imuse_t* im) { ImPlayers_ApplyGroupVol(im); }
