#include <imuse/commands.h> /* imuse_start_sound, imuse_set_param, ... */
#include <imuse/groups.h>
#include <imuse/hilevel.h>

#include "internal/debug.h"
#include "internal/state.h"

#include <stddef.h>

/*
 * libimuse — high-level convenience helpers.
 *
 * Wraps `imuse_set_group_volume` + `imuse_start_sound` +
 * `imuse_set_param(IMUSE_PARAM_SOUND_GROUP)` into per-group entry
 * points (master / music / sfx / voice / dipped). `ImSetGroupVol(g,
 * -1)` is the query form — returns raw vol without changing it.
 *
 * Also hosts the INSANE wave-fill callback setter
 * (`imuse_set_wave_fill_cb`). The callback is dormant in the demo
 * build; storage exists so a host wiring it up has somewhere to
 * point.
 */

/* --- Volume control --- */

int imuse_set_master_vol(imuse_t* im, int vol) {
	imuse_set_group_volume(im, IMUSE_GROUP_MASTER, vol);
	return 0;
}

int imuse_get_master_vol(imuse_t* im) { return imuse_get_group_volume(im, IMUSE_GROUP_MASTER); }

int imuse_set_music_vol(imuse_t* im, int vol) {
	imuse_set_group_volume(im, IMUSE_GROUP_MUSIC, vol);
	return 0;
}

int imuse_get_music_vol(imuse_t* im) { return imuse_get_group_volume(im, IMUSE_GROUP_MUSIC); }

int imuse_set_sfx_vol(imuse_t* im, int vol) {
	imuse_set_group_volume(im, IMUSE_GROUP_SFX, vol);
	return 0;
}

int imuse_get_sfx_vol(imuse_t* im) { return imuse_get_group_volume(im, IMUSE_GROUP_SFX); }

int imuse_set_voice_vol(imuse_t* im, int vol) {
	imuse_set_group_volume(im, IMUSE_GROUP_VOICE, vol);
	return 0;
}

int imuse_get_voice_vol(imuse_t* im) { return imuse_get_group_volume(im, IMUSE_GROUP_VOICE); }

/* --- Sound start with group assignment --- */

int imuse_start_sfx(imuse_t* im, void* soundId) {
	ImDebug_LogTrace(im, "imuse_start_sfx id=%p", soundId);
	if (imuse_start_sound(im, (intptr_t)soundId, 0))
		return -1;
	if (imuse_set_param(im, (intptr_t)soundId, IMUSE_PARAM_SOUND_GROUP, IMUSE_GROUP_SFX))
		return -1;
	return 0;
}

int imuse_start_voice(imuse_t* im, void* soundId) {
	ImDebug_LogTrace(im, "imuse_start_voice id=%p", soundId);
	if (imuse_start_sound(im, (intptr_t)soundId, 0))
		return -1;
	if (imuse_set_param(im, (intptr_t)soundId, IMUSE_PARAM_SOUND_GROUP, IMUSE_GROUP_VOICE))
		return -1;
	return 0;
}

int imuse_start_music(imuse_t* im, void* soundId) {
	ImDebug_LogTrace(im, "imuse_start_music id=%p", soundId);
	if (imuse_start_sound(im, (intptr_t)soundId, 0))
		return -1;
	if (imuse_set_param(im, (intptr_t)soundId, IMUSE_PARAM_SOUND_GROUP, IMUSE_GROUP_MUSIC))
		return -1;
	return 0;
}

int imuse_start_dipped_music(imuse_t* im, void* soundId) {
	if (imuse_start_sound(im, (intptr_t)soundId, 0))
		return -1;
	if (imuse_set_param(im, (intptr_t)soundId, IMUSE_PARAM_SOUND_GROUP, IMUSE_GROUP_DIPPED))
		return -1;
	return 0;
}

/* --- INSANE cutscene wave-fill callback --- */

void imuse_set_wave_fill_cb(imuse_t* im, ImuseWaveFillFunc cb) { im->hilevel.waveFillFunc = (void*)cb; }
