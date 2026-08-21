#include "internal/fades.h"
#include "internal/state.h"

#include "internal/debug.h"
#include <imuse/commands.h>

#include <string.h>

/* Integer Bresenham stepping avoids cumulative rounding drift on fade slopes. */

/* ===== Module state =====
 * (ImSoundFader struct definition lives in internal/fades.h so the
 * imuse_t aggregate in internal/state.h can embed the fader pool by
 * value.) */

/* Self-healing "any fader live" fast-path flag. Update clears it and
 * re-raises it per live slot; when every fade drains Update becomes a
 * cheap no-op until ImFades_Param / _Restore arms a new slot. */

/* ===== Lifecycle ===== */

int ImFades_Init(imuse_t* im) {
	ImDebug_LogMsg(im, "FADES module...");
	for (int i = 0; i < IM_NUM_FADERS; ++i)
		im->fades.pool[i].status = 0;
	im->fades.on = 0;
	return 0;
}

int ImFades_Deinit(imuse_t* im) {
	/* Same zero-status sweep as Init, minus the log. Called also from
	 * imuse_stop_all_sounds, so it's part of the hard-reset path too. */
	for (int i = 0; i < IM_NUM_FADERS; ++i)
		im->fades.pool[i].status = 0;
	im->fades.on = 0;
	return 0;
}

/* ===== Save / Restore ===== */

int ImFades_GetSaveSize(imuse_t* im) { return (int)sizeof(im->fades.pool); }

int ImFades_Save(imuse_t* im, void* buf, int size) {
	int needed = ImFades_GetSaveSize(im);
	if (size < needed)
		return -5;
	memcpy(buf, im->fades.pool, sizeof(im->fades.pool));
	return needed;
}

int ImFades_Restore(imuse_t* im, void* buf) {
	memcpy(im->fades.pool, buf, sizeof(im->fades.pool));
	/* Unconditionally raise the flag; Update's self-healing pass clears
	 * it on the next tick if no slot is actually live. Matches the DOS
	 * binary's "assume work to do" approach. */
	im->fades.on = 1;
	return ImFades_GetSaveSize(im);
}

/* ===== Param validation ===== */

/* Whitelist of params the fade-ramp path accepts. Anything else is
 * rejected; FadeParam can't ramp things like soundType or midiBeat
 * that don't have a meaningful linear axis. */
static int fade_param_is_valid(int param) {
	switch (param) {
		case IMUSE_PARAM_SOUND_PRIORITY:
		case IMUSE_PARAM_SOUND_VOL:
		case IMUSE_PARAM_SOUND_PAN:
		case IMUSE_PARAM_SOUND_DETUNE:
		case IMUSE_PARAM_MIDI_SPEED:
			return 1;
		default:
			/* Any per-channel midiPartTrim.chN — the low byte carries the
			 * channel index. */
			return (param & IMUSE_PARAM_KIND_MASK) == IMUSE_PARAM_MIDI_PART_TRIM;
	}
}

/* ===== Fade scheduling ===== */

int ImFades_Param(imuse_t* im, intptr_t soundId, int param, int value, int time) {
	if (soundId == 0 || time < 0 || !fade_param_is_valid(param))
		return -5;

	/* Cancel any existing fade on this (sound, param) so the new ramp
	 * replaces cleanly without a dueling ramp pushing opposite values. */
	ImFades_DisableForSound(im, soundId, param);

	/* time == 0: skip the ramp, apply target immediately. Fading
	 * soundVol to 0 instantly is the canonical "stop me" gesture and
	 * routes through IMUSE_CMD_STOP_SOUND instead of SET_PARAM(vol, 0). */
	if (time == 0) {
		if (param == IMUSE_PARAM_SOUND_VOL && value == 0)
			return imuse_stop_sound(im, soundId);
		return imuse_set_param(im, soundId, param, value);
	}

	/* Ramp path: find a free slot. */
	ImSoundFader* slot = NULL;
	for (int i = 0; i < IM_NUM_FADERS; ++i) {
		if (im->fades.pool[i].status == 0) {
			slot = &im->fades.pool[i];
			break;
		}
	}
	if (!slot) {
		ImDebug_LogMsg(im, "ERROR: fd unable to alloc fade...");
		return -6;
	}

	/* Seed the Bresenham state:
	 *   slope           = signed delta divided by length (int truncation)
	 *   stepDir         = sign of delta, applied each time the
	 *                     fractional accumulator overflows `length`
	 *   slopeMod        = absolute fractional part |delta| % length
	 *   modOvfloCounter = 0 (cleared implicitly by seeding from a
	 *                       zero-status slot; paranoia zero below)
	 * currentVal is read from the live parameter so the ramp starts
	 * from wherever the sound is right now. */
	slot->soundId = soundId;
	slot->param = param;
	slot->currentVal = imuse_get_param(im, soundId, param);
	slot->length = time;
	slot->counter = time;

	int delta = value - slot->currentVal;
	slot->slope = delta / time;
	slot->stepDir = (delta >= 0) ? 1 : -1;

	if (delta < 0)
		delta = -delta;
	slot->slopeMod = delta % time;
	slot->modOvfloCounter = 0;

	slot->status = 1;
	im->fades.on = 1;
	return 0;
}

void ImFades_DisableForSound(imuse_t* im, intptr_t soundId, int param) {
	/* Walk every slot; free the ones matching (soundId, param), with
	 * -1 as a wildcard on param. Intentionally leaves currentVal in
	 * place — the sound is not snapped back to its pre-fade value, the
	 * ramp just stops wherever it is. */
	for (int i = 0; i < IM_NUM_FADERS; ++i) {
		ImSoundFader* slot = &im->fades.pool[i];
		if (slot->status != 0 && slot->soundId == soundId && (param == slot->param || param == -1)) {
			slot->status = 0;
		}
	}
}

/* ===== Per-tick step ===== */

/* Rate-limit the IMUSE_CMD_SET_PARAM pushes to every 6 ticks (~10 Hz). The
 * internal currentVal advances every tick so interpolation accuracy is
 * preserved; only the dispatch call is throttled. counter counts down
 * from `length` to 0 and 0 % 6 == 0, so the final value is guaranteed
 * to land on the last tick. */
#define IM_FADE_PUSH_EVERY_N_TICKS 6

static void fade_push_current(imuse_t* im, const ImSoundFader* slot) {
	/* Fading soundVol to zero is the "stop the sound" gesture — route
	 * through STOP_SOUND instead of SET_PARAM(vol, 0) so the backend
	 * can free the player rather than keeping it alive at silence. */
	if (slot->param == IMUSE_PARAM_SOUND_VOL && slot->currentVal == 0)
		imuse_stop_sound(im, slot->soundId);
	else
		imuse_set_param(im, slot->soundId, slot->param, slot->currentVal);
}

void ImFades_Update(imuse_t* im) {
	if (!im->fades.on)
		return;

	im->fades.on = 0;
	for (int i = 0; i < IM_NUM_FADERS; ++i) {
		ImSoundFader* slot = &im->fades.pool[i];
		if (slot->status == 0)
			continue;

		im->fades.on = 1;

		/* Tick the countdown. Reaching zero deactivates the slot on
		 * this pass — the final value still gets pushed below. */
		if (--slot->counter == 0)
			slot->status = 0;

		/* Linear step: currentVal += slope, with the Bresenham
		 * accumulator kicking an extra ±1 in whenever the fractional
		 * part overflows `length`. */
		int newVal = slot->currentVal + slot->slope;
		unsigned int modAccum = (unsigned int)(slot->slopeMod + slot->modOvfloCounter);
		if (modAccum >= (unsigned int)slot->length) {
			slot->modOvfloCounter = (int)(modAccum - slot->length);
			newVal += slot->stepDir;
		} else {
			slot->modOvfloCounter = (int)modAccum;
		}

		if (newVal == slot->currentVal)
			continue;

		slot->currentVal = newVal;

		/* Throttled push: commit every Nth tick (and on the final
		 * tick, which aligns because counter ∈ {length-1, ..., 0}). */
		if (slot->counter % IM_FADE_PUSH_EVERY_N_TICKS == 0)
			fade_push_current(im, slot);
	}
}
