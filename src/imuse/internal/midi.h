#ifndef __IMUSE_MIDI_H__
#define __IMUSE_MIDI_H__

#include <stdint.h>

#include <imuse/handle.h>

/* MIDI facade over players, slots, and sustain. It owns the reentry lock,
 * sequencer pause flag, and periodic debug refresh state. */

int ImMidi_Init(imuse_t* im);
int ImMidi_Deinit(imuse_t* im);

/* Per-tick step. Called from imuse_advance. Skips entirely when
 * midiLock is held; otherwise advances PLAYERS + SUSTAIN (unless
 * midiPaused) and refreshes the debug overlay every 76 ticks. */
void ImMidi_Update(imuse_t* im);

/* Reentrancy guard. Lock increments a counter, Unlock decrements. Used
 * by many modules (sequencer, tracks, save/restore) to fence critical
 * sections against tick reentry. */
void ImMidi_Lock(imuse_t* im);
void ImMidi_Unlock(imuse_t* im);

int ImMidi_Pause(imuse_t* im);
int ImMidi_Resume(imuse_t* im);

int ImMidi_Save(imuse_t* im, void* buf, int size);
int ImMidi_Restore(imuse_t* im, void* buf);

int ImMidi_StartSound(imuse_t* im, intptr_t soundId, int priority);
int ImMidi_StopSound(imuse_t* im, intptr_t soundId);
int ImMidi_StopAllSounds(imuse_t* im);
intptr_t ImMidi_GetNextSound(imuse_t* im, intptr_t soundId);

int ImMidi_SetParam(imuse_t* im, intptr_t soundId, int param, int value);
int ImMidi_GetParam(imuse_t* im, intptr_t soundId, int param);
void ImMidi_SetHook(imuse_t* im, intptr_t soundId, uint32_t hookId);
int ImMidi_GetHook(imuse_t* im, intptr_t soundId);

/* MIDI-specific opcodes (21..24, 29) dispatched via
 * ImCommands_ExecOpcode for the trigger/defer replay path and called
 * directly from lolevel for immediate requests. */
int ImMidi_Jump(imuse_t* im, intptr_t soundId, int chunk, int measure, int beat, int tick, int sustain);
int ImMidi_Scan(imuse_t* im, intptr_t soundId, int chunk, int measure, int beat, int tick);
int ImMidi_SendMidiMsg(imuse_t* im, intptr_t soundId, int status, int data1, int data2);
int ImMidi_ShareParts(imuse_t* im, intptr_t src, intptr_t dst);
int ImMidi_Panic(imuse_t* im);

/* Re-propagate current groupEffVols[] through every live MIDI player.
 * Called from imuse_set_group_volume after any group volume change. */
void ImMidi_ApplyGroupVol(imuse_t* im);

#endif /* __IMUSE_MIDI_H__ */
