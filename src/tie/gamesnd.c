// FLAGS: TIE95 -od
#include "tie/gamesnd.h"
#include "tie/cdaudio_tie98.h"
#include "tie/fmusic.h"
#include "tie/fsfx.h"
#include "tie/shell.h"
#include "tie/soundext.h"
#include "tie/tie.h" /* colorcycleflag, palette_cycle_user, colorcycleuserflag */
#include "tie_runtime/audio/config.h"
#include "tie_runtime/audio/imuse_api.h"
#include "tie_runtime/audio/imuse_session.h"
#include "tie_runtime/diagnostics/diagnostics.h"
#include "tie_runtime/display/classic_display.h"
#include "tie_runtime/display/classic_framebuffer.h"
#include "tie_runtime/flight_assets/model_types.h"
#include "tie_runtime/input/input.h"
#include "tie_runtime/runtime/exports.h"
#include "tie_runtime/runtime/profile.h"
#include "tie_runtime/storage/storage.h"

#include "landru/sound.h"

#include <imuse/commands.h>
#include <imuse/filelist.h>
#include <imuse/lolevel.h>
#include <stdint.h>
#include <string.h>

/* TIE audio bridge. The runtime iMUSE session owns the engine handle, MIDI
 * backend, PCM output worker, and synthetic-time advance; this module keeps
 * the recovered sound-mode switching and sound-address resolution. */

/* ===== Globals ===== */

// GLOBAL: TIE95 0xFB620
// GLOBAL: TIE98 0x625914
int16_t frontendflag;

/* ===== iMUSE callbacks ===== */

/* ImuseGetSoundPtrFunc: the engine hands us a pointer-width soundId.
 *
 * Three address spaces depending on frontendflag:
 *   == 1 (front-end): soundId is a Sound* -- resolve its data handle.
 *   == 2 (transition): return NULL (engine is being drained).
 *   == 0 (flight):     soundId is a small integer id.
 *     id <  500  -> fsfx SFX index: soundhandles[id]
 *     id >= 500  -> fmusic track id: page in and return the paged
 *                   buffer (id - 500 is the track index).
 */
// FUNCTION: TIE95 0x88BFF
void* gamesnd_GetSoundAddr(intptr_t sound) {
	uint16_t track;

	if (frontendflag == 1) {
		Sound* the_sound = (Sound*)sound;
		if (the_sound && the_sound->data) {
			void* data = xmemhdl_Lock_Handle(the_sound->data);
			xmemhdl_Unlock_Handle(the_sound->data);
			return data;
		}
		return NULL;
	}
	if (frontendflag == 2)
		return NULL;

	/* frontendflag == 0: flight */
	if (sound < 500) {
		uint16_t idx = (uint16_t)sound;
#ifdef TIE_MODERN
		if (idx >= FSFX_NUM_SOUND_HANDLES)
			return NULL;
		return soundhandles[idx];
#else
		{
			void* data = xmemhdl_Lock_Handle(soundhandles[idx]);
			xmemhdl_Unlock_Handle(soundhandles[idx]);
			return data;
		}
#endif
	}
	track = (uint16_t)(sound - 500);
	fmusic_PageSound(track);
	return fmusic_GetPagedSound(track);
}

/* ===== Public API ===== */

// FUNCTION: TIE95 0x88905
int16_t gamesnd_Open_Pre_iMuse(void) {
	/* The runtime session replaces the DOS driver overlay load and
	 * ImInitialize; GetSoundAddr remains the engine's address resolver. */
	if (!TieImuseSession_Open(gamesnd_GetSoundAddr))
		return 0;

	/* The internal wave renderer is always present after I3, so the
	 * digital sub-system is always available. */
	digital_exists = 1;
	return 1;
}

// FUNCTION: TIE95 0x88BDB
void gamesnd_Close_Pre_iMuse(void) { TieImuseSession_Close(); }

// FUNCTION: TIE98 0x42FBA0
void gamesnd_Set_CD_Volume(int volume) {
	if (volume < 0)
		volume = 0;
	if (volume > 16)
		volume = 16;
	cdaudio_Set_Volume((uint32_t)(0xFFFFu * (uint32_t)volume / 16u));
}

/* Transition the already-initialized iMUSE engine from front-end sound
 * (soundext callbacks) to flight music (fmusic callbacks). Retail's
 * GAMESND_game_Open_iMuse at 0x88EF6 does not re-initialize iMUSE -- that
 * would fail with "system already initialized". It only drains the
 * active sounds and swaps the filelist callback pair. */
// FUNCTION: TIE95 0x88EF6
void gamesnd_game_Open_iMuse(void) {
	lolevel_ImStopAllSounds();
	filelist_ImUnloadAll();
	frontendflag = 0;
	lolevel_ImPause();
	filelist_ImInitFilelist(TieImuse_LoadFlightMusic, TieImuse_UnloadFlightMusic, NULL, NULL);
	lolevel_ImResume();
}

// FUNCTION: TIE95 0x88E47
void gamesnd_game_Set_Front_Sound(void) {
	frontendflag = 1;
	lolevel_ImPause();
	filelist_ImInitFilelist(soundext_TIE_Load_Sound, soundext_TIE_Unload_Sound, NULL, NULL);
	lolevel_ImResume();
}

// FUNCTION: TIE95 0x88E8C
void gamesnd_game_Set_Flight_Sound(void) { gamesnd_Transition_Sound(); }

// FUNCTION: TIE95 0x88EB0
// FUNCTION: TIE98 0x425440
void gamesnd_Transition_Sound(void) {
	lolevel_ImStopAllSounds();
	filelist_ImUnloadAll();
	lolevel_ImClearTrigger(-1, -1, -1);
	frontendflag = 2;
}
