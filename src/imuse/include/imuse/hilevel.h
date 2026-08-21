#ifndef __IMUSE_HILEVEL_H__
#define __IMUSE_HILEVEL_H__

#include <stdint.h>

#include <imuse/handle.h>
/* Canonical iMuse sound-group IDs (IMUSE_GROUP_MASTER ..
 * IMUSE_GROUP_DIPPED) come from the engine's GROUPS module. Pulled
 * in here so game-side clients don't need a second include. */
#include <imuse/groups.h>

/*
 * libimuse — convenience wrappers for the four canonical groups.
 *
 * Thin sugar over `imuse_set_group_volume` / `imuse_get_group_volume`
 * + `imuse_set_param(PARAM_SOUND_GROUP)` + `imuse_start_sound`.
 * Provided because TIE relies on them and keeping them in sync with
 * the underlying primitives is a one-liner.
 */

int imuse_set_master_vol(imuse_t* im, int vol);
int imuse_get_master_vol(imuse_t* im);
int imuse_set_music_vol(imuse_t* im, int vol);
int imuse_get_music_vol(imuse_t* im);
int imuse_set_sfx_vol(imuse_t* im, int vol);
int imuse_get_sfx_vol(imuse_t* im);
int imuse_set_voice_vol(imuse_t* im, int vol);
int imuse_get_voice_vol(imuse_t* im);

int imuse_start_sfx(imuse_t* im, void* soundId);
int imuse_start_voice(imuse_t* im, void* soundId);
int imuse_start_music(imuse_t* im, void* soundId);
int imuse_start_dipped_music(imuse_t* im, void* soundId);

#endif
