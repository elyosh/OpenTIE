#include "tie_runtime/audio/imuse_session.h"
#include <stdint.h>

#include "tie/fscript.h"    /* current/next/sequenceID, attributes, *Buildup */
#include <imuse/commands.h> /* imuse_set_param, ImuseCmd, ... */
#include <imuse/filelist.h> /* imuse_filelist_unload, _find */
#include <imuse/hilevel.h>  /* imuse_start_music */
#include <imuse/lolevel.h>  /* imuse_set_trigger, imuse_defer_command */

/* Tracks last-applied state to detect changes. The binary stores this
 * in the upper 16 of a dword at markcoloroffset[64..67]; the low 16
 * is dead memory (no reader, no writer). Shrunk here to a u16. */
static uint16_t cb_prev_attr;
static void* cb_last_sound_id;

#define NUM_CHANNELS 16
#define PARAM_VOLUME 0x0A00    /* iMUSE master volume for a sound */
#define PARAM_CHAN_BASE 0x1100 /* per-channel volume: 0x1100 + channel */
#define PARAM_HOOK 0x0F00      /* hook parameter */
#define FADE_TICKS 120

/* Forward declaration */
int fcallbk_CbSetChannels(void);

/* ================================================================ */

// FUNCTION: TIE 0x1FFB0
void fcallbk_CbInitialize(void) { /* No-op — all state lives in FSCRIPT globals */ }

/*
 * iMUSE trigger callback. Called when a playing sound reaches marker 0.
 * marker_type: 1 = ShareParts (seamless crossfade), 2 = DeferCommand.
 */
int fcallbk_CbDoCallback(int marker_type) {
	int16_t chan_vols[NUM_CHANNELS];

	/* End-of-track callback rearms with `this fn-ptr` as the
	 * opcode (interpreted as a callback because >= IM_OPCODE_MAX);
	 * payload args are unused. */
	ImuseCmd rearm = { .opcode = (intptr_t)fcallbk_CbDoCallback };
	imuse_set_trigger(im, (intptr_t)currentID, 0, &rearm);

	/* Save channel volume state if the sound has a master volume */
	int16_t saved_volume = imuse_get_param(im, (intptr_t)currentID, PARAM_VOLUME);
	if (saved_volume) {
		for (int i = 0; i < NUM_CHANNELS; i++)
			chan_vols[i] = imuse_get_param(im, (intptr_t)currentID, PARAM_CHAN_BASE + i);
	}

	/* If the sequence that just ended IS the current sound, clear it */
	if (sequenceID == currentID) {
		sequenceID = 0;
		sequencePri = 0;
	}

	if (sequenceID) {
		/* A sequence is queued — start it */
		imuse_start_music(im, sequenceID);
		imuse_filelist_unload(im, sequenceID);
		if (marker_type == 1) {
			imuse_share_parts(im, (intptr_t)currentID, (intptr_t)sequenceID);
		} else {
			/* Deferred IMUSE_CMD_STOP_SOUND on the outgoing track 9 ticks
			 * out — the new track has time to ramp up first. */
			ImuseCmd stop = {
				.opcode = IMUSE_CMD_STOP_SOUND,
				.args[0] = (intptr_t)currentID,
			};
			imuse_defer_command(im, 9, &stop);
		}
		ImuseCmd rearm_seq = { .opcode = (intptr_t)fcallbk_CbDoCallback };
		imuse_set_trigger(im, (intptr_t)sequenceID, 0, &rearm_seq);
		sequencePri = 0;
		playingState = 0;
		currentID = sequenceID;
		sequenceID = 0;
	} else if (nextID == currentID) {
		/* Stale self-reference — clear */
		nextID = 0;
	} else if (nextID && (marker_type == 1 || (marker_type == 2 && playingState != currentState))) {
		/* Transition to next track */
		imuse_start_music(im, nextID);
		imuse_filelist_unload(im, nextID);
		if (marker_type == 1) {
			imuse_share_parts(im, (intptr_t)currentID, (intptr_t)nextID);
		} else {
			ImuseCmd stop = {
				.opcode = IMUSE_CMD_STOP_SOUND,
				.args[0] = (intptr_t)currentID,
			};
			imuse_defer_command(im, 9, &stop);
		}
		ImuseCmd rearm_next = { .opcode = (intptr_t)fcallbk_CbDoCallback };
		imuse_set_trigger(im, (intptr_t)nextID, 0, &rearm_next);
		currentID = nextID;
		nextID = 0;
		currentSequence = 0;
		playingState = currentState;
	}

	/* Restore channel volume state */
	if (!saved_volume)
		return fcallbk_CbSetChannels();

	imuse_set_param(im, (intptr_t)currentID, PARAM_VOLUME, saved_volume);
	for (int i = 0; i < NUM_CHANNELS; i++)
		imuse_set_param(im, (intptr_t)currentID, PARAM_CHAN_BASE + i, chan_vols[i]);
	return 0;
}

/*
 * Set MIDI channel volumes based on the current music buildup level.
 * Intro state: uses introBuildup[], with fading for changed channels.
 * Waiting state: uses waitingBuildup[], immediate set.
 */
// FUNCTION: TIE 0x201C0
int fcallbk_CbSetChannels(void) {
	if (playingState == 1 && imuse_filelist_find(im, "tro-in") != currentID &&
		imuse_filelist_find(im, "wait-seq") != currentID) {
		uint16_t target = introBuildup[(uint16_t)attributes[0]];

		for (int ch = 0; ch < NUM_CHANNELS; ch++) {
			int bit = 1 << ch;
			int vol_target = (bit & target) ? 127 : 0;

			/* Determine what the channel was last set to */
			uint16_t last_attr = cb_prev_attr;
			uint16_t other;
			if (last_attr == (uint16_t)attributes[0]) {
				/* Same buildup level — check for special sounds */
				if (imuse_filelist_find(im, "tro-in") == cb_last_sound_id) {
					other = introBuildup[0];
				} else if (cb_last_sound_id == currentID) {
					/* Same level and sound ID advances without a parameter write. */
					continue;
				} else {
					other = target;
				}
			} else {
				other = introBuildup[last_attr];
			}

			/* Set the channel base volume */
			int vol_other = (bit & other) ? 127 : 0;
			imuse_set_param(im, (intptr_t)currentID, PARAM_CHAN_BASE + ch, vol_other);

			/* If target differs from other, fade to target */
			if ((bit & other) != (bit & target))
				imuse_fade_param(im, (intptr_t)currentID, PARAM_CHAN_BASE + ch, vol_target, FADE_TICKS);
		}
	}

	if (playingState == 2 && imuse_filelist_find(im, "wait-in") != currentID &&
		imuse_filelist_find(im, "wait-seq") != currentID) {
		uint16_t mask = waitingBuildup[(uint16_t)attributes[0]];

		for (int ch = 0; ch < NUM_CHANNELS; ch++) {
			int vol = (mask & 1) ? 127 : 0;
			mask >>= 1;
			imuse_set_param(im, (intptr_t)currentID, PARAM_CHAN_BASE + ch, vol);
		}
	}

	/* Track current state for next call */
	cb_prev_attr = (uint16_t)attributes[0];
	cb_last_sound_id = currentID;
	return 0;
}
