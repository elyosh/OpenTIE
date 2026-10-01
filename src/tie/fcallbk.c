#include "tie/fcallbk.h"
#include "tie_runtime/audio/imuse_api.h"

#include "tie/fscript.h" /* current/next/sequenceID, attributes, *Buildup */

#include <imuse/commands.h> /* imuse_get_param, imuse_set_param, IMUSE_CMD_* */
#include <imuse/filelist.h> /* imuse_filelist_unload, _find */
#include <imuse/hilevel.h>  /* imuse_start_music */
#include <imuse/lolevel.h>  /* imuse_share_parts */
#include <stdint.h>

/* Last buildup level and sound applied by CbSetChannels. Both live in
 * fcallbk's initialized data directly after drawpol's 64-byte
 * markcoloroffset table. */
// GLOBAL: TIE95 0xC1C90
static int16_t cb_prev_attr;
// GLOBAL: TIE95 0xC1C94
static intptr_t cb_last_sound_id;

enum {
	PARAM_VOLUME = 0x0A00,    /* iMUSE master volume for a sound */
	PARAM_CHAN_BASE = 0x1100, /* per-channel volume: 0x1100 + channel */
	PARAM_HOOK = 0x0F00,      /* hook parameter */
	FADE_TICKS = 120,
};

/* Forward declaration */
void fcallbk_CbSetChannels(void);

/* ================================================================ */

// FUNCTION: TIE95 0x1FFB0
void fcallbk_CbInitialize(void) { /* No-op — all state lives in FSCRIPT globals */ }

/*
 * iMUSE trigger callback. Called when a playing sound reaches marker 0.
 * marker_type: 1 = ShareParts (seamless crossfade), 2 = DeferCommand.
 */
// FUNCTION: TIE95 0x1FFB4
int fcallbk_CbDoCallback(int marker_type) {
	int i;
	int16_t chan_vols[FCALLBK_NUM_CHANNELS];
	int16_t saved_volume;

	/* Rearm on the next marker of the playing sound, with this function
	 * as the callback. */
	lolevel_ImSetTrigger((intptr_t)currentID, 0, (intptr_t)fcallbk_CbDoCallback);

	/* Save channel volume state if the sound has a master volume */
	saved_volume = lolevel_ImGetParam(currentID, PARAM_VOLUME);
	if (saved_volume) {
		int i;

		for (i = 0; i < FCALLBK_NUM_CHANNELS; i++)
			chan_vols[i] = lolevel_ImGetParam(currentID, PARAM_CHAN_BASE + i);
	}

	/* If the sequence that just ended IS the current sound, clear it */
	if (sequenceID == currentID) {
		sequenceID = 0;
		sequencePri = 0;
	}

	if (sequenceID) {
		/* A sequence is queued — start it */
		hilevel_ImStartMusic(sequenceID, 0);
		filelist_ImUnloadSound(sequenceID);
		if (marker_type == 1) {
			lolevel_ImShareParts(currentID, sequenceID);
		} else {
			/* Stop the outgoing track 9 ticks later, so the new track has
			 * time to ramp up first. */
			lolevel_ImDeferCommand(9, IMUSE_CMD_STOP_SOUND, (intptr_t)currentID);
		}
		lolevel_ImSetTrigger((intptr_t)sequenceID, 0, (intptr_t)fcallbk_CbDoCallback);
		sequencePri = 0;
		playingState = 0;
		currentID = sequenceID;
		sequenceID = 0;
	} else if (nextID == currentID) {
		/* Stale self-reference — clear */
		nextID = 0;
	} else if (nextID && (marker_type == 1 || (marker_type == 2 && playingState != currentState))) {
		/* Transition to next track */
		hilevel_ImStartMusic(nextID, 0);
		filelist_ImUnloadSound(nextID);
		if (marker_type == 1) {
			lolevel_ImShareParts(currentID, nextID);
		} else {
			lolevel_ImDeferCommand(9, IMUSE_CMD_STOP_SOUND, (intptr_t)currentID);
		}
		lolevel_ImSetTrigger((intptr_t)nextID, 0, (intptr_t)fcallbk_CbDoCallback);
		currentID = nextID;
		nextID = 0;
		currentSequence = 0;
		playingState = currentState;
	}

	/* Restore channel volume state */
	if (!saved_volume) {
		fcallbk_CbSetChannels();
		return 0;
	}

	lolevel_ImSetParam(currentID, PARAM_VOLUME, saved_volume);
	for (i = 0; i < FCALLBK_NUM_CHANNELS; i++)
		lolevel_ImSetParam(currentID, PARAM_CHAN_BASE + i, chan_vols[i]);
	return 0;
}

/*
 * Set MIDI channel volumes based on the current music buildup level.
 * Intro state: uses introBuildup[], with fading for changed channels.
 * Waiting state: uses waitingBuildup[], immediate set.
 */
// FUNCTION: TIE95 0x201C0
void fcallbk_CbSetChannels(void) {
	uint16_t target;
	uint16_t prev;
	uint16_t mask;
	uint16_t i;

	if (playingState == 1 && filelist_ImFindSound("tro-in") != currentID &&
		filelist_ImFindSound("wait-seq") != currentID) {
		lolevel_ImSetParam(currentID, PARAM_HOOK, (uint16_t)attributes[0] + 0x80);
		target = introBuildup[(uint16_t)attributes[0]];
		for (i = 0; i < FCALLBK_NUM_CHANNELS; i++) {
			if (cb_prev_attr != (uint16_t)attributes[0]) {
				/* New buildup level: start from the previous level's
				 * channels and fade those that change. */
				prev = introBuildup[cb_prev_attr];
				lolevel_ImSetParam(currentID, PARAM_CHAN_BASE + i, (prev & (1 << i)) ? 127 : 0);
				if ((prev & (1 << i)) != (target & (1 << i)))
					lolevel_ImFadeParam(currentID, PARAM_CHAN_BASE + i, (target & (1 << i)) ? 127 : 0,
										FADE_TICKS);
			} else if (filelist_ImFindSound("tro-in") == cb_last_sound_id) {
				/* Same level after the intro: fade in from level 0. */
				prev = introBuildup[0];
				lolevel_ImSetParam(currentID, PARAM_CHAN_BASE + i, (prev & (1 << i)) ? 127 : 0);
				if ((prev & (1 << i)) != (target & (1 << i)))
					lolevel_ImFadeParam(currentID, PARAM_CHAN_BASE + i, (target & (1 << i)) ? 127 : 0,
										FADE_TICKS);
			} else if (cb_last_sound_id != currentID) {
				/* Same level on a new sound: set the channels directly. */
				lolevel_ImSetParam(currentID, PARAM_CHAN_BASE + i, (target & (1 << i)) ? 127 : 0);
			}
		}
	} else if (playingState == 2 && filelist_ImFindSound("wait-in") != currentID &&
			   filelist_ImFindSound("wait-seq") != currentID) {
		mask = waitingBuildup[(uint16_t)attributes[0]];
		for (i = 0; i < FCALLBK_NUM_CHANNELS; i++) {
			lolevel_ImSetParam(currentID, PARAM_CHAN_BASE + i, (mask & 1) ? 127 : 0);
			mask >>= 1;
		}
	}

	/* Remember the level and sound for the next call. */
	cb_prev_attr = attributes[0];
	cb_last_sound_id = currentID;
}
