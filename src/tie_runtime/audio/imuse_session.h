/*
 * TIE-side libimuse session and sound-id boundary.
 *
 * libimuse is multi-instance internally, but TIE itself uses exactly
 * one session for the whole process. This header provides the global
 * `im` so every TIE source file that calls into libimuse picks up
 * the same handle without TIE having to thread it through every
 * function.
 *
 * Lifecycle: gamesnd_Open_Pre_iMuse opens the session through
 * TieImuseSession_Open; gamesnd_Close_Pre_iMuse stops host rendering and
 * destroys it through TieImuseSession_Close.
 */
#ifndef TIE_IMUSE_SESSION_H
#define TIE_IMUSE_SESSION_H

#include <imuse.h>
#include <stdbool.h>
#include <stdint.h>

#include "tie_runtime/audio/config.h"
#include "tie_runtime/input/input.h"
#include "tie_runtime/storage/storage.h"

extern imuse_t* im;

/* Create the session, its configured MIDI backend, and the host PCM output.
 * get_sound_ptr is the recovered GetSoundAddr resolver. */
bool TieImuseSession_Open(ImuseGetSoundPtrFunc get_sound_ptr);
/* Stop host rendering and destroy the session. No-op before Open. */
void TieImuseSession_Close(void);

/* Advance iMUSE sequencing from the synthetic-clock delta. PCM rendering is
 * independently paced by the host output worker. No-op before Open. */
void TieImuseSession_Advance(int32_t elapsed_us);
bool TieImuseSession_SetMusicDuckingVolumePercent(int percent);

/* Filelist adapters for flight music, whose original load callback returned
 * the fmusic track id as the file handle. */
void* TieImuse_LoadFlightMusic(const char* name);
void TieImuse_UnloadFlightMusic(void* handle);

/* The music scripts seed their random generators from global addresses. */
int32_t TieImuse_AddressSeed(const void* address);

#endif /* TIE_IMUSE_SESSION_H */
