#ifndef __IMUSE_FADES_H__
#define __IMUSE_FADES_H__

#include <stdint.h>

#include <imuse/handle.h>

/* Sixteen scheduled integer ramps use a Bresenham accumulator to avoid slope
 * rounding drift. Values dispatch every six 60 Hz ticks and always at the
 * endpoint. A volume fade reaching zero stops the sound. */

#define IM_NUM_FADERS 16

/* Per-slot fader state. 10 fields — values match the DOS layout with
 * `soundId` promoted to intptr_t. */
typedef struct ImSoundFader {
	int status;          /* 0 = free, 1 = active */
	intptr_t soundId;    /* target sound */
	int param;           /* ImuseParam being ramped */
	int currentVal;      /* integer part of the live value */
	int counter;         /* ticks remaining (counts down from length) */
	int length;          /* original fade length in ticks */
	int slope;           /* integer delta per tick = (target - start) / length */
	int slopeMod;        /* fractional part: |target - start| % length */
	int modOvfloCounter; /* Bresenham accumulator, 0..length-1 */
	int stepDir;         /* +1 / -1, added when modOvflo overflows length */
} ImSoundFader;

/* Lifecycle. */
int ImFades_Init(imuse_t* im);
int ImFades_Deinit(imuse_t* im);

/* Save / Restore: memcpy of the native-layout fader table. NOT
 * DOS-format-compatible (ImSoundFader grows from 40 → 48 bytes on LP64
 * because soundId is widened to intptr_t). GetSaveSize tells the
 * COMMANDS save blob exactly how many bytes Save will write. */
int ImFades_Save(imuse_t* im, void* buf, int size);
int ImFades_Restore(imuse_t* im, void* buf);
int ImFades_GetSaveSize(imuse_t* im);

/* Schedule a linear ramp on a sound parameter. Opcode 14 (IMUSE_CMD_FADE_PARAM).
 *
 *   time > 0  allocate a fader slot, seed the ramp, start it running.
 *   time == 0 apply immediately via IMUSE_CMD_SET_PARAM (or IMUSE_CMD_STOP_SOUND if
 *             fading soundVol to 0).
 *
 * Re-fading an existing (sound, param) cancels the in-flight ramp
 * (DisableForSound) before seeding the new one, so the two don't duel.
 *
 * Returns:
 *   0   success
 *  -5   invalid soundId / param / time (< 0)
 *  -6   fader table full (no free slot) */
int ImFades_Param(imuse_t* im, intptr_t soundId, int param, int value, int time);

/* Cancel scheduled fades on a sound. param == -1 cancels all fades on
 * this soundId (wildcard). Does NOT snap the target back to its prior
 * value — the last pushed value is left in place. Does NOT touch the
 * fades_fadesOn fast-path flag; ImFades_Update rebuilds that on the
 * next tick from surviving slot statuses. */
void ImFades_DisableForSound(imuse_t* im, intptr_t soundId, int param);

/* Per-tick step. Decrements every live fader, applies Bresenham-step
 * interpolation, pushes IMUSE_CMD_SET_PARAM every 6 ticks. Called from
 * imuse_advance's 60 Hz tier. */
void ImFades_Update(imuse_t* im);

#endif /* __IMUSE_FADES_H__ */
