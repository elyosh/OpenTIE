#ifndef TIE_RUNTIME_AUDIO_IMUSE_API_H
#define TIE_RUNTIME_AUDIO_IMUSE_API_H

/* The iMUSE API as TIE95 calls it: the lolevel.c, hilevel.c and filelist.c
 * entry points the game linked from the iMUSE SDK, which dispatch to the
 * separately loaded engine through a single command function. Recovered
 * code calls these by their original names and arguments; the modern build
 * forwards them to the runtime's iMUSE session.
 *
 * Sounds are the original sound values (resource pointers or flight sound
 * numbers), carried at pointer width. Return values follow iMUSE: 0 or a
 * queried value on success, -1 when the engine is absent. */

#include <imuse/filelist.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* lolevel.c */
/* Debug output, formatted as printf. The engine prints nothing; TIE98's
 * replacement returns 0 the same way. */
int lolevel_ImPrintf(const char* format, ...);
int lolevel_ImPause(void);
int lolevel_ImResume(void);
int lolevel_ImSetGroupVol(int group, int vol);
int lolevel_ImStartSound(intptr_t sound, int priority);
int lolevel_ImStopSound(intptr_t sound);
int lolevel_ImStopAllSounds(void);
intptr_t lolevel_ImGetNextSound(intptr_t sound);
int lolevel_ImSetParam(intptr_t sound, int param, int value);
int lolevel_ImGetParam(intptr_t sound, int param);
int lolevel_ImFadeParam(intptr_t sound, int param, int target, int time);
int lolevel_ImSetHook(intptr_t sound, int hook);
int lolevel_ImJumpMidi(intptr_t sound, int chunk, int measure, int beat, int tick, int sustain);
int lolevel_ImScanMidi(intptr_t sound, int chunk, int measure, int beat, int tick);
int lolevel_ImShareParts(intptr_t src, intptr_t dst);
int lolevel_ImCheckTrigger(intptr_t sound, int marker, intptr_t opcode);
int lolevel_ImClearTrigger(intptr_t sound, int marker, intptr_t opcode);

/* Queue a command when the sound reaches a marker, or after a number of
 * ticks. The command is an engine opcode followed by the arguments the
 * original callers pass: sounds as intptr_t, every other argument as int.
 * A start passes only the sound. On a trigger, an opcode of 30 or more is a
 * callback, int (*)(int marker), and takes no arguments; deferred commands
 * carry no callbacks, and checks and clears refuse them. */
int lolevel_ImSetTrigger(intptr_t sound, int marker, intptr_t opcode, ...);
int lolevel_ImDeferCommand(int ticks, intptr_t opcode, ...);

/* filelist.c: loaded sound resources, identified by their sound value. */
int filelist_ImInitFilelist(ImuseLoadSoundFunc load, ImuseUnloadSoundFunc unload, ImuseOpenSoundFunc open,
							ImuseCloseSoundFunc close);
intptr_t filelist_ImLoadSound(const char* name);
void filelist_ImUnloadSound(intptr_t sound);
void filelist_ImUnloadAll(void);
intptr_t filelist_ImFindSound(const char* name);
void filelist_ImFlushSounds(void);

/* hilevel.c: group volumes (a volume of -1 queries without changing) and
 * starts that assign the sound's group. */
int hilevel_ImSetMasterVol(int vol);
int hilevel_ImGetMasterVol(void);
int hilevel_ImSetMusicVol(int vol);
int hilevel_ImGetMusicVol(void);
int hilevel_ImSetSfxVol(int vol);
int hilevel_ImGetSfxVol(void);
int hilevel_ImSetVoiceVol(int vol);
int hilevel_ImGetVoiceVol(void);
int hilevel_ImStartSfx(intptr_t sound, int priority);
int hilevel_ImStartVoice(intptr_t sound, int priority);
int hilevel_ImStartMusic(intptr_t sound, int priority);
int hilevel_ImStartDippedMusic(intptr_t sound, int priority);

#ifdef __cplusplus
}
#endif

#endif
