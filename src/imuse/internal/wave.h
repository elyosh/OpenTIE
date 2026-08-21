#ifndef __IMUSE_WAVE_H__
#define __IMUSE_WAVE_H__

#include <stdint.h>

#include <imuse/handle.h>

/* VOC and streamed-audio facade. Its lock counter prevents update reentry;
 * pause state belongs to TRACKS. Sound hooks are unsupported on this path. */

int ImWave_Init(imuse_t* im);
int ImWave_Deinit(imuse_t* im);

/* Per-tick pump. Called from imuse_advance. Skips when
 * wave_wvSlicingHalted is non-zero; otherwise advances TRACKS. */
void ImWave_Update(imuse_t* im);

/* Reentrancy guard for the WAVE side. Lock increments the counter,
 * Unlock decrements (clamped at zero). Used around every public
 * mutator so callbacks invoked from inside TRACKS/STREAMER don't
 * race the outer call. */
void ImWave_Lock(imuse_t* im);
void ImWave_Unlock(imuse_t* im);

int ImWave_Pause(imuse_t* im);
int ImWave_Resume(imuse_t* im);

int ImWave_Save(imuse_t* im, void* buf, int size);
int ImWave_Restore(imuse_t* im, void* buf);

int ImWave_StartSound(imuse_t* im, intptr_t soundId, int priority, uint32_t startFlags);
int ImWave_StopSoundById(imuse_t* im, intptr_t soundId);
int ImWave_StopAllSounds(imuse_t* im);
intptr_t ImWave_GetNextSound(imuse_t* im, intptr_t soundId);
int ImWave_SetParam(imuse_t* im, intptr_t soundId, int param, int value);
int ImWave_GetParam(imuse_t* im, intptr_t soundId, int param);

/* Hooks are MIDI-only. These return -2 and ignore their arguments,
 * matching the DOS engine. The signatures keep the parameters so
 * the COMMANDS dispatcher (IMUSE_CMD_SET_HOOK / IMUSE_CMD_GET_HOOK) can call
 * through a common ImCommands_{Set,Get}Hook indirection. */
void ImWave_SetHook(imuse_t* im, intptr_t soundId, uint32_t hookId);
int ImWave_GetHook(imuse_t* im, intptr_t soundId);

/* Wave-stream opcodes (IMUSE_CMD_START_STREAM..IMUSE_CMD_QUERY_STREAM,
 * 25..28) wired into the dispatcher for the trigger/defer replay
 * path. Direct callers (lolevel) go through these too. */

/* Start a streaming sound bound to buffer slot `bufId` (an index
 * into the host-provided ImuseStreamBuffer[] pool). Rejected if
 * soundId is 0 or -1. */
int ImWave_StartStream(imuse_t* im, intptr_t soundId, int priority, int bufId);

/* Crossfade old→new stream. `crossFadeBuffer`/`size` scratch area
 * is supplied by the caller (the engine does not own it). On
 * Creative VOC streams, `vocLoopFlag` selects whether the new
 * stream starts at the loop point. */
int ImWave_SwitchStream(imuse_t* im, intptr_t oldSoundId, intptr_t newSoundId, void* crossFadeBuffer,
						int crossFadeBufferSize, int vocLoopFlag);

/* One-shot: pump every attached streamer once. Host usually calls
 * this from its refill loop; returns non-zero on streamer error. */
int ImWave_ProcessStreams(imuse_t* im);

/* Enumerate streaming sounds in ascending-soundId order.
 * Pass 0 to start; return value is the next soundId, or 0 at end.
 * For each hit, `*bufSize`, `*lowWaterMark`, `*available` are
 * filled with the streamer-buffer metrics so the host can throttle
 * refills. */
intptr_t ImWave_QueryNextStream(imuse_t* im, intptr_t soundId, int* bufSize, int* lowWaterMark,
								int* available);

/* Re-propagate current groupEffVols[] through every live wave
 * track. Called from imuse_set_group_volume after any group volume
 * change. */
void ImWave_ApplyGroupVol(imuse_t* im);

#endif /* __IMUSE_WAVE_H__ */
