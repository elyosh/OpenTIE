#include "internal/state.h"
#include <imuse/groups.h>

#include "internal/midi.h"
#include "internal/wave.h"

/* Raw per-group caller-set volume (0..127). Slot 0 = master. */

/* Effective per-group volume after master-scaling. Computed by
 * imuse_set_group_volume and read by imuse_get_group_volume + every
 * per-voice renderer. */

int ImGroups_Init(imuse_t* im) {
	/* Both arrays seeded to 127 — full volume on every group including
	 * master. The effective table matches raw at init because
	 * (127 * (127 + 1)) >> 7 = 127. */
	for (int i = 0; i < IMUSE_NUM_GROUPS; ++i) {
		im->groups.volsRaw[i] = IMUSE_GROUP_MAX_VOL;
		im->groups.volsEff[i] = IMUSE_GROUP_MAX_VOL;
	}
	return 0;
}

int ImGroups_Deinit(imuse_t* im) {
	/* No teardown needed — arrays live in BSS and will be re-zeroed on
	 * next Init. Matches the DOS binary's bare `return 0`. */
	return 0;
}

int imuse_set_group_volume(imuse_t* im, int group, int vol) {
	if ((unsigned)group >= IMUSE_NUM_GROUPS)
		return -5;

	/* Query mode: return the raw (pre-scaling) value. */
	if (vol == -1)
		return im->groups.volsRaw[group];

	if ((unsigned)vol > IMUSE_GROUP_MAX_VOL)
		return -5;

	int oldRawVol = im->groups.volsRaw[group];

	if (group == IMUSE_GROUP_MASTER) {
		/* Master change: commit and recompute every sub-group effVol
		 * against the new master. The master itself is mirrored into
		 * slot 0 of the effective table. */
		im->groups.volsRaw[0] = vol;
		im->groups.volsEff[0] = vol;
		for (int g = 1; g < IMUSE_NUM_GROUPS; ++g) {
			im->groups.volsEff[g] = (vol * (im->groups.volsRaw[g] + 1)) >> 7;
		}
	} else {
		/* Sub-group change: commit and recompute ONLY its own effVol
		 * against the current master. */
		im->groups.volsRaw[group] = vol;
		im->groups.volsEff[group] = (im->groups.volsRaw[0] * (vol + 1)) >> 7;
	}

	/* Push the updated effVols into every live MIDI player and wave
	 * track. Those walkers re-read im->groups.volsEff[soundGroup] per voice,
	 * so no group/volume arg is needed here. */
	ImMidi_ApplyGroupVol(im);
	ImWave_ApplyGroupVol(im);

	return oldRawVol;
}

int imuse_get_group_volume(imuse_t* im, int group) {
	if ((unsigned)group >= IMUSE_NUM_GROUPS)
		return -5;
	return im->groups.volsEff[group];
}
